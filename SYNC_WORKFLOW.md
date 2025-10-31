# HTML Synchronization Workflow

## Overview
This document outlines the process for keeping the standalone HTML development file (`config_interface.html`) and the ESP32 embedded HTML (in `swim_pacer.ino`) perfectly synchronized.

## Sync System Components

### 1. Sync Markers
Both files contain identical HTML content between these markers:
```html
<!-- ========== SYNC MARKER: START ESP32 HTML ========== -->
[HTML CONTENT]
<!-- ========== SYNC MARKER: END ESP32 HTML ========== -->
```

### 2. Validation Scripts
- **sync_check.ps1**: PowerShell script for Windows
- **sync_check.py**: Python script (cross-platform)

Both scripts:
- Extract HTML between sync markers from both files
- Normalize whitespace and comments
- Compare content and report differences
- Exit with code 0 (success) or 1 (needs sync)

## Development Workflow

### For HTML Interface Changes:
1. **Edit the standalone file**: `config_interface.html`
   - This file can be opened directly in browser for testing
   - Contains full HTML structure with sync markers
   - Easier to debug and preview changes

2. **Test your changes**: Open `config_interface.html` in browser

3. **Run sync check**: 
   ```powershell
   powershell -ExecutionPolicy Bypass -File sync_check.ps1
   ```

4. **If files are out of sync**:
   - Copy HTML content from `config_interface.html` (between sync markers)
   - Paste into `swim_pacer.ino` handleRoot() function (between sync markers)
   - OR use the sync helper script (see automation section)

5. **Verify sync**: Run sync check again to confirm

### For ESP32 Backend Changes:
1. **Edit ESP32 code**: `swim_pacer.ino`
   - Modify server handlers, settings, LED control, etc.
   - Do NOT edit HTML content directly in .ino file

2. **For HTML changes needed**:
   - Edit `config_interface.html` first
   - Follow the workflow above to sync back to ESP32

## Sync Rules

### ✅ DO:
- Always edit HTML in `config_interface.html` first
- Use sync checker before committing
- Keep content between sync markers identical
- Test standalone HTML before syncing
- Document any breaking changes

### ❌ DON'T:
- Edit HTML directly in `swim_pacer.ino`
- Commit when sync check fails
- Add development-only content without sync markers
- Break the sync marker comments

## File Structure
```
swim_pacer/
├── config_interface.html     # 🎯 Primary HTML (edit here)
├── swim_pacer.ino            # ESP32 code with embedded HTML
├── sync_check.ps1            # Windows sync validator
├── sync_check.py             # Cross-platform sync validator
└── SYNC_WORKFLOW.md          # This documentation
```

## Sync Automation (Future)

### Auto-Sync Script Concept:
```powershell
# sync_update.ps1 - Automatically update ESP32 from standalone
# 1. Extract HTML from config_interface.html
# 2. Replace HTML in swim_pacer.ino
# 3. Validate sync
# 4. Report success/failure
```

### Git Pre-Commit Hook:
```bash
#!/bin/sh
# Run sync check before allowing commit
if ! powershell -ExecutionPolicy Bypass -File sync_check.ps1; then
    echo "❌ HTML files are not synchronized!"
    echo "💡 Run sync_check.ps1 to see differences"
    exit 1
fi
```

## Troubleshooting

### Common Issues:
1. **Sync check reports differences**:
   - Check for extra whitespace or comments
   - Ensure sync markers are intact
   - Verify no dev-only content in production

2. **PowerShell execution policy errors**:
   ```powershell
   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
   ```

3. **Python script encoding issues**:
   - Ensure UTF-8 encoding in both files
   - Check for BOM (Byte Order Mark) issues

### Sync Check Output:
- **SUCCESS**: Files are identical ✅
- **ERROR**: Shows first difference location and context ❌

## Benefits of This System

1. **Single Source of Truth**: `config_interface.html` is primary
2. **Easy Testing**: Standalone file opens directly in browser
3. **Validation**: Automated sync checking prevents drift
4. **Safety**: Backup and version control of both versions
5. **Development Speed**: Faster HTML iteration cycle

## Maintenance

### Weekly Checks:
- Run sync validator as part of testing
- Verify both files render identically
- Update documentation if workflow changes

### Before Releases:
- Mandatory sync check
- Test ESP32 embedded version
- Validate all features work in both contexts

---

**Remember**: The goal is to maintain identical user experience whether accessing the standalone HTML file or the ESP32's embedded web server!