// Detect if running in standalone mode (file:// protocol)
const isStandaloneMode = window.location.protocol === 'file:';

const max_swimmers_per_lane = 10;
const default_swimmers_per_lane = 3;
let currentSettings = {
    color: 'red',
    brightness: 196,
    pulseWidth: 1.0,
    restTime: 5,
    swimTime: 30,
    swimDistance: 50,
    swimmerInterval: 4,
    delayIndicatorsEnabled: true,
    maxSwimmersPerLane: max_swimmers_per_lane,
    numSwimmersPerLane: [
        default_swimmers_per_lane,
        default_swimmers_per_lane,
        default_swimmers_per_lane,
        default_swimmers_per_lane],
    numRounds: 10,
    colorMode: 'individual',
    swimmerColor: '#0000ff',
    poolLength: 25,
    poolLengthUnits: 'yards',
    stripLength: 23,  // 75 feet = 23 meters
    ledsPerMeter: 30,
    numLanes: 2,
    currentLane: 0,
    laneNames: ['Lane 1', 'Lane 2', 'Lane 3', 'Lane 4'],
    underwatersEnabled: false,
    lightSize: 1.0,
    firstUnderwaterDistance: 20,
    underwaterDistance: 20,
    surfaceDistance: 2,
    hideAfter: 1,
    underwaterColor: '#0000ff',
    surfaceColor: '#00ff00'
};

// Swimmer set configuration - now lane-specific
let swimmerSets = [[], [], [], []]; // Array of sets for each lane (up to 4 lanes)
let laneRunning = [false, false, false, false]; // Running state for each lane
let currentSwimmerIndex = -1; // Track which swimmer is being edited

// Enum for swim set status bit mask
const SWIMSET_STATUS_PENDING    = 0x0 << 0;
const SWIMSET_STATUS_SYNCHED    = 0x1 << 0;
const SWIMSET_STATUS_ACTIVE     = 0x1 << 1;
const SWIMSET_STATUS_COMPLETED  = 0x1 << 2;

// Swim Set Queue Management - now lane-specific
let swimSetQueues = [[], [], [], []]; // Array of queued swim sets for each lane
let editingSwimSetIndexes = [-1, -1, -1, -1]; // Index of swim set being edited for each lane (-1 if creating new)
let createdSwimSets = [null, null, null, null]; // Temporarily holds created set before queuing for each lane
const swimmerColors = ['red', 'green', 'blue', 'yellow', 'purple', 'cyan', 'red', 'green', 'blue', 'yellow'];
const colorHex = {
    'red': '#ff0000',
    'green': '#00ff00',
    'blue': '#0000ff',
    'yellow': '#ffff00',
    'purple': '#800080',
    'cyan': '#00ffff',
    'red': '#ff0000',
    'green': '#00ff00',
    'blue': '#0000ff',
    'yellow': '#ffff00',
    'custom': '#0000ff', // Default custom color
};

// Lane identification system
let laneIdentificationMode = false;
// TODO: Just re-use the swimmer colors array?
const laneIdentificationColors = ['#ff0000', '#00ff00', '#0000ff', '#ffff00']; // Red, Green, Blue, Yellow

// Convert hex color to color name for swimmer assignment
function hexToColorName(hexColor) {
    // Check if it matches any existing colors
    for (const [colorName, colorValue] of Object.entries(colorHex)) {
        if (colorValue.toLowerCase() === hexColor.toLowerCase()) {
            return colorName;
        }
    }
    // If no match found, update custom color and return 'custom'
    colorHex.custom = hexColor;
    return 'custom';
}

// Page navigation
function showPage(pageId) {
    // Hide all pages
    document.querySelectorAll('.page').forEach(page => page.classList.remove('active'));
    document.querySelectorAll('.nav-tab').forEach(tab => tab.classList.remove('active'));

    // Show selected page
    document.getElementById(pageId).classList.add('active');
    event.target.classList.add('active');
}

// Helper function to parse time input (supports both "30" and "1:30" formats)
function parseTimeInput(timeStr) {
    // Accept numbers or strings; convert to string unless null/undefined.
    if (timeStr === undefined || timeStr === null) return 30; // Default to 30 seconds

    // Coerce non-strings (numbers) to string and trim safely.
    timeStr = String(timeStr).trim();
    if (timeStr === '') return 30; // Default to 30 seconds

    // Check if it contains a colon (minutes:seconds format)
    if (timeStr.includes(':')) {
        const parts = timeStr.split(':');
        if (parts.length === 2) {
            const minutes = parseFloat(parts[0]) || 0;
            const seconds = parseFloat(parts[1]) || 0;
            return (minutes * 60) + seconds;
        }
    }

    // Otherwise, treat as seconds only
    const seconds = parseFloat(timeStr);
    return isNaN(seconds) ? 30 : seconds; // Default to 30 if invalid
}

// Format seconds as M:SS (no decimal places). Rounds to nearest second.
function formatSecondsToMmSs(seconds) {
    if (seconds === undefined || seconds === null || isNaN(seconds)) return '0:00';
    const s = Math.round(Number(seconds));
    const mins = Math.floor(s / 60);
    const secs = s % 60;
    return `${mins}:${secs.toString().padStart(2, '0')}`;
}

// Compact formatting: if <= 60 seconds, show as 'Ns' (e.g., '30s'); if >60, show as M:SS (e.g., '1:30')
function formatCompactTime(seconds) {
    if (seconds === undefined || seconds === null || isNaN(seconds)) return '0s';
    const s = Math.round(Number(seconds));
    if (s > 60) return formatSecondsToMmSs(s);
    return `${s}s`;
}

function updateSwimDistance(triggerSave = true) {
    const swimDistance = parseInt(document.getElementById('swimDistance').value);
    currentSettings.swimDistance = swimDistance;

    if (triggerSave) {
        // Save only the swim distance
        sendSwimDistance(currentSettings.swimDistance);
    }
    // Update swimmer set so UI and created set reflect the new swim distance immediately
    // TODO: I don't think we want this
    //console.log('Swim distance updated - Skipping updating swimmer set from config');
    console.log('Swim distance updated - calling create or updating swimmer set from config');
    try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
}

function updateCalculations(triggerSave = true) {
    const poolLengthElement = document.getElementById('poolLength').value;
    const poolLength = parseFloat(poolLengthElement.split(' ')[0]);
    const poolLengthUnits = poolLengthElement.includes('m') ? 'meters' : 'yards';
    const stripLength = parseFloat(document.getElementById('stripLength').value);
    const ledsPerMeter = parseInt(document.getElementById('ledsPerMeter').value);

    // Update current settings
    currentSettings.poolLength = poolLength;
    currentSettings.poolLengthUnits = poolLengthUnits;
    currentSettings.stripLength = stripLength;
    currentSettings.ledsPerMeter = ledsPerMeter;
    document.getElementById('distanceUnits').textContent = poolLengthUnits;

    // Update distance units when pool length changes.
    // We only want to save the specific fields that changed instead of issuing
    // a broad, multi-endpoint write which would overwrite other device state.
    updateSwimDistance(false);

    // If caller requested saving, call the targeted send helpers so the
    // server only receives the changed values.
    if (triggerSave) {
        sendPoolLength(currentSettings.poolLength, currentSettings.poolLengthUnits);
        sendStripLength(currentSettings.stripLength);
        sendLedsPerMeter(currentSettings.ledsPerMeter);
    }
}

function updateNumLanes() {
    const numLanes = parseInt(document.getElementById('numLanes').value);
    currentSettings.numLanes = numLanes;
    console.log('updateNumLanes: calling updateLaneSelector after setting numLanes to what was selected in UI: ' + numLanes);
    updateLaneSelector();
    updateLaneNamesSection();
    sendNumLanes(numLanes);
}

function updateLaneSelector() {
    console.log('updateLaneSelector() called - currentSettings.numLanes=', currentSettings.numLanes,
                'currentSettings.currentLane=', currentSettings.currentLane,
                'DOM currentLane element exists=', !!document.getElementById('currentLane'));
    const laneSelector = document.getElementById('laneSelector');
    const currentLaneSelect = document.getElementById('currentLane');

    // Show/hide lane selector based on number of lanes
    if (currentSettings.numLanes > 1) {
        console.log(`Updating lane selector to show ${currentSettings.numLanes} lanes`);
        laneSelector.style.display = 'block';

        // Populate lane options
        if (currentLaneSelect) {
            currentLaneSelect.innerHTML = '';
            for (let i = 0; i < currentSettings.numLanes; i++) {
                const option = document.createElement('option');
                option.value = i;
                option.textContent = currentSettings.laneNames[i];
                currentLaneSelect.appendChild(option);
            }

            // Set current lane
            currentLaneSelect.value = currentSettings.currentLane;
        } else {
            console.log('updateLaneSelector: currentLane select element not found in DOM');
        }
    } else {
        console.log('Hiding lane selector for single lane configuration');
        laneSelector.style.display = 'none';
        currentSettings.currentLane = 0; // Default to lane 0 for single lane
    }
}

// Return the currently selected lane from the UI, falling back to currentSettings
function getCurrentLaneFromUI() {
    const el = document.getElementById('currentLane');
    if (!el) return (currentSettings.currentLane !== undefined) ? currentSettings.currentLane : 0;
    const v = parseInt(el.value);
    return isNaN(v) ? ((currentSettings.currentLane !== undefined) ? currentSettings.currentLane : 0) : v;
}

function updateLaneNamesSection() {
    const laneNamesList = document.getElementById('laneNamesList');
    laneNamesList.innerHTML = '';

    for (let i = 0; i < currentSettings.numLanes; i++) {
        const laneDiv = document.createElement('div');
        laneDiv.style.cssText = 'margin: 8px 0; display: flex; align-items: center; gap: 10px;';

        // Color indicator (shown only in identification mode)
        const colorIndicator = document.createElement('div');
        colorIndicator.style.cssText = `width: 20px; height: 20px; border-radius: 50%; border: 2px solid #ddd; display: ${laneIdentificationMode ? 'block' : 'none'};`;
        if (laneIdentificationMode) {
            colorIndicator.style.backgroundColor = laneIdentificationColors[i];
        }

        const input = document.createElement('input');
        input.type = 'text';
        input.value = currentSettings.laneNames[i];
        input.placeholder = `Lane ${i + 1}`;
        input.style.cssText = 'flex: 1; padding: 8px 12px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px;';
        input.onchange = function() {
            currentSettings.laneNames[i] = this.value;
            console.log('updateLaneNamesSection: calling updateLaneSelector after setting laneNames[' + i + '] to: ' + this.value);
            updateLaneSelector(); // Refresh the dropdown on main page
            // Lane names are client-only UI labels; no server update necessary
        };

        laneDiv.appendChild(colorIndicator);
        laneDiv.appendChild(input);
        laneNamesList.appendChild(laneDiv);
    }
}

function toggleLaneIdentification() {
    const button = document.getElementById('identifyLanesBtn');

    if (!laneIdentificationMode) {
        // Start identification mode
        laneIdentificationMode = true;
        button.textContent = 'Stop';
        button.style.backgroundColor = '#dc3545';
        button.style.color = 'white';

        // Update the lane names section to show color indicators
        updateLaneNamesSection();

        // Send command to ESP32 to light up lanes with identification colors
        for (let i = 0; i < currentSettings.numLanes; i++) {
            const color = laneIdentificationColors[i];
            sendLaneIdentificationCommand(i, color);
        }
    } else {
        // Stop identification mode
        laneIdentificationMode = false;
        button.textContent = 'Identify Lanes';
        button.style.backgroundColor = '#007bff';
        button.style.color = 'white';
        button.style.borderColor = '#007bff';

        // Update the lane names section to hide color indicators
        updateLaneNamesSection();

        // Send command to ESP32 to stop identification and resume normal operation
        stopLaneIdentification();
    }
}

// TODO: This is not handled on the server side yet
function sendLaneIdentificationCommand(laneIndex, colorHex) {
    // Convert hex color to RGB
    const r = parseInt(colorHex.substr(1, 2), 16);
    const g = parseInt(colorHex.substr(3, 2), 16);
    const b = parseInt(colorHex.substr(5, 2), 16);

    fetch('/identifyLane', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `lane=${laneIndex}&r=${r}&g=${g}&b=${b}`
    }).catch(error => {
        console.log('Lane identification command failed (standalone mode)');
    });
}

// TODO: This is not handled on the server side yet
function stopLaneIdentification() {
    fetch('/stopIdentification', {
        method: 'POST'
    }).catch(error => {
        console.log('Stop identification command failed (standalone mode)');
    });
}

function updateCurrentLane() {
    const newLane = parseInt(document.getElementById('currentLane').value);
    currentSettings.currentLane = newLane;

    // Get elements for swimmer set management
    const swimmerSetDiv = document.getElementById('swimmerSet');
    const configControls = document.getElementById('configControls');
    const createSetBtn = document.getElementById('createSetBtn');
    const currentSet = getCurrentSwimmerSet();

    // Update button text based on whether the new lane has a set
    if (currentSet.length > 0) {
        createSetBtn.textContent = 'Modify Set';
    } else {
        createSetBtn.textContent = 'Create Set';
    }

    // If swimmer set is currently displayed, update it for the new lane
    if (swimmerSetDiv && swimmerSetDiv.style.display === 'block') {
        if (currentSet.length > 0) {
            // New lane has a set, display it
            displaySwimmerSet();
        } else {
            // New lane has no set, hide set display and show config controls
            configControls.style.display = 'block';
            swimmerSetDiv.style.display = 'none';
            createSetBtn.textContent = 'Create Set';
        }
    }

    // Update the status display for the new lane
    updateStatus();

    // Update the numSwimmers selector to reflect per-lane count if available
    try {
        const laneCount = currentSettings.numSwimmersPerLane[newLane];
        setNumSwimmersUI(laneCount);
    } catch (e) {}

    // Update queue display for the new lane
    updateQueueDisplay();
    updatePacerButtons();


    // Enable/disable reset positions button depending on running state
    try {
        const btn = document.getElementById('resetPositionsBtn');
        if (btn) btn.disabled = !!laneRunning[currentSettings.currentLane];
    } catch (e) {}
}

// Reset swimmers positions for the current lane back to the starting wall
function resetLanePositions() {
    const lane = currentSettings.currentLane || 0;
    // Only allow when not running
    if (laneRunning[lane]) {
        alert('Cannot reset positions while lane is running');
        return;
    }

    // Optimistic UI: disable button while request in flight
    const btn = document.getElementById('resetPositionsBtn');
    if (btn) btn.disabled = true;

    fetch('/resetLane', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body:  JSON.stringify({ lane }),
    }).then(resp => {
        if (!resp.ok) throw new Error('reset failed');
        // On success, reconcile queue and refresh status/UI
        // Ask device for updated queue/state
        setTimeout(async () => {
            reconcileQueueWithDevice();
            await fetchDeviceSettingsAndApply();
            updateStatus();
            updateQueueDisplay();
        }, 200);
    }).catch(err => {
        console.log('resetLanePositions failed', err);
        alert('Failed to reset lane positions');
    }).finally(() => {
        if (btn) btn.disabled = false;
    });
}

function getCurrentSwimmerSet() {
    return swimmerSets[currentSettings.currentLane] || [];
}

function setCurrentSwimmerSet(newSet) {
    swimmerSets[currentSettings.currentLane] = newSet;
}

// Helper functions for running set data (immutable copies)
function updateBrightness() {
    const brightnessPercent = document.getElementById('brightness').value;

    // Convert percentage (0-100) to internal value (20-255)
    // Formula: internal = 20 + (percent / 100) * (255 - 20)
    const internalBrightness = Math.round(20 + (brightnessPercent / 100) * (255 - 20));

    currentSettings.brightness = internalBrightness;
    document.getElementById('brightnessValue').textContent = brightnessPercent + '%';
    sendBrightness(brightnessPercent);
}

function initializeBrightnessDisplay() {
    // Convert internal brightness (20-255) back to percentage (0-100)
    // Formula: percent = (internal - 20) * 100 / (255 - 20)
    const brightnessPercent = Math.round((currentSettings.brightness - 20) * 100 / (255 - 20));

    document.getElementById('brightness').value = brightnessPercent;
    document.getElementById('brightnessValue').textContent = brightnessPercent + '%';
}

function updatePulseWidth() {
    const pulseWidth = document.getElementById('pulseWidth').value;
    currentSettings.pulseWidth = parseFloat(pulseWidth);
    // UI now shows the numeric input directly; send updated value to device
    // Update unit label (singular vs plural)
    try {
        const unitEl = document.getElementById('pulseWidthUnit');
        if (unitEl) unitEl.textContent = (parseFloat(pulseWidth) === 1.0) ? 'foot' : 'feet';
    } catch (e) {}
    sendPulseWidth(pulseWidth);
}

function updateSwimTime() {
    const swimTime = document.getElementById('swimTime').value;
    currentSettings.swimTime = parseInt(swimTime);
    const unit = parseInt(swimTime) === 1 ? ' second' : ' seconds';
    const rlabel = document.getElementById('swimTimeValue');
    if (rlabel) rlabel.textContent = swimTime + unit;
    // Also send swim time to device so it can be used as a default for swim-sets
    // Fire-and-forget but swallow network errors (avoids uncaught AbortError)
    sendSwimTime(currentSettings.swimTime).catch(err => {
        console.debug('sendSwimTime failed (ignored):', err && err.message ? err.message : err);
    });

    // Rebuild swimmer set so UI reflects the new rest time immediately
    // TODO: Why are we doing this?
    console.log('Swim time updated - calling create or updating swimmer set from config');
    try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
}

function updateRestTime() {
    const restTime = document.getElementById('restTime').value;
    currentSettings.restTime = parseInt(restTime);
    const unit = parseInt(restTime) === 1 ? ' second' : ' seconds';
    const rlabel = document.getElementById('restTimeValue');
    if (rlabel) rlabel.textContent = restTime + unit;
    // Also send rest time to device so it can be used as a default for swim-sets
    sendRestTime(currentSettings.restTime).catch(err => {
        console.debug('sendRestTime failed (ignored):', err && err.message ? err.message : err);
    });
    // Rebuild swimmer set so UI reflects the new rest time immediately
    console.log('Rest time updated - calling create or updating swimmer set from config');
    try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
}



function updateSwimmerInterval() {
    const swimmerInterval = document.getElementById('swimmerInterval').value;
    currentSettings.swimmerInterval = parseInt(swimmerInterval);
    const unit = parseInt(swimmerInterval) === 1 ? ' second' : ' seconds';
    const silabel = document.getElementById('swimmerIntervalValue');
    if (silabel) silabel.textContent = swimmerInterval + unit;
    // Also send swimmer interval to device so it can be used as a default for swim-sets
    sendSwimmerInterval(currentSettings.swimmerInterval).catch(err => {
        console.debug('sendSwimmerInterval failed (ignored):', err && err.message ? err.message : err);
    });
    // Rebuild swimmer set so UI reflects the new swimmer interval immediately
    console.log('Swimmer interval updated - calling create or updating swimmer set from config');
    try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
}

function updateDelayIndicatorsEnabled() {
    const enabled = document.getElementById('delayIndicatorsEnabled').checked;
    currentSettings.delayIndicatorsEnabled = enabled;

    // Update toggle labels
    const toggleOff = document.getElementById('delayIndicatorOff');
    const toggleOn = document.getElementById('delayIndicatorOn');

    if (enabled) {
        toggleOff.classList.remove('active');
        toggleOn.classList.add('active');
    } else {
        toggleOff.classList.add('active');
        toggleOn.classList.remove('active');
    }

    sendDelayIndicators(enabled);
}

function updateUnderwatersEnabled(triggerSave = true, enabledArg) {
    const chk = document.getElementById('underwatersEnabled');
    // If caller provided enabledArg, use it and update the checkbox; otherwise read from the checkbox
    let enabled;
    if (typeof enabledArg !== 'undefined') {
        enabled = !!enabledArg;
        if (chk) chk.checked = enabled;
    } else {
        enabled = chk ? chk.checked : false;
    }

    currentSettings.underwatersEnabled = enabled;

    console.log(`Underwaters enabled set to: ${enabled}, triggerSave: ${triggerSave}`);

    // Show/hide underwaters controls
    const controls = document.getElementById('underwatersControls');
    if (controls) controls.style.display = enabled ? 'block' : 'none';

    // Update toggle labels
    const toggleOff = document.getElementById('toggleOff');
    const toggleOn = document.getElementById('toggleOn');

    if (toggleOff && toggleOn) {
        if (enabled) {
            toggleOff.classList.remove('active');
            toggleOn.classList.add('active');
        } else {
            toggleOff.classList.add('active');
            toggleOn.classList.remove('active');
        }
    }

    if (triggerSave === true) {
        sendUnderwaterSettings();
    }
}

function updateLightSize() {
    const lightSize = document.getElementById('lightSize').value;
    currentSettings.lightSize = parseFloat(lightSize);
    // Numeric input shows value; send updated underwater settings to device
    try {
        const unitEl = document.getElementById('lightSizeUnit');
        if (unitEl) unitEl.textContent = (parseFloat(lightSize) === 1.0) ? 'foot' : 'feet';
    } catch (e) {}
    sendUnderwaterSettings();
}

function updateFirstUnderwaterDistance() {
    const distance = document.getElementById('firstUnderwaterDistance').value;
    currentSettings.firstUnderwaterDistance = parseInt(distance);
    const unit = parseInt(distance) === 1 ? ' foot' : ' feet';
    document.getElementById('firstUnderwaterDistanceValue').textContent = distance + unit;
    sendUnderwaterSettings();
}

function updateUnderwaterDistance() {
    const distance = document.getElementById('underwaterDistance').value;
    currentSettings.underwaterDistance = parseInt(distance);
    const unit = parseInt(distance) === 1 ? ' foot' : ' feet';
    document.getElementById('underwaterDistanceValue').textContent = distance + unit;
    sendUnderwaterSettings();
}

function updateHideAfter() {
    const hideAfter = document.getElementById('hideAfter').value;
    currentSettings.hideAfter = parseInt(hideAfter);
    try {
        const unitEl = document.getElementById('hideAfterUnit');
        if (unitEl) unitEl.textContent = (parseInt(hideAfter) === 1) ? 'second' : 'seconds';
    } catch (e) {}
    sendUnderwaterSettings();
}

// -------------------
// UI-only setter helpers (do NOT call updateSettings)
// These are used when rendering device defaults so we don't POST back to the server.
// -------------------

function setRestTimeUI(value) {
    if (value === undefined || value === null) return;
    const v = parseInt(value);
    const el = document.getElementById('restTime');
    if (el) el.value = v;
    currentSettings.restTime = v;
    const unit = v === 1 ? ' second' : ' seconds';
    const label = document.getElementById('restTimeValue');
    if (label) label.textContent = v + unit;
}

function setSwimmerIntervalUI(value) {
    if (value === undefined || value === null) return;
    const v = parseInt(value);
    const el = document.getElementById('swimmerInterval');
    if (el) el.value = v;
    currentSettings.swimmerInterval = v;
    const unit = v === 1 ? ' second' : ' seconds';
    const label = document.getElementById('swimmerIntervalValue');
    if (label) label.textContent = v + unit;
}

function setPoolLengthUI(length, units) {
    if (length === undefined || length === null) return;
    const el = document.getElementById('poolLength');
    let lengthStr = length;
    if (units === 'meters') {
        lengthStr = length + 'm';
    }
    if (el) el.value = lengthStr;
    currentSettings.poolLength = length;
    currentSettings.poolLengthUnits = units;
}

function setPulseWidthUI(value) {
    if (value === undefined || value === null) return;
    const v = parseFloat(value);
    const el = document.getElementById('pulseWidth');
    if (el) el.value = v;
    currentSettings.pulseWidth = v;
    try {
        const unitEl = document.getElementById('pulseWidthUnit');
        if (unitEl) unitEl.textContent = (v === 1.0) ? 'foot' : 'feet';
    } catch (e) {}
}

function setBrightnessUI(percent) {
    if (percent === undefined || percent === null) return;
    const p = Math.round(percent);
    const el = document.getElementById('brightness');
    if (el) el.value = p;
    const internal = Math.round(20 + (p / 100) * (255 - 20));
    currentSettings.brightness = internal;
    const label = document.getElementById('brightnessValue');
    if (label) label.textContent = p + '%';
}

function setFirstUnderwaterDistanceUI(value) {
    if (value === undefined || value === null) return;
    const v = parseInt(value);
    const el = document.getElementById('firstUnderwaterDistance');
    if (el) el.value = v;
    currentSettings.firstUnderwaterDistance = v;
    const unit = v === 1 ? ' foot' : ' feet';
    const label = document.getElementById('firstUnderwaterDistanceValue');
    if (label) label.textContent = v + unit;
}

function setUnderwaterDistanceUI(value) {
    if (value === undefined || value === null) return;
    const v = parseInt(value);
    const el = document.getElementById('underwaterDistance');
    if (el) el.value = v;
    currentSettings.underwaterDistance = v;
    const unit = v === 1 ? ' foot' : ' feet';
    const label = document.getElementById('underwaterDistanceValue');
    if (label) label.textContent = v + unit;
}

function setHideAfterUI(value) {
    if (value === undefined || value === null) return;
    const v = parseInt(value);
    const el = document.getElementById('hideAfter');
    if (el) el.value = v;
    currentSettings.hideAfter = v;
    try {
        const unitEl = document.getElementById('hideAfterUnit');
        if (unitEl) unitEl.textContent = (v === 1) ? 'second' : 'seconds';
    } catch (e) {}
}

function setLightSizeUI(value) {
    if (value === undefined || value === null) return;
    const v = parseFloat(value);
    const el = document.getElementById('lightSize');
    if (el) el.value = v;
    currentSettings.lightSize = v;
    try {
        const unitEl = document.getElementById('lightSizeUnit');
        if (unitEl) unitEl.textContent = (v === 1.0) ? 'foot' : 'feet';
    } catch (e) {}
}

function setNumSwimmersUI(value) {
    if (value === undefined || value === null) return;
    const v = parseInt(value);
    const el = document.getElementById('numSwimmers');
    if (el) el.value = v;
    // Update per-lane value
    const lane = currentSettings.currentLane || 0;
    currentSettings.numSwimmersPerLane[lane] = v;
}

function setNumRoundsUI(value) {
    if (value === undefined || value === null) return;
    const v = parseInt(value);
    const el = document.getElementById('numRounds');
    if (el) el.value = v;
    currentSettings.numRounds = v;
}

// -------------------

function openColorPickerForUnderwater() {
    currentColorContext = 'underwater';
    openColorPicker();
}

function openColorPickerForSurface() {
    currentColorContext = 'surface';
    openColorPicker();
}

function updateNumSwimmers() {
    const numSwimmers = parseInt(document.getElementById('numSwimmers').value);
    const lane = currentSettings.currentLane || 0;
    currentSettings.numSwimmersPerLane[lane] = numSwimmers;
    sendNumSwimmers(lane, numSwimmers);
}

function updateNumRounds() {
    const numRounds = document.getElementById('numRounds').value;
    currentSettings.numRounds = parseInt(numRounds);
    // Fire-and-forget but swallow network errors (avoids uncaught AbortError)
    sendNumRounds(currentSettings.numRounds).catch(err => {
        console.debug('sendNumRounds failed (ignored):', err && err.message ? err.message : err);
    });
    // Rebuild current swimmer set so UI and created set reflect the new rounds immediately
    console.log('Num rounds updated - calling create or updating swimmer set from config');
    try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
}

function updateColorMode() {
    const colorMode = document.querySelector('input[name="colorMode"]:checked').value;
    currentSettings.colorMode = colorMode;
    updateVisualSelection();

    // Only send color mode change; avoid broad writes that would reset swimmers
    if (!isStandaloneMode) {
        fetch('/setColorMode', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `colorMode=${colorMode}`
        }).catch(error => {
            console.log('Color mode update - server not available');
        });
    }
}

function selectIndividualColors() {
    document.getElementById('individualColors').checked = true;
    updateColorMode();
}

function selectSameColor() {
    document.getElementById('sameColor').checked = true;
    updateColorMode();
}

function selectSameColorAndOpenPicker(event) {
    // Prevent the row click from triggering
    event.stopPropagation();

    // Select same color mode
    document.getElementById('sameColor').checked = true;
    updateColorMode();

    // Open color picker
    openColorPicker();
}

function updateVisualSelection() {
    const isIndividual = document.getElementById('individualColors').checked;
    const individualRow = document.getElementById('individualColorsRow');
    const sameColorRow = document.getElementById('sameColorRow');

    // Update visual feedback by highlighting the selected row
    if (isIndividual) {
        individualRow.style.backgroundColor = '#e3f2fd';
        individualRow.style.border = '2px solid #007bff';
        sameColorRow.style.backgroundColor = 'transparent';
        sameColorRow.style.border = '2px solid transparent';
    } else {
        individualRow.style.backgroundColor = 'transparent';
        individualRow.style.border = '2px solid transparent';
        sameColorRow.style.backgroundColor = '#e3f2fd';
        sameColorRow.style.border = '2px solid #007bff';
    }
}

function openColorPicker() {
    // Reset swimmer index (this is called from coach config)
    currentSwimmerIndex = -1;

    // Populate color grid if not already done
    populateColorGrid();

    // Show the custom color picker modal
    document.getElementById('customColorPicker').style.display = 'block';
}

function closeColorPicker() {
    document.getElementById('customColorPicker').style.display = 'none';
    currentSwimmerIndex = -1; // Reset swimmer index when closing
}

function populateColorGrid() {
    const colorGrid = document.getElementById('colorGrid');
    if (!colorGrid) return;
    if (colorGrid.children.length > 0) return; // Already populated

    // Render the shared masterPalette into the grid
    masterPalette.forEach(color => {
        const colorDiv = document.createElement('div');
        colorDiv.className = 'color-swatch';
        colorDiv.setAttribute('data-color', color);
        colorDiv.style.cssText = `
            width: 40px;
            height: 40px;
            background-color: ${color};
            border: 2px solid #333;
            border-radius: 50%;
            cursor: pointer;
            transition: transform 0.1s;
            box-sizing: border-box;
        `;
        colorDiv.onmouseover = () => colorDiv.style.transform = 'scale(1.1)';
        colorDiv.onmouseout = () => colorDiv.style.transform = 'scale(1)';
        colorDiv.onclick = () => selectColor(color);
        colorGrid.appendChild(colorDiv);
    });
}

// Shared palette used by the color picker. Exported here so we can ensure device colors
// are present without destroying the rest of the palette.
const masterPalette = [
    '#ff0000', '#00ff00', '#0000ff', '#ffff00', '#ff00ff', '#00ffff',
    '#800000', '#008000', '#000080', '#808000', '#800080', '#008080',
    '#ff8000', '#80ff00', '#8000ff', '#ff0080', '#0080ff', '#ff8080',
    '#ffa500', '#90ee90', '#add8e6', '#f0e68c', '#dda0dd', '#afeeee',
    '#ffffff', '#c0c0c0', '#808080', '#404040', '#202020', '#000000'
];

// Maximum capacity for the master palette. If more device colors are needed
// we'll replace redundant entries instead of growing unbounded.
const PALETTE_CAPACITY = 32;

// Helper: convert #rrggbb to {r,g,b}
function hexToRgb(hex) {
    if (!hex) return { r: 0, g: 0, b: 0 };
    const s = hex.replace('#', '');
    return {
        r: parseInt(s.substring(0,2), 16),
        g: parseInt(s.substring(2,4), 16),
        b: parseInt(s.substring(4,6), 16)
    };
}

// Euclidean color distance between two hex colors
function colorDistanceHex(a, b) {
    const aa = hexToRgb(a);
    const bb = hexToRgb(b);
    const dr = aa.r - bb.r;
    const dg = aa.g - bb.g;
    const db = aa.b - bb.b;
    return Math.sqrt(dr*dr + dg*dg + db*db);
}

// Ensure the provided hex color exists in masterPalette.
// If palette has room, append. If full, replace the most redundant color
// (the one whose nearest-neighbor distance is smallest). Returns the index.
function ensureColorInPalette(hex) {
    if (!hex) return -1;
    hex = hex.toLowerCase();
    // Normalize to 7-char hex (#rrggbb)
    if (!hex.startsWith('#') && hex.length === 6) hex = '#' + hex;

    const existing = masterPalette.findIndex(c => c.toLowerCase() === hex);
    if (existing !== -1) return existing;

    const MAX = PALETTE_CAPACITY;
    // If there's space, append
    if (masterPalette.length < MAX) {
        masterPalette.push(hex);
        // If DOM already populated, append a swatch
        const colorGrid = document.getElementById('colorGrid');
        if (colorGrid) {
            const colorDiv = document.createElement('div');
            colorDiv.className = 'color-swatch';
            colorDiv.setAttribute('data-color', hex);
            colorDiv.style.cssText = `width:40px;height:40px;background-color:${hex};border:2px solid #333;border-radius:50%;cursor:pointer;transition:transform 0.1s;box-sizing:border-box;`;
            colorDiv.onmouseover = () => colorDiv.style.transform = 'scale(1.1)';
            colorDiv.onmouseout = () => colorDiv.style.transform = 'scale(1)';
            colorDiv.onclick = () => selectColor(hex);
            colorGrid.appendChild(colorDiv);
        }
        return masterPalette.length - 1;
    }

    // Palette full: find the most redundant color (smallest nearest-neighbor distance)
    let bestIdx = 0;
    let bestMinDist = Infinity;
    for (let i = 0; i < masterPalette.length; i++) {
        let minDist = Infinity;
        for (let j = 0; j < masterPalette.length; j++) {
            if (i === j) continue;
            const d = colorDistanceHex(masterPalette[i], masterPalette[j]);
            if (d < minDist) minDist = d;
        }
        if (minDist < bestMinDist) {
            bestMinDist = minDist;
            bestIdx = i;
        }
    }

    // Replace the most redundant color at bestIdx
    masterPalette[bestIdx] = hex;
    // If DOM populated, update the corresponding swatch element
    const colorGrid = document.getElementById('colorGrid');
    if (colorGrid && colorGrid.children.length > bestIdx) {
        const el = colorGrid.children[bestIdx];
        if (el) {
            el.setAttribute('data-color', hex);
            el.style.backgroundColor = hex;
            el.onclick = () => selectColor(hex);
        }
    }
    return bestIdx;
}

function selectColor(color) {
    if (currentSwimmerIndex >= 0) {
        // Updating individual swimmer color
        const currentSet = getCurrentSwimmerSet();
        // Defensive programming: ensure we don't accidentally modify all swimmers
        if (currentSwimmerIndex < currentSet.length) {
            // Store the hex color directly to avoid shared reference issues
            currentSet[currentSwimmerIndex].color = color;
            console.log(`Updated swimmer ${currentSwimmerIndex} in ${currentSettings.laneNames[currentSettings.currentLane]} color to ${color}`);
            displaySwimmerSet();
        }
        currentSwimmerIndex = -1; // Reset
    } else if (currentColorContext === 'underwater') {
        // Updating underwater color
        currentSettings.underwaterColor = color;
        document.getElementById('underwaterColorIndicator').style.backgroundColor = color;
        sendUnderwaterSettings();
    } else if (currentColorContext === 'surface') {
        // Updating surface color
        currentSettings.surfaceColor = color;
        document.getElementById('surfaceColorIndicator').style.backgroundColor = color;
        sendUnderwaterSettings();
    } else {
        // Updating coach config same color setting
        currentSettings.swimmerColor = color;
        document.getElementById('colorIndicator').style.backgroundColor = color;

        // Send the same color for all swimmers
        fetch('/setSwimmerColor', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `color=${encodeURIComponent(currentSettings.swimmerColor)}`
        }).catch(error => {
            console.log('Swimmer color update - server not available');
        });
    }

    currentColorContext = null; // Reset context
    closeColorPicker();
}

function updateSwimmerColor() {
    const swimmerColor = document.getElementById('swimmerColorPicker').value;
    currentSettings.swimmerColor = swimmerColor;

    // Update the color indicator
    document.getElementById('colorIndicator').style.backgroundColor = swimmerColor;

    // Send the color to the server (without resetting the swim set)
    // Skip server communication in standalone mode
    if (!isStandaloneMode) {
        fetch('/setSwimmerColor', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `color=${encodeURIComponent(swimmerColor)}`
        }).catch(error => {
            console.log('Swimmer color update - server not available');
        });
    }
}

let currentColorContext = null; // Track which color picker context we're in
let _draggedQueueIndex = -1; // index of item being dragged within current lane
// When true, broad settings writes are suppressed to avoid overwriting device defaults
let suppressSettingsWrites = false;

// TODO: Need to set this properly
function updateStatus() {
    const queueDisplay = document.getElementById('queueDisplay');
    const queueTitle = queueDisplay.querySelector('h4');
    const toggleBtnElement = document.getElementById('toggleBtn');

    if (queueDisplay) {
        // Prefer the per-lane running state so the border reflects the lane's actual status.
        const lane = currentSettings.currentLane || 0;
        const running = (laneRunning && laneRunning[lane]) ? true : false;
        console.log(`Updating status display: lane ${lane}, laneRunning=${laneRunning}, laneRunning[lane]=${laneRunning[lane]}, effective running=${running}`);
        if (running) {
            // Running: green border
            queueDisplay.style.borderLeft = '4px solid #28a745';
            if (queueTitle) queueTitle.style.color = '#28a745';
        } else {
            // Stopped: red border
            queueDisplay.style.borderLeft = '4px solid #dc3545';
            if (queueTitle) queueTitle.style.color = '#dc3545';
        }
    }

    // toggleBtn doesn't exist in queue-based interface, so check first
    if (toggleBtnElement) {
        toggleBtnElement.textContent = laneRunning[currentSettings.currentLane] ? 'Stop Pacer' : 'Start Pacer';
    }
}

function togglePacer() {
    // Toggle the running state for the current lane
    const currentLane = currentSettings.currentLane;
    laneRunning[currentLane] = !laneRunning[currentLane];

    // Update UI immediately for better responsiveness
    updateStatus();


    // Try to notify server (will fail gracefully in standalone mode)
    fetch('/toggle', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body:  JSON.stringify({ lane: currentLane }),
    })
        .then(response => response.text())
        .then(result => {
            // Server responded - status is already updated via updateStatus()
            console.log('Server response:', result);
        })
        .catch(error => {
            // Server not available (standalone mode), keep local status
            console.log('Running in standalone mode - server not available');
        });
}


// Create or update the current lane's swimmer set from the configuration controls.
// If resetSwimTimes is true, any per-swimmer custom swim time values are overwritten with the
// current swim time input value (useful when the user changes the swim time).
function createOrUpdateSwimmerSetFromConfig(resetSwimTimes = false) {
    console.log('createOrUpdateSwimmerSetFromConfig called with resetSwimTimes =', resetSwimTimes);
    let currentSet = getCurrentSwimmerSet() || [];

    // Ensure currentSettings reflect inputs
    // TODO: Is this needed? Shouldn't updates to any of these already update currentSettings?
    // Let's check if any of these differ from currentSettings before overwriting?
    // Maybe this is used when editing an existing set?
    let settingsMatched = true;
    const currentLane = currentSettings.currentLane;
    const newNumSwimmers = parseInt(document.getElementById('numSwimmers').value);
    if (newNumSwimmers !== currentSettings.numSwimmersPerLane[currentLane]) {
        console.log(`BUG?: numSwimmers for lane ${currentLane} was not updated when creating swim set (variable: ${currentSettings.numSwimmersPerLane[currentLane]} !== HTML:${newNumSwimmers}).`);
        currentSettings.numSwimmersPerLane[currentLane] = newNumSwimmers;
        settingsMatched = false;
    }
    const newSwimmerInterval = parseInt(document.getElementById('swimmerInterval').value);
    if (newSwimmerInterval !== currentSettings.swimmerInterval) {
        console.log(`BUG?: swimmerInterval was not updated when creating swim set (variable: ${currentSettings.swimmerInterval} !== HTML:${newSwimmerInterval}).`);
        currentSettings.swimmerInterval = newSwimmerInterval;
        settingsMatched = false;
    }
    const newNumRounds = parseInt(document.getElementById('numRounds').value);
    if (newNumRounds !== currentSettings.numRounds) {
        console.log(`BUG?: numRounds was not updated when creating swim set (variable: ${currentSettings.numRounds} !== HTML:${newNumRounds}).`);
        currentSettings.numRounds = newNumRounds;
        settingsMatched = false;
    }
    currentSettings.swimTime = parseTimeInput(document.getElementById('swimTime').value);
    const newRestTime = parseInt(document.getElementById('restTime').value);
    if (newRestTime !== currentSettings.restTime) {
        console.log(`BUG?: restTime was not updated when creating swim set (variable: ${currentSettings.restTime} !== HTML:${newRestTime}).`);
        currentSettings.restTime = newRestTime;
        settingsMatched = false;
    }
    if (settingsMatched) {
        console.log('Current settings match UI inputs.');
    }

    // If set is empty, create a fresh set using current inputs
    if (!currentSet || currentSet.length === 0) {
        console.log('Creating new swimmer set from config');
        const newSet = [];
        //for (let i = 0; i < currentSettings.numSwimmersPerLane[currentLane]; i++) {
        for (let i = 0; i < max_swimmers_per_lane; i++) {
            let swimmerColor;
            if (currentSettings.colorMode === 'same') {
                swimmerColor = currentSettings.swimmerColor;
            } else {
                swimmerColor = colorHex[swimmerColors[i]];
            }
            const newSwimmer = {
                id: i + 1,
                color: swimmerColor,
                swimTime: currentSettings.swimTime,
                interval: (i + 1) * currentSettings.swimmerInterval,
                lane: currentSettings.currentLane,
            };
            newSet.push(newSwimmer);
        }

        setCurrentSwimmerSet(newSet);

        // Also store as createdSwimSets so the rest of the UI expects a created set
        console.log('CreateOrUpdateSwimmerSetFromConfig-A: calling generateSetSummary with swimmers:', newSet, 'and settings:', currentSettings);

        createdSwimSets[currentSettings.currentLane] = {
            id: Date.now(),
            lane: currentSettings.currentLane,
            swimmers: JSON.parse(JSON.stringify(newSet)),
            numRounds: currentSettings.numRounds,
            swimDistance: currentSettings.swimDistance,
            swimTime: currentSettings.swimTime,
            restTime: currentSettings.restTime,
            swimmerInterval: currentSettings.swimmerInterval,
            summary: generateSetSummary(newSet, currentSettings)
        };

        console.log('New swim set created:', createdSwimSets[currentSettings.currentLane]);
    } else {
        console.log('Updating existing swimmer set from config');
        // If swim time changed and we must reset per-swimmer customizations, overwrite swim time
        if (resetSwimTimes) {
            for (let i = 0; i < currentSet.length; i++) {
                currentSet[i].swimTime = currentSettings.swimTime;
            }
        }

        // Recompute intervals and ids to keep consistent
        for (let i = 0; i < currentSet.length; i++) {
            currentSet[i].id = i + 1;
            currentSet[i].interval = (i + 1) * currentSettings.swimmerInterval;
            currentSet[i].lane = currentSettings.currentLane;
            // ensure color exists
            if (!currentSet[i].color) {
                currentSet[i].color = (currentSettings.colorMode === 'same') ? currentSettings.swimmerColor : colorHex[swimmerColors[i]];
            }
        }

        setCurrentSwimmerSet(currentSet);
        console.log('CreateOrUpdateSwimmerSetFromConfig-B: calling generateSetSummary with swimmers:', currentSet, 'and settings:', currentSettings);

        // Keep createdSwimSets in sync so queuing/start paths can use it
        createdSwimSets[currentSettings.currentLane] = {
            id: createdSwimSets[currentSettings.currentLane] ? createdSwimSets[currentSettings.currentLane].id : Date.now(),
            lane: currentSettings.currentLane,
            swimmers: JSON.parse(JSON.stringify(currentSet)),
            numRounds: currentSettings.numRounds,
            swimDistance: currentSettings.swimDistance,
            swimTime: currentSettings.swimTime,
            restTime: currentSettings.restTime,
            swimmerInterval: currentSettings.swimmerInterval,
            summary: generateSetSummary(currentSet, currentSettings)
        };
        console.log('Existing swim set updated:', createdSwimSets[currentSettings.currentLane]);
    }

    // Refresh the swimmers UI if visible
    updateSwimmerSetDisplay();
}

// Resize data for a single lane to match device-reported swimmer count
function migrateSwimmerCountsToDeviceForLane(lane, deviceNumSwimmers) {
    if (lane < 0 || lane >= swimmerSets.length) return;
    if (!deviceNumSwimmers || deviceNumSwimmers < 1) return;

    // Record per-lane count in settings
    currentSettings.numSwimmersPerLane[lane] = deviceNumSwimmers;
    console.log(`Migrated (from device) swimmer count for lane ${lane}: ${deviceNumSwimmers}`);

    // If this is the currently selected lane, update UI controls
    if (currentSettings.currentLane === lane) {
        console.log(`Updating UI controls for lane ${lane} after migrating swimmer count from device`);
        try {
            document.getElementById('numSwimmers').value = deviceNumSwimmers;
            setNumSwimmersUI(deviceNumSwimmers);
            displaySwimmerSet();
            updateQueueDisplay();
        } catch (e) {}
    }
}


function generateSetSummary(swimmers, settings) {
    console.log('Generating set summary with swimmers:', swimmers, 'and settings:', settings);
    const swimDistance = settings.swimDistance;
    const swimTime = swimmers[0].swimTime;
    const restTime = settings.restTime;
    const numRounds = settings.numRounds;

    // distance label: use "50's" for plural rounds, "50'" for a single round (no "'s")
    const distanceLabel = (numRounds === 1) ? `${swimDistance}` : `${swimDistance}'s`;

    // Display swim/rest time using compact formatting: 'Ns' for <=60s, 'M:SS' for >60s
    const avgSwimTimeDisplay = formatCompactTime(swimTime);
    const restDisplay = formatCompactTime(restTime);

    return `${numRounds} x ${distanceLabel} on the ${avgSwimTimeDisplay} with ${restDisplay} rest`;
}

function queueSwimSet() {
    const currentLane = currentSettings.currentLane;

    // Ensure we have a created set based on current inputs. If user is on the
    // Swimmer Customizations tab but hasn't explicitly created a set, build it
    // from the current controls so we can queue what they're seeing.
    try {
        if (!createdSwimSets[currentLane]) {
            console.log('We are queuing a new swim set, but it does not exist - Creating from config');
            createOrUpdateSwimmerSetFromConfig(false);
        }
    } catch (e) {
        console.log('Failed to auto-create set before queueing:', e);
    }

    if (!createdSwimSets[currentLane]) {
        alert('No set to queue');
        return;
    }

    // Build minimal payload and send to device queue. Include lane so device
    // can enqueue into the correct per-lane queue.
    const payload = buildMinimalSwimSetPayload(createdSwimSets[currentLane]);
    payload.lane = currentLane;
    // Create a compact clientTempId (64-bit hex string) for deterministic reconciliation
    const clientTempId = (Date.now().toString(16) + Math.floor(Math.random() * 0xFFFFFF).toString(16)).slice(0,16);
    payload.clientTempId = clientTempId;

    console.log('queueSwimSet calling /enqueueSwimSet');
    fetch('/enqueueSwimSet', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    }).then(async resp => {
        if (!resp.ok) throw new Error('enqueue failed');
    // Try to read JSON response which may include a canonical id and echoed clientTempId.
    let json = null;
    try { json = await resp.json(); } catch (e) { /* ignore */ }
    const canonicalId = (json && json.id) ? json.id : createdSwimSets[currentLane].id;
    const echoedClientTemp = (json && json.clientTempId) ? String(json.clientTempId) : clientTempId;
        // Keep local queue in sync for UI responsiveness. Push a deep copy so
        // the UI can continue showing the current created set in the editor.
        const queued = JSON.parse(JSON.stringify(createdSwimSets[currentLane]));
        queued.id = canonicalId;
    queued.clientTempId = echoedClientTemp;
    queued.synced = true;
        swimSetQueues[currentLane].push(queued);
        // Do NOT call returnToConfigMode(); keep the Swimmer Customizations tab visible
        updateQueueDisplay();
        updatePacerButtons();
    }).catch(err => {
        console.log('Failed to enqueue on device, keeping local queue only');
        const localQueued = JSON.parse(JSON.stringify(createdSwimSets[currentLane]));
        // Keep clientTempId so we can reconcile later
        localQueued.clientTempId = clientTempId;
        // Mark as not yet synced
        localQueued.synced = false;
        swimSetQueues[currentLane].push(localQueued);
        // Keep the UI on the swimmers tab so the user can continue customizing
        updateQueueDisplay();
        updatePacerButtons();
    });
}

function cancelSwimSet() {
    const currentLane = currentSettings.currentLane;
    createdSwimSets[currentLane] = null;
    returnToConfigMode();
}

function returnToConfigMode() {
    // Show config controls and hide swimmer set
    try { const cfg = document.getElementById('configControls'); if (cfg) cfg.style.display = 'block'; } catch (e) {}
    try { const s = document.getElementById('swimmerSet'); if (s) s.style.display = 'none'; } catch (e) {}

    // Show queue button group and hide edit group
    try { const q = document.getElementById('queueButtons'); if (q) q.style.display = 'block'; } catch (e) {}
    try { const eb = document.getElementById('editButtons'); if (eb) eb.style.display = 'none'; } catch (e) {}
    try { const cfgBtns = document.getElementById('configButtons'); if (cfgBtns) cfgBtns.style.display = 'block'; } catch (e) {}
}

function updateQueueDisplay() {
    const lane = currentSettings.currentLane || 0;
    const listEl = document.getElementById('queueList');
    if (!listEl) return;

    // Rebuild the entire list to ensure DOM matches swimSetQueues state.
    listEl.innerHTML = '';

    const queue = swimSetQueues[lane] || [];
    if (queue.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'queue-empty';
        empty.textContent = 'Queue is empty';
        listEl.appendChild(empty);
        return;
    }

    queue.forEach((entry, idx) => {
        const row = document.createElement('div');
        row.className = 'queue-item';

        // Status is a number (bitmask)
        const statusVal = entry.status;
        const isCompleted = !!(statusVal & SWIMSET_STATUS_COMPLETED);
        const isActive = !!(statusVal & SWIMSET_STATUS_ACTIVE);
        const isDeletedPending = !!entry.deletedPending;

        // Completed
        if (isCompleted && !isActive) {
            row.classList.add('completed');
            const label = document.createElement('span');
            label.className = 'queue-label';
            label.textContent = (entry.summary || (`${entry.rounds} x ${entry.swimDistance}'s`));
            row.appendChild(label);

            const info = document.createElement('span');
            info.className = 'queue-info';
            info.textContent = 'Completed';
            row.appendChild(info);

            const actions = document.createElement('span');
            actions.className = 'queue-actions';

            // Allow removal of completed sets (delete) but disable edit/run
            const removeBtn = document.createElement('button');
            removeBtn.textContent = 'Remove';
            removeBtn.onclick = () => { deleteSwimSet(idx); };
            actions.appendChild(removeBtn);

            row.appendChild(actions);
            listEl.appendChild(row);
            return;
        }

        // Running / Active
        if (isActive) {
            row.classList.add('running');
            const label = document.createElement('span');
            label.className = 'queue-label';
            label.textContent = (entry.summary || (`${entry.rounds} x ${entry.swimDistance}'s`)) + ' — Running';
            row.appendChild(label);

            const statusSpan = document.createElement('span');
            statusSpan.className = 'queue-status';
            const roundText = (entry.currentRound !== undefined) ? `${entry.currentRound} / ${entry.numRounds}` : 'In progress';
            statusSpan.textContent = roundText;
            row.appendChild(statusSpan);

            listEl.appendChild(row);
            return;
        }

        // If this entry is marked as pending deletion, render as such (but keep visible).
        if (entry.deletedPending) {
            row.classList.add('pending-delete');
            const label = document.createElement('span');
            label.className = 'queue-label';
            label.textContent = entry.summary || (`${entry.rounds} x ${entry.swimDistance}'s`);
            row.appendChild(label);

            const status = document.createElement('span');
            status.className = 'queue-status';
            status.textContent = 'Deleting...';
            row.appendChild(status);

            // Disabled controls placeholder so layout doesn't shift
            const btnArea = document.createElement('span');
            btnArea.className = 'queue-actions';
            const disabledBtn = document.createElement('button');
            disabledBtn.textContent = 'Delete';
            disabledBtn.disabled = true;
            btnArea.appendChild(disabledBtn);
            row.appendChild(btnArea);

            listEl.appendChild(row);
            return;
        }

        // Normal (not pending/completed) rendering
        const label = document.createElement('span');
        label.className = 'queue-label';
        label.textContent = entry.summary || (`${entry.rounds} x ${entry.swimDistance}'s`);
        row.appendChild(label);

        const actions = document.createElement('span');
        actions.className = 'queue-actions';

        const editBtn = document.createElement('button');
        editBtn.textContent = 'Edit';
        editBtn.onclick = () => { editSwimSet(idx); };
        actions.appendChild(editBtn);

        const runBtn = document.createElement('button');
        runBtn.textContent = 'Run';
        runBtn.onclick = () => { runSwimSetNow(idx); };
        actions.appendChild(runBtn);

        const delBtn = document.createElement('button');
        delBtn.textContent = 'Delete';
        delBtn.onclick = () => { deleteSwimSet(idx); };
        actions.appendChild(delBtn);

        row.appendChild(actions);

        listEl.appendChild(row);
    });
}

function updatePacerButtons() {
    const currentLane = currentSettings.currentLane;
    const pacerButtons = document.getElementById('pacerButtons');
    const startBtn = document.getElementById('startBtn');
    const stopBtn = document.getElementById('stopBtn');

    if (!pacerButtons || !startBtn || !stopBtn) {
        console.error('Pacer button elements not found');
        return;
    }

    if (swimSetQueues[currentLane].length > 0) {
        pacerButtons.style.display = 'block';
        if (laneRunning[currentLane]) {
            startBtn.style.display = 'none';
            stopBtn.style.display = 'block';
        } else {
            startBtn.style.display = 'block';
            stopBtn.style.display = 'none';
        }
    } else {
        pacerButtons.style.display = 'none';
    }
}

function editSwimSet(index) {
    const currentLane = currentSettings.currentLane;

    if (index < 0 || index >= swimSetQueues[currentLane].length) return;

    const swimSet = swimSetQueues[currentLane][index];
    editingSwimSetIndexes[currentLane] = index;

    // Load swim set data into configuration
    loadSwimSetIntoConfig(swimSet);

    // Switch to the Swim Set Details tab and show the configuration controls
    // so the user edits the set details (not the swimmer-customizations view).
    try {
        setCreateTab('details');
    } catch (e) {
        // Fallback: ensure config controls are visible
        document.getElementById('configControls').style.display = 'block';
        document.getElementById('swimmerSet').style.display = 'none';
    }

    // Switch to edit buttons (hide queue button group and any config buttons)
    try {
        const cfg = document.getElementById('configButtons'); if (cfg) cfg.style.display = 'none';
    } catch (e) {}
    try {
        const qb = document.getElementById('queueButtons'); if (qb) qb.style.display = 'none';
    } catch (e) {}
    try {
        const eb = document.getElementById('editButtons'); if (eb) eb.style.display = 'block';
    } catch (e) {}
}

function loadSwimSetIntoConfig(swimSet) {
    console.log('Loading swim set into config for editing:', swimSet);
    // Restore settings
    // Copy settings into currentSettings (shallow copy) so we can modify freely
    Object.assign(currentSettings, JSON.parse(JSON.stringify(swimSet.settings || {})));

    // Set current lane
    currentSettings.currentLane = swimSet.lane;
    // Check if the lane differs from the current UI selection
    if (currentSettings.currentLane !== getCurrentLaneFromUI()) {
        console.log('loadSwimSetIntoConfig: Current lane differs from UI selection, updating UI to match loaded swim set lane');
        // Update any UI elements that depend on current lane
        updateLaneUI(currentSettings.currentLane);
    }

    // Restore swimmer set
    // Use a deep copy to avoid mutating the queued swimSet until the user saves
    setCurrentSwimmerSet(JSON.parse(JSON.stringify(swimSet.swimmers || [])));

    // Update all UI elements to reflect loaded settings
    updateAllUIFromSettings();

    // Update visual selection after UI updates
    updateVisualSelection();
}

function saveSwimSet() {
    const currentLane = currentSettings.currentLane;

    if (editingSwimSetIndexes[currentLane] === -1) return;
    // Update the swim set in the current lane's queue
    const currentSet = getCurrentSwimmerSet();

    // Prepare local copy of the queued entry
    const existing = swimSetQueues[currentLane][editingSwimSetIndexes[currentLane]] || {};
    const newEntry = JSON.parse(JSON.stringify(existing));
    newEntry.swimmers = JSON.parse(JSON.stringify(currentSet));
    //newEntry.settings = JSON.parse(JSON.stringify(currentSettings));
    newEntry.numRounds = currentSet.numRounds;
    newEntry.swimDistance = currentSet.swimDistance;
    newEntry.swimTime = currentSet.swimTime;
    newEntry.restTime = currentSet.restTime;
    newEntry.swimmerInterval = currentSet.swimmerInterval;
    console.log('saveSwimSet: calling generateSetSummary with swimmers:', newEntry.swimmers, 'and settings:', currentSettings);
    newEntry.summary = generateSetSummary(newEntry.swimmers, currentSettings);

    // Prepare payload for update endpoint
    const payload = buildMinimalSwimSetPayload(newEntry);
    payload.lane = currentLane;

    if (existing.id && existing.id !== 0) {
        payload.matchId = existing.id;
    }
    else if (existing.clientTempId) payload.matchClientTempId = String(existing.clientTempId);

    // Generate a new clientTempId to set on the server for correlation
    const newClientTemp = (Date.now().toString(16) + Math.floor(Math.random() * 0xFFFFFF).toString(16)).slice(0,16);
    payload.clientTempId = newClientTemp;

    // Try to update on the device; if network fails, apply local-only change
    console.log('saveSwimSet calling /updateSwimSet with payload:', payload);
    fetch('/updateSwimSet', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    }).then(async resp => {
        if (!resp.ok) throw new Error('update failed');
        const json = await resp.json().catch(() => null);
        // Merge server-assigned values back into our local entry
        if (json && json.id) newEntry.id = json.id;
        if (json && json.clientTempId) newEntry.clientTempId = String(json.clientTempId);
        newEntry.synced = true;
        newEntry.updatePending = false;
        swimSetQueues[currentLane][editingSwimSetIndexes[currentLane]] = newEntry;
        console.log("updated successfully on server");
        // Clear editing state and return to create mode
        editingSwimSetIndexes[currentLane] = -1;
        createdSwimSets[currentLane] = null;
        returnToConfigMode();
        updateQueueDisplay();
    }).catch(err => {
        // Device not reachable or update failed — keep local change and mark unsynced
        console.debug('saveSwimSet: network/update error - marking updatePending', err && err.message ? err.message : err);
        newEntry.synced = false;
        newEntry.updatePending = true;
        newEntry.updateRequestedAt = Date.now();
        newEntry.clientTempId = newClientTemp;
        swimSetQueues[currentLane][editingSwimSetIndexes[currentLane]] = newEntry;
        editingSwimSetIndexes[currentLane] = -1;
        createdSwimSets[currentLane] = null;
        returnToConfigMode();
        updateQueueDisplay();
    });
}

function cancelEdit() {
    const currentLane = currentSettings.currentLane;
    // Discard edits and return to configuring a new swim set
    editingSwimSetIndexes[currentLane] = -1;
    // Clear any created/edited temporary set so user returns to blank/new state
    createdSwimSets[currentLane] = null;
    returnToConfigMode();
}

// When editing/creating a set, return to the basic details view without
// discarding the created set. This gives a clear 'back' action instead of
// an ambiguous Cancel which might be interpreted as discard.
function backToSetDetails() {
    // Show config controls and hide the swimmer customization display
    returnToConfigMode();
}

function deleteSwimSet(index) {
    const currentLane = currentSettings.currentLane;

    if (index < 0 || index >= swimSetQueues[currentLane].length) return;

    if (!confirm('Delete this swim set from Lane ' + (currentLane + 1) + '?')) return;

    // Mark locally as pending delete rather than immediately removing.
    // This prevents a later reconcile from re-adding it if the server delete fails.
    const item = swimSetQueues[currentLane][index];
    item.deletedPending = true;
    item.deleteRequestedAt = Date.now();

    // Update UI to reflect pending deletion (disable buttons / show status)
    updateQueueDisplay();
    updatePacerButtons();

    // Attempt server delete (best-effort). On success remove; on failure keep pending and retry later.
    attemptDeleteOnServer(currentLane, item).catch(err => {
        console.debug('Initial delete request failed, will retry later', err && err.message ? err.message : err);
    });
}

// Try to delete an item on the server; resolves true when deleted on server.
async function attemptDeleteOnServer(lane, item) {
    if (isStandaloneMode) {
        // In standalone mode just remove locally after short delay
        setTimeout(() => {
            try {
                const idx = swimSetQueues[lane].indexOf(item);
                if (idx !== -1) {
                    swimSetQueues[lane].splice(idx, 1);
                    updateQueueDisplay();
                    updatePacerButtons();
                }
            } catch (e) {}
        }, 50);
        return true;
    }

    if (item.id && item.id !== 0) {
        console.log('Attempting delete on server for item with id:', item.id);
    } else if (item.clientTempId) {
        console.log('Attempting delete on server for item with clientTempId:', item.clientTempId);
    } else {
        console.log('No id or clientTempId to match swim set for deletion on server, removing locally only');
        // nothing to match on, remove locally
        const idx = swimSetQueues[lane].indexOf(item);
        if (idx !== -1) swimSetQueues[lane].splice(idx, 1);
        updateQueueDisplay();
        updatePacerButtons();
        return false;
    }

    try {
        // Build JSON body
        const body = {};
        if (item.id && item.id !== 0) body.matchId = Number(item.id);
        else if (item.clientTempId) body.matchClientTempId = String(item.clientTempId);
        body.lane = lane;
        console.log("calling /deleteSwimSet with body:", body);
        const resp = await fetch('/deleteSwimSet', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });
        if (resp.ok) {
            // Remove locally on confirmed deletion
            const idx = swimSetQueues[lane].indexOf(item);
            if (idx !== -1) swimSetQueues[lane].splice(idx, 1);
            updateQueueDisplay();
            updatePacerButtons();
            return true;
        } else {
            // leave item.pending so retry logic can pick it up
            return false;
        }
    } catch (e) {
        // Network failed - keep pending
        return false;
    }
}

// Periodic retry for pending deletes. Call from startup and reconcile paths.
async function retryPendingDeletes() {
    if (isStandaloneMode) return;
    for (let lane = 0; lane < swimSetQueues.length; lane++) {
        for (let i = swimSetQueues[lane].length - 1; i >= 0; i--) {
            const it = swimSetQueues[lane][i];
            if (it && it.deletedPending) {
                // attempt delete, but don't block UI
                try {
                    await attemptDeleteOnServer(lane, it);
                } catch (e) {
                    // ignore, will retry next time
                }
            }
        }
    }
}

// Periodic retry for pending updates (call from startup)
async function retryPendingUpdates() {
    if (isStandaloneMode) return;
    for (let lane = 0; lane < swimSetQueues.length; lane++) {
        for (let i = 0; i < swimSetQueues[lane].length; i++) {
            const it = swimSetQueues[lane][i];
            if (it && it.updatePending) {
                // Re-send update to server (best-effort)
                try {
                    const body = {
                        matchId: it.id || 0,
                        matchClientTempId: it.clientTempId || undefined,
                        lane: lane,
                        rounds: it.settings.numRounds || it.rounds,
                        swimDistance: it.settings.swimDistance || it.swimDistance,
                        swimSeconds: it.settings.swimSeconds || it.swimTime || it.swimSeconds,
                        restSeconds: it.settings.restTime || it.restSeconds,
                        swimmerInterval: it.settings.swimmerInterval || it.swimmerInterval,
                        type: it.type || 0,
                        repeat: it.repeat || 0,
                    };
                    const ctrl = new AbortController();
                    const to = setTimeout(() => ctrl.abort(), 7000);
                    const resp = await fetch('/updateSwimSet', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(body),
                        signal: ctrl.signal
                    });
                    clearTimeout(to);
                    if (resp.ok) {
                        const j = await resp.json().catch(()=>null);
                        it.updatePending = false;
                        if (j && j.id) it.id = j.id;
                        if (j && j.clientTempId) it.clientTempId = j.clientTempId;
                        swimSetQueues[lane][i] = it;
                        updateQueueDisplay();
                    }
                } catch (e) {
                    // ignore; will retry later
                }
            }
        }
    }
}


// Drag & Drop handlers for reordering queue items
function handleDragStart(evt, index) {
    const currentLane = currentSettings.currentLane;
    // Safeguard: do not allow dragging items that are completed
    const isCompleted = !!(swimSetQueues[currentLane] && swimSetQueues[currentLane][index] && swimSetQueues[currentLane][index].completed);
    if (isCompleted) {
        evt.preventDefault();
        return;
    }

    _draggedQueueIndex = index;
    try { evt.dataTransfer.setData('text/plain', String(index)); } catch (e) {}
    evt.dataTransfer.effectAllowed = 'move';
}

function handleDragOver(evt) {
    evt.preventDefault();
    evt.dataTransfer.dropEffect = 'move';
}

function handleDrop(evt, targetIndex) {
    evt.preventDefault();
    const currentLane = currentSettings.currentLane;
    const from = _draggedQueueIndex;
    let to = targetIndex;
    if (from < 0 || to < 0 || from === to) return;

    // Disallow dropping a future (non-completed, non-running) item before any
    // completed or currently-running items. Compute the last protected index
    // (highest index of any completed or active item). Future sets must be
    // positioned strictly after that index.
    let lastProtectedIndex = -1;
    for (let i = 0; i < swimSetQueues[currentLane].length; i++) {
        const s = swimSetQueues[currentLane][i];
        const isCompleted = !!s.completed;
        if (isCompleted) lastProtectedIndex = Math.max(lastProtectedIndex, i);
    }

    if (lastProtectedIndex >= 0 && to <= lastProtectedIndex) {
        // Moving a pending set before a protected set is not allowed
        alert('Cannot move a future swim set before a completed or running set.');
        _draggedQueueIndex = -1;
        return;
    }

    // When removing an element that appears before the insertion point, the
    // indices shift left by one. Adjust target index accordingly.
    if (from < to) to = to - 1;

    // Reorder locally
    const item = swimSetQueues[currentLane].splice(from, 1)[0];
    swimSetQueues[currentLane].splice(to, 0, item);

    // Clear dragged index and refresh UI
    _draggedQueueIndex = -1;
    updateQueueDisplay();

    // Send new order to device: POST lane & order (comma-separated ids or clientTempIds)
    const order = swimSetQueues[currentLane].map(s => s.id && s.id !== 0 ? String(s.id) : String(s.clientTempId || '0')).join(',');
    console.log('handleDrop calling /reorderSwimQueue');
    fetch('/reorderSwimQueue', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ lane: currentLane, order })
    }).then(resp => {
        if (!resp.ok) throw new Error('reorder failed');
        // Optionally reconcile response in the future
    }).catch(err => {
        console.log('reorder request failed, will keep local order until reconciliation', err);
    });
}

function loadSwimSetForExecution(swimSet) {
    console.log('Loading swim set for execution:', swimSet);
    // Set the current lane
    currentSettings.currentLane = swimSet.lane;
    // Check if the lane differs from the current UI selection
    if (currentSettings.currentLane !== getCurrentLaneFromUI()) {
        console.log('loadSwimSetForExecution: Current lane differs from UI selection, updating UI to match loaded swim set lane');
        // Update any UI elements that depend on current lane
        updateLaneUI(currentSettings.currentLane);
    }

    // Load settings
    Object.assign(currentSettings, swimSet.settings);

    // Update the DOM input fields with the swim set's settings
    if (swimSet.settings.swimTime !== undefined) {
        const el = document.getElementById('swimTime');
        if (el) el.value = formatSecondsToMmSs(swimSet.settings.swimTime);
    }
    if (swimSet.settings.numRounds) {
        document.getElementById('numRounds').value = swimSet.settings.numRounds;
    }

    // Load swimmer set
    setCurrentSwimmerSet(swimSet.swimmers);

    // Update lane selector to reflect current lane
    console.log('loadSwimSetForExecution: calling updateLaneSelector after setting current lane to:', currentSettings.currentLane);
    updateLaneSelector();
}

// Server-first start: request device to start the head (or specific index)
// Returns Promise<boolean>
// TODO Why is this so big, why not just call toggle?
async function runSwimSetNow(requestedIndex) {
    // TODO: Why doesn't this just call toggle?
    const lane = getCurrentLaneFromUI();
    const queue = swimSetQueues[lane] || [];

    // choose head or requested index (first non-completed)
    let headIndex = -1;
    if (typeof requestedIndex === 'number' && requestedIndex >= 0 && requestedIndex < queue.length && !queue[requestedIndex].completed) {
        headIndex = requestedIndex;
    } else {
        headIndex = queue.findIndex(s => !s.completed);
    }
    if (headIndex === -1) {
        console.warn('runSwimSetNow: no pending swim set to start');
        return false;
    }
    const head = queue[headIndex];

    head.startingPending = true;
    updateQueueDisplay();

    const payload = { lane };
    if (head.id && head.id !== 0) payload.matchId = Number(head.id);
    else if (head.clientTempId) payload.matchClientTempId = String(head.clientTempId);
    payload.set = { settings: head.settings || {}, swimmers: head.swimmers || [] };

    const controller = new AbortController();
    const to = setTimeout(() => controller.abort(), 7000);
    try {
        const resp = await fetch('/runSwimSetNow', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
            signal: controller.signal
        });
        clearTimeout(to);
        head.startingPending = false;
        updateQueueDisplay();

        if (!resp.ok) {
            const txt = await resp.text().catch(()=>null);
            console.warn('Device rejected start:', resp.status, txt);
            return false;
        }
        const json = await resp.json().catch(()=>null);
        if (json && json.startedId) head.id = json.startedId;
        if (json && json.clientTempId) head.clientTempId = String(json.clientTempId);

        return true;
    } catch (e) {
        clearTimeout(to);
        head.startingPending = false;
        updateQueueDisplay();
        console.warn('runSwimSetNow failed (no server confirmation)', e && e.message ? e.message : e);
        return false;
    }
}

// Server-first stop: ask device to stop, then clear local running state on confirmation
async function stopPacerExecution() {
    const lane = getCurrentLaneFromUI();
    // Ask server to stop this lane
    const controller = new AbortController();
    const to = setTimeout(() => controller.abort(), 5000);
    try {
        const resp = await fetch('/toggle', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ lane }),
            signal: controller.signal
        });
        clearTimeout(to);
        if (!resp.ok) {
            console.warn('Device stop request failed', resp.status);
            return false;
        }
        // Server confirmed stop — clear local running state
        laneRunning[lane] = false;
        updateQueueDisplay();
        updatePacerButtons();
        updateStatus();
        return true;
    } catch (e) {
        clearTimeout(to);
        console.warn('stopPacerExecution failed (no server confirmation)', e && e.message ? e.message : e);
        return false;
    }
}

function sendStopCommand() {
    fetch('/toggle', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body:  JSON.stringify({ lane: currentLane }),
    })
        .then(response => response.text())
        .then(result => {
            console.log('Stop command sent:', result);
        })
        .catch(error => {
            console.log('Running in standalone mode - stop command not sent');
        });
}

// Update the original createSet function to call the new workflow
function createSet() {
    // Redirect to new workflow
    createWorkSet();
}

function displaySwimmerSet() {
    const currentSet = getCurrentSwimmerSet();

    // Update set details in swim practice nomenclature
    const setDetails = document.getElementById('setDetails');
    const swimDistance = currentSettings.swimDistance;
    const swimTime = currentSet.length > 0 ? currentSet[0].swimTime : parseTimeInput(document.getElementById('swimTime').value);
    const restTime = currentSettings.restTime;
    const numRounds = currentSettings.numRounds;

    const avgSwimTimeDisplay = (swimTime > 60) ? formatSecondsToMmSs(swimTime) : `${Math.round(swimTime)}s`;
    setDetails.innerHTML = `${numRounds} x ${swimDistance}'s on the ${avgSwimTimeDisplay} with ${restTime} sec rest`;

    const swimmerList = document.getElementById('swimmerList');
    swimmerList.innerHTML = '';

    currentSet.forEach((swimmer, index) => {
        const row = document.createElement('div');
        row.className = 'swimmer-row';

        // Determine the actual color value for display
        let displayColor;
        if (swimmer.color.startsWith('#')) {
            // It's already a hex color
            displayColor = swimmer.color;
        } else {
            // It's a color name, look it up
            displayColor = colorHex[swimmer.color] || swimmer.color;
        }

        // Format swimTime value for display: use M:SS when > 60s, else plain seconds
        const swimTimeDisplay = (swimmer.swimTime > 60) ? formatSecondsToMmSs(swimmer.swimTime) : `${Math.round(swimmer.swimTime)}`;
        const swimTimeUnitLabel = (swimmer.swimTime > 60) ? 'min:sec' : 'sec';

       row.innerHTML = `
          <div class="swimmer-color" style="background-color: ${displayColor}"
              onclick="cycleSwimmerColor(${index})" title="Click to change color"></div>
          <div class="swimmer-info">Swimmer ${swimmer.id}</div>
          <div class="swimmer-swimTime"> <input type="text" class="swimmer-swimTime-input" value="${swimTimeDisplay}"
              placeholder="30 or 1:30" onchange="updateSwimmerSwimTime(${index}, this.value)"> <span class="swimTime-unit">${swimTimeUnitLabel}</span></div>
       `;

        swimmerList.appendChild(row);
    });
}

function cycleSwimmerColor(swimmerIndex) {
    const currentSet = getCurrentSwimmerSet();
    // Defensive programming: validate swimmer index
    if (swimmerIndex < 0 || swimmerIndex >= currentSet.length) {
        console.error(`Invalid swimmer index: ${swimmerIndex}`);
        return;
    }

    // Set the current swimmer being edited and open color picker
    currentSwimmerIndex = swimmerIndex;
    console.log(`Opening color picker for swimmer ${swimmerIndex} in ${currentSettings.laneNames[currentSettings.currentLane]}`);
    populateColorGrid();
    document.getElementById('customColorPicker').style.display = 'block';
}

function updateSwimmerSwimTime(swimmerIndex, newSwimTime) {
    const currentSet = getCurrentSwimmerSet();
    if (currentSet[swimmerIndex]) {
        // parseTimeInput supports both seconds and M:SS formats
        const parsed = parseTimeInput(String(newSwimTime));
        currentSet[swimmerIndex].swimTime = parsed;
        // Refresh display to update unit labels if needed
        updateSwimmerSetDisplay();
    }
}
// Use targeted send* helpers (e.g., sendPoolLength, sendBrightness) instead of broad writes
// so we avoid overwriting unrelated device state.

// Build the minimal swim set payload expected by the device
function buildMinimalSwimSetPayload(createdSet) {
    // createdSet: { id, lane, swimmers, settings, summary }
    const settings = createdSet.settings || {};
    const newSet = {
        rounds: Number(settings.numRounds || currentSettings.numRounds),
        swimDistance: Number(settings.swimDistance || currentSettings.swimDistance),
        swimSeconds: Number(settings.swimTime || currentSettings.swimTime),
        restSeconds: Number(settings.restTime || currentSettings.restTime),
        swimmerInterval: Number(settings.swimmerInterval || currentSettings.swimmerInterval),
        type: 0,
        repeat: 0
    };
    console.log('buildMinimalSwimSetPayload generated payload:', newSet);
    return newSet;
}

// Helper function to update all UI elements from settings
function updateAllUIFromSettings() {
    // Update input fields
    document.getElementById('numRounds').value = currentSettings.numRounds;
    document.getElementById('swimTime').value = currentSettings.swimTime;
    document.getElementById('restTime').value = currentSettings.restTime;
    document.getElementById('numSwimmers').value = currentSettings.numSwimmersPerLane[currentSettings.currentLane];
    document.getElementById('swimmerInterval').value = currentSettings.swimmerInterval;
    document.getElementById('swimDistance').value = currentSettings.swimDistance;
    document.getElementById('brightness').value = Math.round((currentSettings.brightness - 20) * 100 / (255 - 20));

    // Update underwater fields
    document.getElementById('firstUnderwaterDistance').value = currentSettings.firstUnderwaterDistance;
    document.getElementById('underwaterDistance').value = currentSettings.underwaterDistance;
    document.getElementById('hideAfter').value = currentSettings.hideAfter;

    // Update underwaters checkbox and labels WITHOUT triggering server updates
    const underChk = document.getElementById('underwatersEnabled');
    const underControls = document.getElementById('underwatersControls');
    const toggleOff = document.getElementById('toggleOff');
    const toggleOn = document.getElementById('toggleOn');
    if (underChk) underChk.checked = !!currentSettings.underwatersEnabled;
    if (underControls) underControls.style.display = currentSettings.underwatersEnabled ? 'block' : 'none';
    if (toggleOff && toggleOn) {
        if (currentSettings.underwatersEnabled) {
            toggleOff.classList.remove('active');
            toggleOn.classList.add('active');
        } else {
            toggleOff.classList.add('active');
            toggleOn.classList.remove('active');
        }
    }

    // Update radio button states
    if (currentSettings.colorMode === 'individual') {
        document.getElementById('individualColors').checked = true;
    } else {
        document.getElementById('sameColor').checked = true;
    }

    // Update display values (use UI-only setters to avoid POSTs)
    setNumRoundsUI(currentSettings.numRounds);
    setRestTimeUI(currentSettings.restTime);
    setNumSwimmersUI(currentSettings.numSwimmersPerLane[currentSettings.currentLane]);
    setSwimmerIntervalUI(currentSettings.swimmerInterval);
    setPoolLengthUI(currentSettings.poolLength, currentSettings.poolLengthUnits);
    setBrightnessUI(Math.round((currentSettings.brightness - 20) * 100 / (255 - 20)));
    setFirstUnderwaterDistanceUI(currentSettings.firstUnderwaterDistance);
    setUnderwaterDistanceUI(currentSettings.underwaterDistance);
    setHideAfterUI(currentSettings.hideAfter);

    // Update visual selections
    updateVisualSelection();

    // Update lane selector
    console.log('updateAllUIFromSettings: calling updateLaneSelector with current lane:', currentSettings.currentLane);
    updateLaneSelector();
}

// Initialize queue display on page load
function initializeQueueSystem() {
    // Ensure queue display is updated on initialization
    if (document.getElementById('queueList')) {
        updateQueueDisplay();
        updatePacerButtons();
    } else {
        // Retry after a short delay if elements aren't ready
        setTimeout(initializeQueueSystem, 100);
    }
}

// Reconcile local queue entries with device queue for the current lane.
// Matches items by summary and key numeric fields to detect items accepted by the device.
async function reconcileQueueWithDevice() {
    if (isStandaloneMode) return;
    const lane = currentSettings.currentLane;
    try {
        console.log('reconcileQueueWithDevice calling /getSwimQueue');
        const res = await fetch('/getSwimQueue', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ lane }),
        });
        if (!res.ok) return;
        console.log('reconcileQueueWithDevice received response:', res);
        const payload = await res.json();
        console.log('reconcileQueueWithDevice received payload:', payload);

        // Normalize payload
        const deviceQueue = Array.isArray(payload) ? payload : (Array.isArray(payload.queue) ? payload.queue : []);
        const deviceStatus = payload && payload.status ? payload.status : null;

        // Debug output show device queue and status
        console.log('Device swim queue for reconciliation:', deviceQueue);
        console.log('Device status for reconciliation:', deviceStatus);

        // Build quick lookup tables for device entries (restricted to our lane if device provides lane)
        const deviceById = new Map();
        const deviceByClient = new Map();
        const deviceBySignature = new Map(); // signature -> first matching device entry

        for (let i = 0; i < deviceQueue.length; i++) {
            const d = deviceQueue[i];
            // filter by lane if device provides lane
            if (d.lane !== undefined && Number(d.lane) !== Number(lane)) continue;
            if (d.id !== undefined && d.id !== null) deviceById.set(String(d.id), d);
            if (d.clientTempId !== undefined && d.clientTempId !== null && String(d.clientTempId) !== '0' && String(d.clientTempId) !== '') {
                deviceByClient.set(String(d.clientTempId), d);
            }
            // tolerant signature for best-effort match
            const swimSec = Number(d.swimSeconds ?? d.swimTime ?? 0).toFixed(0);
            const restSec = Number(d.restSeconds ?? d.restTime ?? 0).toFixed(0);
            const rounds = Number(d.rounds ?? d.numRounds ?? 0).toFixed(0);
            const dist = Number(d.swimDistance ?? d.distance ?? 0).toFixed(0);
            const sig = `${swimSec}|${restSec}|${rounds}|${dist}`;
            if (!deviceBySignature.has(sig)) deviceBySignature.set(sig, d);
        }

        // Ensure swimSetQueues[lane] exists
        swimSetQueues[lane] = swimSetQueues[lane] || [];

        // Try to reconcile local entries in-place using best-known matches
        for (let i = 0; i < swimSetQueues[lane].length; i++) {
            const local = swimSetQueues[lane][i];
            if (!local) continue;

            // Skip items we intentionally flagged as pending delete (do not re-sync them back)
            if (local.deletedPending) continue;

            // 1) Match by server id if present
            let matchedDevice = null;
            if (local.id !== undefined && local.id !== null && String(local.id) !== '0') {
                console.log('Attempting match by id for local swim set:', local);
                matchedDevice = deviceById.get(String(local.id));
                if (matchedDevice) {
                    console.log('Successfully matched by id:', matchedDevice);
                }
            }

            // 2) Match by clientTempId (stable client-generated identifier)
            if (!matchedDevice && local.clientTempId) {
                console.log('Attempting match by clientTempId for local swim set:', local);
                matchedDevice = deviceByClient.get(String(local.clientTempId));
                if (matchedDevice) {
                    console.log('Successfully matched by clientTempId:', matchedDevice);
                }
            }

            // 3) Match by tolerant signature (best-effort)
            if (!matchedDevice) {
                console.log('Attempting signature match for local swim set:', local);
                const sigLocal = `${Math.round(local.settings?.swimTime ?? local.swimSeconds ?? 0).toFixed(0)}|${Number(local.settings?.restTime ?? local.restSeconds ?? 0).toFixed(0)}|${Number(local.settings?.numRounds ?? local.rounds ?? 0).toFixed(0)}|${Number(local.settings?.swimDistance ?? local.swimDistance ?? 0).toFixed(0)}`;
                matchedDevice = deviceBySignature.get(sigLocal);
                if (matchedDevice) {
                    console.log('Successfully matched by signature:', matchedDevice);
                }
            }

            // 4) If still not matched, attempt tolerant field match (numbers equal or within small tolerance)
            if (!matchedDevice) {
                console.log('Attempting tolerant field match for local swim set:', local);
                for (const [k, d] of deviceById.entries()) {
                    try {
                        const dObj = (typeof d === 'object') ? d : null;
                        if (!dObj) continue;
                        const dSwim = Number(dObj.swimSeconds ?? dObj.swimTime ?? 0);
                        const lSwim = Number(local.settings?.swimTime ?? local.swimSeconds ?? 0);
                        const dRest = Number(dObj.restSeconds ?? dObj.restTime ?? 0);
                        const lRest = Number(local.settings?.restTime ?? local.restSeconds ?? 0);
                        const dRounds = Number(dObj.rounds ?? dObj.numRounds ?? 0);
                        const lRounds = Number(local.settings?.numRounds ?? local.rounds ?? 0);
                        const dDist = Number(dObj.swimDistance ?? dObj.distance ?? 0);
                        const lDist = Number(local.settings?.swimDistance ?? local.swimDistance ?? 0);
                        if (Math.abs(dSwim - lSwim) <= 1 && Math.abs(dRest - lRest) <= 1 && dRounds === lRounds && Math.abs(dDist - lDist) <= 1) {
                            matchedDevice = dObj;
                            break;
                        }
                    } catch (e) { continue; }
                }
                if (matchedDevice) {
                    console.log('Successfully matched by tolerant field comparison:', matchedDevice);
                }
            }

            // Merge authoritative fields from device when matched
            if (matchedDevice) {
                console.log('Merging fields from matched device:', matchedDevice);
                local.synced = true;
                if (matchedDevice.id !== undefined) local.id = matchedDevice.id;
                if (matchedDevice.clientTempId !== undefined) local.clientTempId = String(matchedDevice.clientTempId);
                if (matchedDevice.completed !== undefined) local.completed = !!matchedDevice.completed;
                // Accept server-side status if present (could be numeric bitmask or string)
                if (matchedDevice.status !== undefined) local.status = matchedDevice.status;
                // Merge runtime progress fields if provided
                if (matchedDevice.currentRound !== undefined) local.currentRound = Number(matchedDevice.currentRound);
                if (matchedDevice.startedAt) local.startedAt = matchedDevice.startedAt;
                if (matchedDevice.completedAt) local.completedAt = matchedDevice.completedAt;

                //debug output
                console.log('Reconciled local swim set with device entry:', local, matchedDevice);
            } else {
                console.log('No matching device found for local swim set:', local);
                // Not matched: keep local optimistic flags and leave unsynced
                local.synced = !!local.synced;
            }
        }

        // Persist latest deviceStatus snapshot and timestamp for UI preference
        if (deviceStatus && typeof deviceStatus === 'object') {
            try {
                // laneRunning block
                if (deviceStatus.laneRunning !== undefined) {
                    if (Array.isArray(deviceStatus.laneRunning)) {
                        for (let li = 0; li < deviceStatus.laneRunning.length && li < laneRunning.length; li++) {
                            laneRunning[li] = !!deviceStatus.laneRunning[li];
                        }
                    } else {
                        laneRunning[lane] = !!deviceStatus.laneRunning;
                    }
                    if (!laneRunning[lane]) {
                        // Call main stop handler to clear local state
                        stopPacerExecution().catch(()=>{});
                    } else {
                        // assume it was started, if we force start it, not sure if we will
                        // overwrite any local state incorrectly
                    }
                }
            } catch (e) {
                console.warn('Failed to apply device status block:', e);
            }
        }

        // Reflect changes in the UI
        updateQueueDisplay();
        updateStatus();
        updatePacerButtons();
    } catch (e) {
        // ignore network errors but log for debugging
        console.warn('reconcileQueueWithDevice error:', e);
    } finally {
        // Retry pending deletes/updates (best-effort)
        retryPendingDeletes().catch(()=>{});
        retryPendingUpdates().catch(()=>{});
    }
}

// Periodic reconciliation timer id
let reconcileIntervalId = null;

// Add function to refresh swimmer set display
function updateSwimmerSetDisplay() {
    const swimmerSetDiv = document.getElementById('swimmerSet');
    if (swimmerSetDiv && swimmerSetDiv.style.display === 'block') {
        displaySwimmerSet();
    }
}

// Initialize queue system after DOM is ready
document.addEventListener('DOMContentLoaded', async function() {
    console.log('******** DOM fully loaded **********');

    // Device-first: fetch device settings and apply them before doing queue reconciliation or UI init
    try {
        await fetchDeviceSettingsAndApply();
        console.log('Device settings applied before UI init');
    } catch (e) {
        console.warn('fetchDeviceSettingsAndApply failed, proceeding with local defaults', e);
    }

    // Reconcile queue once now (using device-applied settings) and start periodic reconciliation
    if (!isStandaloneMode) {
        try {
            await reconcileQueueWithDevice();
            console.log('Initial queue reconciliation complete');
        } catch (e) {
            console.warn('Initial reconcileQueueWithDevice failed', e);
        }
        reconcileIntervalId = setInterval(reconcileQueueWithDevice, 10000);
    }

    // Some browsers restore form control values after load (preserve user inputs).
    // To ensure labels stay in sync with actual slider values (which may be restored
    // by the browser after scripts run), schedule a short re-sync.
    setTimeout(syncRangeLabels, 60);

    // Initialize rest of the UI now that device values are applied and queue reconciled
    updateCalculations(false);
    initializeBrightnessDisplay();
    console.log('DOM fully loaded: calling updateLaneSelector with current lane:', currentSettings.currentLane);
    updateLaneSelector();
    updateLaneNamesSection();
    updateStatus();

    // Ensure visual selection is applied after DOM is ready
    updateVisualSelection();
    initializeQueueSystem();

    // Initialize the create/edit tabs and restore last-used tab
    try {
        const last = localStorage.getItem('createTab') || 'swimmers';
        setCreateTab(last, true);
    } catch (e) {
        // ignore if localStorage unavailable
        setCreateTab('swimmers', true);
    }

    // Attach start/stop handlers that call server-first functions directly
    try {
        const startBtn = document.getElementById('startBtn');
        if (startBtn) {
            startBtn.addEventListener('click', () => {
                // runSwimSetNow returns a Promise<boolean>
                runSwimSetNow().then(ok => {
                    if (!ok) console.warn('Device did not confirm start');
                }).catch(err => console.error('run start error', err));
            });
        }

        const stopBtn = document.getElementById('stopBtn');
        if (stopBtn) {
            stopBtn.addEventListener('click', () => {
                stopPacerExecution().then(ok => {
                    if (!ok) console.warn('Device did not confirm stop');
                }).catch(err => console.error('stop error', err));
            });
        }
    } catch (e) {
        console.warn('Failed to attach start/stop listeners', e);
    }
});

// When navigating back/forward some browsers restore form values. Use pageshow
// to ensure labels reflect any restored control values.
window.addEventListener('pageshow', function() {
    setTimeout(syncRangeLabels, 20);
});

// Sync labels for range/number controls by calling the existing update handlers.
function syncRangeLabels() {
    try {
        // Use UI-only setters so we don't POST during restore
        setRestTimeUI(document.getElementById('restTime') ? document.getElementById('restTime').value : currentSettings.restTime);
        setSwimmerIntervalUI(document.getElementById('swimmerInterval') ? document.getElementById('swimmerInterval').value : currentSettings.swimmerInterval);
        setPulseWidthUI(document.getElementById('pulseWidth') ? document.getElementById('pulseWidth').value : currentSettings.pulseWidth);
        setFirstUnderwaterDistanceUI(document.getElementById('firstUnderwaterDistance') ? document.getElementById('firstUnderwaterDistance').value : currentSettings.firstUnderwaterDistance);
        setUnderwaterDistanceUI(document.getElementById('underwaterDistance') ? document.getElementById('underwaterDistance').value : currentSettings.underwaterDistance);
        setHideAfterUI(document.getElementById('hideAfter') ? document.getElementById('hideAfter').value : currentSettings.hideAfter);
        setLightSizeUI(document.getElementById('lightSize') ? document.getElementById('lightSize').value : currentSettings.lightSize);
        setBrightnessUI(document.getElementById('brightness') ? document.getElementById('brightness').value : Math.round((currentSettings.brightness - 20) * 100 / (255 - 20)));
    } catch (e) {
        // Non-fatal - if elements not present just ignore
        //console.log('syncRangeLabels failed', e);
    }
}

// Generic collapsible section toggler used by HTML headers.
function toggleSection(areaId, toggleBtnId) {
    const area = document.getElementById(areaId);
    const btn = document.getElementById(toggleBtnId);
    if (!area) return;
    const isHidden = area.style.display === 'none';
    area.style.display = isHidden ? 'block' : 'none';
    if (btn) {
        btn.style.transform = isHidden ? 'rotate(180deg)' : 'rotate(0deg)';
    }
}

// Create/Edit card tabbing
function setCreateTab(tabName, silent) {
    const detailsTab = document.getElementById('createDetailsTab');
    const swimmersTab = document.getElementById('createSwimmersTab');

    if (tabName === 'details') {
        detailsTab.classList.add('active');
        swimmersTab.classList.remove('active');
        document.getElementById('configControls').style.display = 'block';
        document.getElementById('swimmerSet').style.display = 'none';
        // Ensure Queue button visible
        const queueBtn = document.getElementById('queueBtn'); if (queueBtn) queueBtn.style.display = 'inline-block';
    } else {
        detailsTab.classList.remove('active');
        swimmersTab.classList.add('active');
        document.getElementById('configControls').style.display = 'none';
        document.getElementById('swimmerSet').style.display = 'block';
        const queueBtn = document.getElementById('queueBtn'); if (queueBtn) queueBtn.style.display = 'inline-block';

        // Only create/update the swimmer set if we don't already have one
        // or if the current settings differ from the created set's settings.
        try {
            const lane = currentSettings.currentLane || 0;
            const created = createdSwimSets[lane];
            let needCreate = false;
            if (!created || !created.settings) {
                needCreate = true;
            } else {
                const createdNumSwimmers = (created.swimmers && created.swimmers.length) || (created.settings.numSwimmers || currentSettings.numSwimmersPerLane[lane]);
                const createdSig = `${created.settings.swimDistance}|${created.settings.swimTime}|${created.settings.restTime}|${created.settings.numRounds}|${created.settings.swimmerInterval}|${createdNumSwimmers}`;
                const curSig = `${currentSettings.swimDistance}|${currentSettings.swimTime}|${currentSettings.restTime}|${currentSettings.numRounds}|${currentSettings.swimmerInterval}|${currentSettings.numSwimmersPerLane[lane]}`;
                if (createdSig !== curSig) needCreate = true;
            }

            if (needCreate) {
                console.log('We are switching to the swimmer tab, - Creating or update from config');
                createOrUpdateSwimmerSetFromConfig(false);
            } else {
                console.log('Swimmers tab switch: existing created set is up-to-date, skipping rebuild');
                // Ensure the existing created set is rendered into the swimmer UI
                try {
                    if (created && created.swimmers) {
                        // Populate the editable swimmer set and update the display
                        setCurrentSwimmerSet(JSON.parse(JSON.stringify(created.swimmers)));
                        // Ensure forms/values reflect the created set's settings
                        Object.assign(currentSettings, JSON.parse(JSON.stringify(created.settings || {})));
                        updateAllUIFromSettings();
                        updateSwimmerSetDisplay();
                    } else {
                        // If no created set present, still ensure current UI shows any existing swimmerSet
                        updateSwimmerSetDisplay();
                    }
                } catch (e) {
                    console.warn('Failed to render existing created set on tab switch:', e);
                }
            }
        } catch (e) {
            console.log('Error checking created set on tab switch, falling back to create:', e);
            try { createOrUpdateSwimmerSetFromConfig(false); } catch (e2) {}
        }
    }

    try {
        if (!silent) localStorage.setItem('createTab', tabName);
    } catch (e) {}
}

// When the swim time input changes, reset per-swimmer swim times
// so that swimmer customizations are ignored and all swimmers use the new swim time.
document.addEventListener('DOMContentLoaded', function() {
    const swimTimeEl = document.getElementById('swimTime');
    if (swimTimeEl) {
        swimTimeEl.addEventListener('change', function() {
            // Rebuild the swimmer set and reset swimmer swim times to the new value
            createOrUpdateSwimmerSetFromConfig(true);
        });
    }

    // Ensure switching to swimmers tab creates/updates the swimmer set automatically
    const detailsTabBtn = document.getElementById('createDetailsTab');
    const swimmersTabBtn = document.getElementById('createSwimmersTab');
    if (swimmersTabBtn) {
        swimmersTabBtn.addEventListener('click', function() {
            // Auto-create/update the set from current controls without resetting swim times
            // TODO: We were ensuring swim times were properly set when changing tabs
            // but it doesn't seem to be necessary.
            console.log('Switching to swimmers tab - Skipping updating swimmer set from config');
            //createOrUpdateSwimmerSetFromConfig(false);
        });
    }
    if (detailsTabBtn) {
        detailsTabBtn.addEventListener('click', function() {
            // No special action needed when going back to details
        });
    }
});

// Convert bytes to hex color string
function rgbBytesToHex(r, g, b) {
    const toHex = (v) => v.toString(16).padStart(2, '0');
    return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
}

async function fetchDeviceSettingsAndApply() {
    if (isStandaloneMode) return;

    // Suppress outbound writes while we apply device-provided defaults
    suppressSettingsWrites = true;

    try {
        console.log("Fetching globalConfigSettings");
        const res = await fetch('/globalConfigSettings');
        if (!res.ok) return;
        const dev = await res.json();
        console.log('fetchDeviceSettingsAndApply: device payload:', dev);

        if (dev.numLanes !== undefined) {
            console.log('Device reports numLanes=', dev.numLanes, ' (type:', typeof dev.numLanes, ')');
        } else {
            console.log('Device did not report numLanes');
        }
        console.log("Received globalConfigSettings, underwaters:" + dev.underwatersEnabled);
        console.log('DEBUG: device reports numSwimmersPerLane =', dev.numSwimmersPerLane);

        // Merge device settings into currentSettings with conversions
        if (dev.stripLengthMeters !== undefined) currentSettings.stripLength = parseFloat(dev.stripLengthMeters);
        if (dev.ledsPerMeter !== undefined) currentSettings.ledsPerMeter = parseInt(dev.ledsPerMeter);
        if (dev.numLanes !== undefined) currentSettings.numLanes = parseInt(dev.numLanes);
        if (dev.numSwimmersPerLane !== undefined && Array.isArray(dev.numSwimmersPerLane)) {

            // Prefer per-lane counts
            try {
                for (let li = 0; li < dev.numSwimmersPerLane.length && li < 4; li++) {
                    const n = parseInt(dev.numSwimmersPerLane[li]);
                    if (!isNaN(n) && n >= 0 && n <= currentSettings.maxSwimmers) {
                        // Apply per-lane value by resizing swimmerSets for each lane
                        migrateSwimmerCountsToDeviceForLane(li, n);
                    }
                }
            } catch (e) {
                // fallback to global value
            }
        }
        if (dev.poolLength !== undefined) currentSettings.poolLength = dev.poolLength;
        if (dev.poolLengthUnits !== undefined) currentSettings.poolLengthUnits = dev.poolLengthUnits;

        // Convert brightness 0-255 to percent (0-100)
        if (dev.brightness !== undefined) {
            const b = parseInt(dev.brightness);
            const percent = Math.round(((b - 20) * 100) / (255 - 20));
            // Clamp
            const clamped = Math.max(0, Math.min(100, percent));
            // Set both internal brightness (0-255) and UI control
            currentSettings.brightness = b;
            document.getElementById('brightness').value = clamped;
            document.getElementById('brightnessValue').textContent = clamped + '%';
        }

        // Colors as bytes -> hex
        if (dev.colorRed !== undefined && dev.colorGreen !== undefined && dev.colorBlue !== undefined) {
            currentSettings.swimmerColor = rgbBytesToHex(Number(dev.colorRed), Number(dev.colorGreen), Number(dev.colorBlue));
            document.getElementById('colorIndicator').style.backgroundColor = currentSettings.swimmerColor;
        }

        // Underwaters enabled + colors
        if (dev.underwatersEnabled !== undefined) {
            console.log("Applying underwatersEnabled from device:", dev.underwatersEnabled);
            // Use the dedicated function so DOM and labels are updated consistently
            updateUnderwatersEnabled(false, (dev.underwatersEnabled === 'true' || dev.underwatersEnabled === true));
        }
        if (dev.underwaterColor !== undefined) {
            currentSettings.underwaterColor = dev.underwaterColor;
            const el = document.getElementById('underwaterColorIndicator');
            if (el) el.style.backgroundColor = currentSettings.underwaterColor;
        }
        if (dev.surfaceColor !== undefined) {
            currentSettings.surfaceColor = dev.surfaceColor;
            const el = document.getElementById('surfaceColorIndicator');
            if (el) el.style.backgroundColor = currentSettings.surfaceColor;
        }

        // Ensure device colors appear in the shared palette
        try {
            // Populate the grid if not already
            populateColorGrid();

            if (currentSettings.underwaterColor) {
                ensureColorInPalette(currentSettings.underwaterColor);
            }
            if (currentSettings.surfaceColor) {
                ensureColorInPalette(currentSettings.surfaceColor);
            }
            if (currentSettings.swimmerColor) {
                ensureColorInPalette(currentSettings.swimmerColor);
            }

            // Mark selected swatches visually
            const colorGrid = document.getElementById('colorGrid');
            if (colorGrid) {
                for (let i = 0; i < colorGrid.children.length; i++) {
                    const child = colorGrid.children[i];
                    const c = (child.getAttribute('data-color') || '').toLowerCase();
                    child.classList.remove('selected');
                    // simple marking: if matches any active color, add selected
                    if (c && (c === (currentSettings.underwaterColor || '').toLowerCase() || c === (currentSettings.surfaceColor || '').toLowerCase() || c === (currentSettings.swimmerColor || '').toLowerCase())) {
                        child.classList.add('selected');
                    }
                }
            }
        } catch (e) {
            // Non-fatal
            console.log('Palette sync skipped:', e);
        }

        // After merging, update full UI
        updateAllUIFromSettings();
        // Re-enable outbound writes now that device defaults are applied
        suppressSettingsWrites = false;
    } catch (e) {
        console.log('Device settings fetch failed:', e);
        // Ensure we don't leave suppression enabled on error
        suppressSettingsWrites = false;
    }
}

// -----------------------------
// Targeted server update helpers
// Each helper only updates one aspect of device state to limit POSTs
// -----------------------------

// Default timeout for individual setting posts (ms)
const SEND_TIMEOUT_MS = 800;

// Generic form POST with timeout support. Returns a Promise that resolves/rejects
// like fetch. If writes are suppressed or we're in standalone mode, returns a
// resolved Promise immediately so callers can uniformly await it.
function postForm(path, body, timeoutMs = SEND_TIMEOUT_MS) {
    if (isStandaloneMode || suppressSettingsWrites) return Promise.resolve({ suppressed: true });

    const controller = new AbortController();
    const signal = controller.signal;
    const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

    return fetch(path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body,
        signal
    }).finally(() => clearTimeout(timeoutId));
}

function sendPulseWidth(pulseWidth) {
    return postForm('/setPulseWidth', `pulseWidth=${encodeURIComponent(pulseWidth)}`);
}

function sendStripLength(stripLength) {
    return postForm('/setStripLength', `stripLength=${encodeURIComponent(stripLength)}`);
}

function sendPoolLength(poolLength, poolUnits) {
    return postForm('/setPoolLength', `poolLength=${encodeURIComponent(poolLength)}&poolLengthUnits=${encodeURIComponent(poolUnits)}`);
}

function sendLedsPerMeter(ledsPerMeter) {
    return postForm('/setLedsPerMeter', `ledsPerMeter=${encodeURIComponent(ledsPerMeter)}`);
}

function sendNumLanes(numLanes) {
    return postForm('/setNumLanes', `numLanes=${encodeURIComponent(numLanes)}`);
}

function sendDelayIndicators(enabled) {
    return postForm('/setDelayIndicators', `enabled=${enabled ? 'true' : 'false'}`);
}

function sendNumSwimmers(lane, numSwimmers) {
    let body = `numSwimmers=${encodeURIComponent(numSwimmers)}&lane=${encodeURIComponent(lane)}`;
    return postForm('/setNumSwimmers', body);
}

function sendColorMode(mode) {
    return postForm('/setColorMode', `colorMode=${encodeURIComponent(mode)}`);
}

function sendSwimmerColor(hex) {
    return postForm('/setSwimmerColor', `color=${encodeURIComponent(hex)}`);
}

function sendSwimmerColors(csvHex) {
    return postForm('/setSwimmerColors', `colors=${encodeURIComponent(csvHex)}`);
}

function sendUnderwaterSettings() {
    const u = currentSettings;
    const body = `enabled=${u.underwatersEnabled ? 'true' : 'false'}&firstUnderwaterDistance=${encodeURIComponent(u.firstUnderwaterDistance)}&underwaterDistance=${encodeURIComponent(u.underwaterDistance)}&surfaceDistance=${encodeURIComponent(u.surfaceDistance)}&hideAfter=${encodeURIComponent(u.hideAfter)}&lightSize=${encodeURIComponent(u.lightSize)}&underwaterColor=${encodeURIComponent(u.underwaterColor)}&surfaceColor=${encodeURIComponent(u.surfaceColor)}`;
    return postForm('/setUnderwaterSettings', body);
}

function sendBrightness(percent) {
    // convert percent to internal 0-255 if caller sent percent
    const internal = Math.round(20 + (percent / 100) * (255 - 20));
    return postForm('/setBrightness', `brightness=${encodeURIComponent(internal)}`);
}

function sendSwimDistance(swimDistance) {
    return postForm('/setSwimDistance', `swimDistance=${encodeURIComponent(Number(swimDistance))}`);
}

function sendNumRounds(numRounds) {
    return postForm('/setNumRounds', `numRounds=${encodeURIComponent(Number(numRounds))}`);
}

function sendSwimTime(swimSeconds) {
    return postForm('/setSwimTime', `swimTime=${encodeURIComponent(Number(parseTimeInput(swimSeconds)))}`);
}

function sendRestTime(restSeconds) {
    return postForm('/setRestTime', `restTime=${encodeURIComponent(Number(restSeconds))}`);
}

function sendSwimmerInterval(intervalSeconds) {
    return postForm('/setSwimmerInterval', `swimmerInterval=${encodeURIComponent(Number(intervalSeconds))}`);
}

// Replace the old timeout-based start with a promise-based sequence. We will
// wait for a short period for the individual setting posts to settle, but not
// indefinitely. Use Promise.allSettled and race it with a timeout fallback.
async function sendStartCommand() {
    // Collect promises from each targeted send helper
    const promises = [];

    // Geometry and LED mapping
    promises.push(sendPoolLength(currentSettings.poolLength, currentSettings.poolLengthUnits));
    promises.push(sendStripLength(currentSettings.stripLength));
    promises.push(sendLedsPerMeter(currentSettings.ledsPerMeter));

    // Visual settings
    try {
        const brightnessPercent = Math.round((currentSettings.brightness - 20) * 100 / (255 - 20));
        promises.push(sendBrightness(brightnessPercent));
    } catch (e) {
        // ignore if conversion fails
    }
    promises.push(sendPulseWidth(currentSettings.pulseWidth));

    // Lane and swimmer counts
    promises.push(sendNumLanes(currentSettings.numLanes));
    promises.push(sendNumSwimmers(currentSettings.currentLane, currentSettings.numSwimmersPerLane[currentSettings.currentLane]));
    promises.push(sendNumRounds(currentSettings.numRounds));

    // Swim set-related
    promises.push(sendSwimDistance(currentSettings.swimDistance));
    promises.push(sendSwimTime(currentSettings.swimTime));
    promises.push(sendRestTime(currentSettings.restTime));
    promises.push(sendSwimmerInterval(currentSettings.swimmerInterval));

    // Indicators and underwater config
    promises.push(sendDelayIndicators(currentSettings.delayIndicatorsEnabled));
    promises.push(sendUnderwaterSettings());

    // Color configuration
    if (currentSettings.colorMode === 'individual') {
        const set = getCurrentSwimmerSet();
        const colors = [];
        for (let i = 0; i < currentSettings.numSwimmersPerLane[currentSettings.currentLane]; i++) {
            if (set && set[i] && set[i].color) colors.push(set[i].color);
            else colors.push(currentSettings.swimmerColor);
        }
        promises.push(sendSwimmerColors(colors.join(',')));
    } else {
        promises.push(sendSwimmerColor(currentSettings.swimmerColor));
    }

    // Wait for all posts to settle, but cap wait time by racing with a timeout.
    const waitMs = 1000; // maximum time to wait for all sends
    const allSettledPromise = Promise.allSettled(promises.map(p => p || Promise.resolve()));
    await Promise.race([
        allSettledPromise,
        new Promise(resolve => setTimeout(resolve, waitMs))
    ]);

    // Finally, request the device to toggle/start. Do not block on this.
    fetch('/toggle', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body:  JSON.stringify({ lane: currentLane }),
    })
        .then(response => response.text())
        .then(result => {
            console.log('Start command issued, server response:', result);
        })
        .catch(err => {
            console.log('Start command failed (standalone mode)');
        });
}
