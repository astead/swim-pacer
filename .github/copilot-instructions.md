# Copilot / AI Agent Instructions for Swim Pacer

This document guides an AI coding agent (or contributor) working on the `swim_pacer` project. It focuses on the repository layout, coding and runtime conventions, important safety rules, and recommended workflows for making changes with minimal risk.

**Project Summary**
- **Purpose:** ESP32-based swim pacing system with a web UI for creating and running swim sets across multiple lanes. LEDs indicate timing and progress.
- **Runtime:** ESP32 (Arduino) with FreeRTOS; LED output via FastLED. The device is authoritative for queue execution; the browser client manages local edits and reconciles with the device.

**Key Files**
- `swim_pacer.ino`: Main firmware. Contains queue processing, LED rendering, HTTP handlers, and core timing code.
- `data/script.js`: Browser client logic — queue UI, reconciliation, UniqueId generation, enqueue helpers, and modal UI code.
- `swim-pacer.html` / `style.css`: Embedded UI assets served from SPIFFS.
- `README.md`: Deploy and quick-start instructions.
- `deploy.ps1`, `deploy-spiffs.ps1`: Windows PowerShell scripts used to build/flash and upload SPIFFS.

**High-Level Conventions**
- **Identifiers:** Use `UniqueId` (lowercase hex string) as the stable identifier for swim sets. The client generates a 16-character lowercase hex `uniqueId`; server and client must canonicalize to lowercase when comparing.
- **Device Authority:** The ESP32 is authoritative for the queue state. The client may optimistically create or edit queue items locally but must use `uniqueId` for reconciliation and rely on the server's `/getSwimQueue` response as the source of truth.
- **JSON payloads:** When creating or updating swim sets, include `uniqueId` and `type`. For LOOP entries include `loopFromUniqueId` and `repeatRemaining`. Inspect `buildMinimalSwimSetPayload` and `handleEnqueueSwimSet` for exact fields.

Example minimal swim set JSON (illustrative — confirm in `data/script.js`):
```
{
  "type": "SWIMSET",
  "uniqueId": "0123456789abcdef",
  "lane": 1,
  "rounds": 3,
  "distanceMeters": 50,
  "swimmers": [ { "swimTimeMs": 30000 }, { "swimTimeMs": 32000 } ],
  "repeatRemaining": 0,
  "loopFromUniqueId": ""
}
```

**HTTP Endpoints (common)**
- `POST /enqueueSwimSet` — enqueue a swim set (use `sendEnqueuePayload` client helper).
- `GET /getSwimQueue` — retrieve current authoritative queue for a lane.
- `POST /updateSwimSet` — update fields of an existing swim set by `uniqueId`.
- `POST /reorderSwimQueue` — reorder the queue on the device.
- `POST /reinitFastLED` — request a soft or full reinitialization of FastLED (use with caution).

**FastLED & Runtime Safety (must-read)**
- Only one task should call `FastLED.show()` — the codebase uses a dedicated `renderTask` that consumes a `renderSemaphore` and holds a `renderLock` mutex.
- Never delete or re-add LED controllers while the renderer may be iterating the buffers. Prefer one of:
  - Perform a safe full restart (preferred) when changing strip count, `ledsPerMeter`, or `stripLengthMeters`.
  - Use a soft-clear reinit that does not destroy controllers if the change is non-structural.
- When touching buffers or controller lists, acquire `renderLock`. When signaling frames for display, release the `renderSemaphore` so the renderer wakes and calls `FastLED.show()` exclusively.
- Avoid heavy per-pixel computations in the display loop — use `spliceOutGaps` mapping and `copySegments` with `memcpy` or `fill_solid` to copy contiguous ranges.

**Performance Notes & Optimizations**
- Cache per-swimmer speed-per-ms and precompute copy segments to avoid inner-loop branching.
- Profile critical paths with the existing micros()/performance timers; avoid printing to Serial in tight loops on device.
- Throttle calls to `FastLED.show()` if frames are not needed; the render task is present to centralize this.

**Developer Workflow for Code Changes**
- Make small, focused PRs. Each PR should:
  - Include a short imperative commit subject (e.g., `Fix: reconcile by UniqueId and two-way sync`).
  - Explain hardware or runtime risks in the PR description (FastLED reinit, memory use).
  - List manual test steps involving flashing and verifying LED behavior (link to `README.md` deploy steps).
- When editing `swim_pacer.ino`:
  - Run static review for memory usage and avoid allocating large temporary buffers on the stack.
  - Ensure `renderLock` is used around any controller/buffer modification.
  - If structural LED changes are required (strip count, lengths), prefer doing a controlled restart to avoid FastLED assertion failures.

**Testing on Hardware**
- Use `.uilduild.options.json` / `.uild` artifacts or run the included PowerShell scripts:
  - `.uild` is a folder; run `.\deploy-spiffs.ps1 -Port COM7` then upload sketch.
  - Alternatively run `.\deploy.ps1` to compile and upload everything (Windows PowerShell).
- After flashing, connect to the board AP `SwimPacer_Config` and open `http://192.168.4.1` to exercise the UI.

**Agent Do/Don't Checklist**
- Do: Keep changes small; prefer add-over-replace for buffers; use `uniqueId`; add safe guards and checks for null/empty arrays.
- Do: Update `README.md` or inline comments when changing payload shapes or endpoint semantics.
- Don't: Remove the `renderTask` or call `FastLED.show()` from new tasks without following the `renderLock`/`renderSemaphore` pattern.
- Don't: Reallocate or free LED controller lists from an ISR or concurrently with the render task.

**Priority TODOs (for humans/agents)**
- Finish server-authoritative LOOP (LOOP_TYPE) runtime behavior and test loops during reorders and deletes.
- Add a small integration test plan describing manual steps to reproduce enqueue/reconcile behaviors.
- Add an endpoint or command-mode to enable on-demand profiler output (to reduce Serial spam during normal runs).

If you need clarification about a specific function or a JSON field, search for the symbol in `data/script.js` and `swim_pacer.ino` (e.g., `reconcileQueueWithDevice`, `buildMinimalSwimSetPayload`, `applySwimSetToSettings`).

Thank you — be cautious with LED controller changes and prefer safe restarts when in doubt.
