// Detect if running in standalone mode (file:// protocol)
const isStandaloneMode = window.location.protocol === 'file:';


let currentSettings = {
    color: 'red',
    brightness: 196,
    pulseWidth: 1.0,
    restTime: 5,
    swimTime: 30,
    swimDistance: 50,
    swimmerInterval: 4,
    delayIndicatorsEnabled: true,
    maxSwimmersPerLane: 10,
    numSwimmersPerLane: [3,3,3,3],
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
    isRunning: false,
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
let runningSets = [null, null, null, null]; // Immutable copies of sets when pacer starts
let runningSettings = [null, null, null, null]; // Settings snapshot when pacer starts
let currentSwimmerIndex = -1; // Track which swimmer is being edited

// Swim Set Queue Management - now lane-specific
let swimSetQueues = [[], [], [], []]; // Array of queued swim sets for each lane
let activeSwimSets = [null, null, null, null]; // Currently running swim set for each lane
let activeSwimSetIndex = [-1, -1, -1, -1]; // Index of the active swim set within swimSetQueues for each lane
let editingSwimSetIndexes = [-1, -1, -1, -1]; // Index of swim set being edited for each lane (-1 if creating new)
let createdSwimSets = [null, null, null, null]; // Temporarily holds created set before queuing for each lane
const swimmerColors = ['red', 'green', 'blue', 'yellow', 'purple', 'cyan'];
const colorHex = {
    'red': '#ff0000',
    'green': '#00ff00',
    'blue': '#0000ff',
    'yellow': '#ffff00',
    'purple': '#800080',
    'cyan': '#00ffff',
    'custom': '#0000ff' // Default custom color
};

// Lane identification system
let laneIdentificationMode = false;
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

    // Update isRunning to reflect the new lane's state
    currentSettings.isRunning = laneRunning[newLane];

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

    // If the new lane is running, start status updates
    if (currentSettings.isRunning) {
        initializePacerStatus();
        startStatusUpdates();
    } else {
        stopStatusUpdates();
    }

    // Enable/disable reset positions button depending on running state
    try {
        const btn = document.getElementById('resetPositionsBtn');
        if (btn) btn.disabled = !!currentSettings.isRunning;
    } catch (e) {}
}

// Reset swimmers positions for the current lane back to the starting wall
function resetLanePositions() {
    const lane = currentSettings.currentLane || 0;
    // Only allow when not running
    if (currentSettings.isRunning) {
        alert('Cannot reset positions while lane is running');
        return;
    }

    // Optimistic UI: disable button while request in flight
    const btn = document.getElementById('resetPositionsBtn');
    if (btn) btn.disabled = true;

    fetch('/resetLane', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `lane=${lane}`
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
function getRunningSwimmerSet() {
    const currentLane = currentSettings.currentLane;
    return runningSets[currentLane] || getCurrentSwimmerSet();
}

function getRunningSettings() {
    const currentLane = currentSettings.currentLane;
    return runningSettings[currentLane] || currentSettings;
}

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
    sendSwimTime(currentSettings.swimTime);

    // Rebuild swimmer set so UI reflects the new rest time immediately
    // TODO: Why are we doing this?
    console.log('Swim time updated - Skipping updating swimmer set from config');
    //try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
}

function updateRestTime() {
    const restTime = document.getElementById('restTime').value;
    currentSettings.restTime = parseInt(restTime);
    const unit = parseInt(restTime) === 1 ? ' second' : ' seconds';
    const rlabel = document.getElementById('restTimeValue');
    if (rlabel) rlabel.textContent = restTime + unit;
    // Also send rest time to device so it can be used as a default for swim-sets
    sendRestTime(currentSettings.restTime);
    // Rebuild swimmer set so UI reflects the new rest time immediately
    console.log('Rest time updated - Skipping updating swimmer set from config');
    //try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
}



function updateSwimmerInterval() {
    const swimmerInterval = document.getElementById('swimmerInterval').value;
    currentSettings.swimmerInterval = parseInt(swimmerInterval);
    const unit = parseInt(swimmerInterval) === 1 ? ' second' : ' seconds';
    const silabel = document.getElementById('swimmerIntervalValue');
    if (silabel) silabel.textContent = swimmerInterval + unit;
    // Also send swimmer interval to device so it can be used as a default for swim-sets
    sendSwimmerInterval(currentSettings.swimmerInterval);
    // Rebuild swimmer set so UI reflects the new swimmer interval immediately
    console.log('Swimmer interval updated - Skipping updating swimmer set from config');
    //try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
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
    sendNumRounds(currentSettings.numRounds);
    // Rebuild current swimmer set so UI and created set reflect the new rounds immediately
    console.log('Num rounds updated - Skipping updating swimmer set from config');
    //try { createOrUpdateSwimmerSetFromConfig(false); } catch (e) {}
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

// Pacer status tracking variables - now lane-specific
let pacerStartTimes = [0, 0, 0, 0]; // Start time for each lane
let currentRounds = [1, 1, 1, 1]; // Current round for each lane
let completionHandled = [false, false, false, false]; // Track if completion has been handled for each lane
let statusUpdateInterval = null;
let currentColorContext = null; // Track which color picker context we're in
let _draggedQueueIndex = -1; // index of item being dragged within current lane
// When true, broad settings writes are suppressed to avoid overwriting device defaults
let suppressSettingsWrites = false;

function updateStatus() {
    const queueDisplay = document.getElementById('queueDisplay');
    const queueTitle = queueDisplay.querySelector('h4');
    const toggleBtnElement = document.getElementById('toggleBtn');

    if (queueDisplay) {
        // Prefer the per-lane running state so the border reflects the lane's actual status.
        const lane = currentSettings.currentLane || 0;
        const isLaneRunning = (laneRunning && laneRunning[lane]) ? true : false;
        const running = isLaneRunning || currentSettings.isRunning;
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
        toggleBtnElement.textContent = currentSettings.isRunning ? 'Stop Pacer' : 'Start Pacer';
    }
}

function togglePacer() {
    // Toggle the running state for the current lane
    const currentLane = currentSettings.currentLane;
    laneRunning[currentLane] = !laneRunning[currentLane];
    currentSettings.isRunning = laneRunning[currentLane];

    // Update UI immediately for better responsiveness
    updateStatus();

    // Handle detailed status display
    const detailedStatus = document.getElementById('detailedStatus');
    if (currentSettings.isRunning) {
        // Starting pacer for current lane - create immutable copies
        pacerStartTimes[currentLane] = Date.now();
        currentRounds[currentLane] = 1;

        // Create immutable copies of current set and settings
        runningSets[currentLane] = JSON.parse(JSON.stringify(getCurrentSwimmerSet()));
        runningSettings[currentLane] = {
            swimDistance: currentSettings.swimDistance,
            swimTime: currentSettings.swimTime,
            restTime: currentSettings.restTime,
            numRounds: currentSettings.numRounds,
            swimmerInterval: currentSettings.swimmerInterval,
            numSwimmers: currentSettings.numSwimmersPerLane[currentLane],
            laneName: currentSettings.laneNames[currentLane]
        };

        initializePacerStatus();
        startStatusUpdates();
    } else {
        // Stopping pacer for current lane - clear running copies
        runningSets[currentLane] = null;
        runningSettings[currentLane] = null;
        stopStatusUpdates();
    }

    // Try to notify server (will fail gracefully in standalone mode)
    fetch('/toggle', { method: 'POST' })
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

function initializePacerStatus() {
    const currentLane = currentSettings.currentLane;
    const runningData = getRunningSettings();

    // Safely update elements (check if they exist first)
    const currentRoundEl = document.getElementById('currentRound');
    if (currentRoundEl) {
        currentRoundEl.textContent = currentRounds[currentLane];
    }

    const totalRoundsEl = document.getElementById('totalRounds');
    if (totalRoundsEl) {
        totalRoundsEl.textContent = runningData.numRounds;
    }

    // Initialize round timing display using running settings
    const swimSeconds = runningData.swimTime;
    const restSeconds = runningData.restTime;
    const totalRoundTime = swimSeconds + restSeconds;

    // Compute total set duration (when the last swimmer finishes all rounds)
    const lastSwimmerStartDelay = (runningData.swimmerInterval || 0) + ((runningData.numSwimmers - 1) * (runningData.swimmerInterval || 0));
    const totalSetTimePerSwimmer = runningData.numRounds * totalRoundTime;
    const totalSetDuration = lastSwimmerStartDelay + totalSetTimePerSwimmer; // seconds
    const totalSetMinutes = Math.floor(totalSetDuration / 60);
    const totalSetSecondsOnly = Math.floor(totalSetDuration % 60);
    const totalSetStr = `${totalSetMinutes}:${totalSetSecondsOnly.toString().padStart(2, '0')}`;
    const setTotalEl = document.getElementById('setTotalTiming');
    if (setTotalEl) setTotalEl.textContent = `0:00 / ${totalSetStr}`;

    // Active swimmers/next event/current phase UI removed — skip updating them

    // Update set basics display using running settings
    const swimDistance = runningData.swimDistance;
    const numRounds = runningData.numRounds;
    const setBasicsEl = document.getElementById('setBasics');
    if (setBasicsEl) {
        setBasicsEl.textContent = `- ${numRounds} x ${swimDistance}'s`;
    }

    // Show initial delay countdown using running settings
    // Use swimmerInterval as the initial delay for the first swimmer
    const initialDelayDisplay = runningData.swimmerInterval || 0;
    // nextEvent, currentPhase, and elapsedTime elements were removed from the UI.
    // We intentionally do not update them here.
}

function startStatusUpdates() {
    statusUpdateInterval = setInterval(updatePacerStatus, 1000); // Update every second
}

function stopStatusUpdates() {
    if (statusUpdateInterval) {
        clearInterval(statusUpdateInterval);
        statusUpdateInterval = null;
    }
}

function updatePacerStatus() {
    if (!currentSettings.isRunning) return;

    const currentLane = currentSettings.currentLane;
    const runningData = getRunningSettings();
    const elapsedSeconds = Math.floor((Date.now() - pacerStartTimes[currentLane]) / 1000);

    // Account for initial delay using running settings
    const initialDelaySeconds = runningData.swimmerInterval || 0;
    const timeAfterInitialDelay = elapsedSeconds - initialDelaySeconds;

    // Check if we're still in the initial delay period
    if (timeAfterInitialDelay < 0) {
        // Still in initial delay phase — update round timing and round number only
        const swimSeconds = runningData.swimTime;
        const restSeconds = runningData.restTime;

        // Compute totalSetStr locally and update set total timing to show elapsed 0
        const totalRoundTime_loc = swimSeconds + restSeconds;
        const lastSwimmerStartDelay_loc = (runningData.swimmerInterval || 0) + ((runningData.numSwimmers - 1) * (runningData.swimmerInterval || 0));
        const totalSetTimePerSwimmer_loc = runningData.numRounds * totalRoundTime_loc;
        const totalSetDuration_loc = lastSwimmerStartDelay_loc + totalSetTimePerSwimmer_loc;
        const totalSetMinutes_loc = Math.floor(totalSetDuration_loc / 60);
        const totalSetSecondsOnly_loc = Math.floor(totalSetDuration_loc % 60);
        const totalSetStr_loc = `${totalSetMinutes_loc}:${totalSetSecondsOnly_loc.toString().padStart(2, '0')}`;
        const setTotalEl2 = document.getElementById('setTotalTiming');
        if (setTotalEl2) setTotalEl2.textContent = `0:00 / ${totalSetStr_loc}`;
        return;
    }

    // Calculate current phase and progress (after initial delay) using running settings
    const swimSeconds = runningData.swimTime;
    const restSeconds = runningData.restTime;
    const totalRoundTime = swimSeconds + restSeconds;

    // Update current round (after initial delay)
    const calculatedRound = Math.floor(timeAfterInitialDelay / totalRoundTime) + 1;
    if (calculatedRound !== currentRounds[currentLane] && calculatedRound <= runningData.numRounds) {
        currentRounds[currentLane] = calculatedRound;
        document.getElementById('currentRound').textContent = currentRounds[currentLane];
    }

    // Update set total timing: elapsed since pacerStartTimes (including initial delay) vs totalSetDuration
    const lastSwimmerStartDelay2 = (runningData.swimmerInterval || 0) + ((runningData.numSwimmers - 1) * (runningData.swimmerInterval || 0));
    const totalSetTimePerSwimmer2 = runningData.numRounds * totalRoundTime;
    const totalSetDuration2 = lastSwimmerStartDelay2 + totalSetTimePerSwimmer2;
    const totalSetMinutes2 = Math.floor(totalSetDuration2 / 60);
    const totalSetSecondsOnly2 = Math.floor(totalSetDuration2 % 60);
    const totalSetStr2 = `${totalSetMinutes2}:${totalSetSecondsOnly2.toString().padStart(2, '0')}`;
    const elapsedSinceStart = elapsedSeconds; // includes initial delay
    const setTotalEl3 = document.getElementById('setTotalTiming');
    if (setTotalEl3) {
        const elapsedStr = formatSecondsToMmSs(elapsedSinceStart);
        setTotalEl3.textContent = `${elapsedStr} / ${totalSetStr2}`;
    }

    // Check if ALL swimmers have completed the set
    // Calculate when the last swimmer finishes
    const lastSwimmerStartDelay = (runningData.swimmerInterval || 0) + ((runningData.numSwimmers - 1) * runningData.swimmerInterval);
    const totalSetTimePerSwimmer = runningData.numRounds * totalRoundTime;
    const lastSwimmerFinishTime = lastSwimmerStartDelay + totalSetTimePerSwimmer;

    // Check if the entire set is complete (all swimmers finished)
    if (elapsedSeconds >= lastSwimmerFinishTime) {
    // Set complete — update currentRound only
    const currentRoundEl = document.getElementById('currentRound');
    if (currentRoundEl) currentRoundEl.textContent = runningData.numRounds;

        // Handle queue completion (only once)
        if (activeSwimSets[currentLane] && !completionHandled[currentLane]) {
            completionHandled[currentLane] = true;
            // Set is complete, remove from queue and handle next set
            setTimeout(() => {
                handleSetCompletion();
            }, 2000); // Give user 2 seconds to see completion message
        }
    }
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
        for (let i = 0; i < currentSettings.numSwimmersPerLane[currentLane]; i++) {
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
        createdSwimSets[currentSettings.currentLane] = {
            id: Date.now(),
            lane: currentSettings.currentLane,
            laneName: currentSettings.laneNames[currentSettings.currentLane],
            swimmers: JSON.parse(JSON.stringify(newSet)),
            settings: { ...currentSettings, swimTime: currentSettings.swimTime },
            summary: generateSetSummary(newSet, currentSettings)
        };

        console.log('New swim set created:', createdSwimSets[currentSettings.currentLane]);
    } else {
        console.log('Updating existing swimmer set from config');
        // Update existing set in-place
        // Resize if number of swimmers changed
        if (currentSettings.numSwimmersPerLane[currentSettings.currentLane] < currentSet.length) {
            currentSet = currentSet.slice(0, currentSettings.numSwimmersPerLane[currentSettings.currentLane]);
        } else if (currentSettings.numSwimmersPerLane[currentSettings.currentLane] > currentSet.length) {
            for (let i = currentSet.length; i < currentSettings.numSwimmersPerLane[currentSettings.currentLane]; i++) {
                let swimmerColor;
                if (currentSettings.colorMode === 'same') {
                    swimmerColor = currentSettings.swimmerColor;
                } else {
                    swimmerColor = colorHex[swimmerColors[i]];
                }
                currentSet.push({
                    id: i + 1,
                    color: swimmerColor,
                    swimTime: currentSettings.swimTime,
                    interval: (i + 1) * currentSettings.swimmerInterval,
                    lane: currentSettings.currentLane
                });
            }
        }

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

        // Keep createdSwimSets in sync so queuing/start paths can use it
        createdSwimSets[currentSettings.currentLane] = {
            id: createdSwimSets[currentSettings.currentLane] ? createdSwimSets[currentSettings.currentLane].id : Date.now(),
            lane: currentSettings.currentLane,
            laneName: currentSettings.laneNames[currentSettings.currentLane],
            swimmers: JSON.parse(JSON.stringify(currentSet)),
            settings: { ...currentSettings, swimTime: currentSettings.swimTime },
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

    // Resize swimmerSets[lane]
    let set = swimmerSets[lane] || [];
    if (deviceNumSwimmers < set.length) {
        set = set.slice(0, deviceNumSwimmers);
    } else if (deviceNumSwimmers > set.length) {
        for (let i = set.length; i < deviceNumSwimmers; i++) {
            set.push({
                id: i + 1,
                color: (currentSettings.colorMode === 'same') ? currentSettings.swimmerColor : colorHex[swimmerColors[i]],
                swimTime: (parseTimeInput(document.getElementById('swimTime').value) || 30),
                interval: (i + 1) * currentSettings.swimmerInterval,
                lane: lane
            });
        }
    }
    swimmerSets[lane] = set;

    // Resize createdSwimSets if present
    const created = createdSwimSets[lane];
    if (created && created.swimmers) {
        let cs = created.swimmers;
        if (deviceNumSwimmers < cs.length) cs = cs.slice(0, deviceNumSwimmers);
        else if (deviceNumSwimmers > cs.length) {
            for (let i = cs.length; i < deviceNumSwimmers; i++) {
                cs.push({
                    id: i + 1,
                    color: (currentSettings.colorMode === 'same') ? currentSettings.swimmerColor : colorHex[swimmerColors[i]],
                    swimTime: created.settings ? (created.settings.swimTime || parseTimeInput(document.getElementById('swimTime').value)) : parseTimeInput(document.getElementById('swimTime').value),
                    interval: (i + 1) * (created.settings ? created.settings.swimmerInterval || currentSettings.swimmerInterval : currentSettings.swimmerInterval),
                    lane: lane
                });
            }
        }
        created.swimmers = cs;
        created.settings = created.settings || {};
        created.settings.numSwimmers = deviceNumSwimmers;
        created.summary = generateSetSummary(created.swimmers, created.settings || currentSettings);
        createdSwimSets[lane] = created;
    }

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
    const swimDistance = settings.swimDistance;
    const swimTime = swimmers[0].swimTime;
    const restTime = settings.restTime;
    const numRounds = settings.numRounds;

    // Display swim/rest time using compact formatting: 'Ns' for <=60s, 'M:SS' for >60s
    const avgSwimTimeDisplay = formatCompactTime(swimTime);
    const restDisplay = formatCompactTime(restTime);

    return `${numRounds} x ${swimDistance}'s on the ${avgSwimTimeDisplay} with ${restDisplay} rest`;
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
    const currentLane = currentSettings.currentLane;
    const queueList = document.getElementById('queueList');

    if (!queueList) {
        console.error('Queue list element not found');
        return;
    }

    let html = '';

    // Show all sets in the queue (completed and pending)
    if (swimSetQueues[currentLane].length === 0) {
        html += '<div style="color: #666; font-style: italic;">No sets queued for Lane ' + (currentLane + 1) + '</div>';
    } else {
    swimSetQueues[currentLane].forEach((swimSet, index) => {
            // Determine active set using the tracked active index for this lane.
            const isActive = (activeSwimSetIndex[currentLane] === index);
            const isCompleted = swimSet.completed;

            let statusClass = '';
            let borderColor = '#ddd';
            let backgroundColor = '#fff';

            if (isActive) {
                backgroundColor = '#e7f3ff';
                borderColor = '#2196F3';
            } else if (isCompleted) {
                backgroundColor = '#f8f9fa';
                borderColor = '#28a745';
            }

            // If this queue entry is an action/rest item, render it specially
            const draggable = (!isActive && !isCompleted) ? `draggable="true" ondragstart="handleDragStart(event, ${index})" ondragover="handleDragOver(event)" ondrop="handleDrop(event, ${index})"` : '';
            if (swimSet.type === 'rest' || swimSet.type === 'action') {
                /*
                const actionSummary = swimSet.summary || (swimSet.type === 'rest' ? `Rest: ${formatSecondsToMmSs(swimSet.seconds)}` : (swimSet.summary || 'Action'));
                const actionClass = 'style="background: #f5f5f5; border: 1px dashed #bbb; padding: 8px 10px; margin: 5px 0; border-radius: 4px;"';
                html += `
                    <div ${actionClass} class="queue-action-item" ${draggable}>
                        <div style="display:flex; justify-content:space-between; align-items:center;">
                            <div style="font-style: italic; color: #444;">
                                ${actionSummary}
                            </div>
                            <div style="display:flex; gap:6px;">
                                <button onclick="deleteSwimSet(${index})" style="padding: 2px 6px; font-size: 12px; background: #dc3545; color: white; border: none; border-radius: 3px; cursor: pointer;">Delete</button>
                            </div>
                        </div>
                    </div>
                `;
                */
            } else {
                statusClass = `style="background: ${backgroundColor}; border: 1px solid ${borderColor}; padding: 8px 10px; margin: 5px 0; border-radius: 4px; ${isCompleted ? 'opacity: 0.8;' : ''}"`;

                // Get the correct round values for display
                const displayCurrentRound = isActive ? currentRounds[currentLane] : 1;
                const displayTotalRounds = (swimSet.settings && swimSet.settings.numRounds) ||
                                          (isActive && runningSettings[currentLane] ? runningSettings[currentLane].numRounds : 10);

                html += `
                    <div ${statusClass} ${draggable}>
                        <div style="display: flex; justify-content: space-between; align-items: center;">
                            <div style="font-weight: bold; color: ${isActive ? '#1976D2' : isCompleted ? '#28a745' : '#333'};">
                                ${swimSet.summary}
                            </div>
                            <div style="display: flex; gap: 5px; align-items: center;">
                                ${isActive ? `<div style="font-weight: bold; color: #1976D2; margin-left: 8px;">Round: <span id="currentRound">${displayCurrentRound}</span>/<span id="totalRounds">${displayTotalRounds}</span></div>` : ''}
                                ${!isActive && !isCompleted ? `<button onclick="editSwimSet(${index})" style="padding: 2px 6px; font-size: 12px; background: #007bff; color: white; border: none; border-radius: 3px; cursor: pointer;">Edit</button>` : ''}
                                ${!isActive && !isCompleted ? `<button onclick="deleteSwimSet(${index})" style="padding: 2px 6px; font-size: 12px; background: #dc3545; color: white; border: none; border-radius: 3px; cursor: pointer;">Delete</button>` : ''}
                                ${isCompleted ? `<div style="font-size: 12px; color: #28a745; font-weight: bold;">Completed</div>` : ''}
                            </div>
                        </div>
                    </div>
                `;
            }
        });
    }

    queueList.innerHTML = html;
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
        if (activeSwimSets[currentLane]) {
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
    newEntry.settings = JSON.parse(JSON.stringify(currentSettings));
    newEntry.summary = generateSetSummary(newEntry.swimmers, newEntry.settings);

    // Prepare payload for update endpoint
    const payload = buildMinimalSwimSetPayload(newEntry);
    payload.lane = currentLane;

    if (existing.id && existing.id !== 0) payload.matchId = existing.id;
    else if (existing.clientTempId) payload.matchClientTempId = String(existing.clientTempId);

    // Generate a new clientTempId to set on the server for correlation
    const newClientTemp = (Date.now().toString(16) + Math.floor(Math.random() * 0xFFFFFF).toString(16)).slice(0,16);
    payload.clientTempId = newClientTemp;

    // Try to update on the device; if network fails, apply local-only change
    console.log('saveSwimSet calling /updateSwimSet');
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
        swimSetQueues[currentLane][editingSwimSetIndexes[currentLane]] = newEntry;

        // Clear editing state and return to create mode
        editingSwimSetIndexes[currentLane] = -1;
        createdSwimSets[currentLane] = null;
        returnToConfigMode();
        updateQueueDisplay();
    }).catch(err => {
        // Device not reachable or update failed — keep local change and mark unsynced
        newEntry.synced = false;
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

    if (confirm('Delete this swim set from Lane ' + (currentLane + 1) + '?')) {
        swimSetQueues[currentLane].splice(index, 1);
        updateQueueDisplay();
        updatePacerButtons();
    }
}

// Drag & Drop handlers for reordering queue items
function handleDragStart(evt, index) {
    const currentLane = currentSettings.currentLane;
    // Safeguard: do not allow dragging items that are active or completed
    const isActive = (activeSwimSetIndex[currentLane] === index);
    const isCompleted = !!(swimSetQueues[currentLane] && swimSetQueues[currentLane][index] && swimSetQueues[currentLane][index].completed);
    if (isActive || isCompleted) {
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
        const isActive = (activeSwimSetIndex[currentLane] === i);
        const isCompleted = !!s.completed;
        if (isActive || isCompleted) lastProtectedIndex = Math.max(lastProtectedIndex, i);
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
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `lane=${currentLane}&order=${encodeURIComponent(order)}`
    }).then(resp => {
        if (!resp.ok) throw new Error('reorder failed');
        // Optionally reconcile response in the future
    }).catch(err => {
        console.log('reorder request failed, will keep local order until reconciliation', err);
    });
}

function startQueue() {
    const currentLane = currentSettings.currentLane;

    if (swimSetQueues[currentLane].length === 0) return;

    // Find the first non-completed swim set in current lane's queue
    const nextSwimSet = swimSetQueues[currentLane].find(set => !set.completed);
    if (!nextSwimSet) return; // No pending sets to start

    // Start the next non-completed swim set
    activeSwimSets[currentLane] = nextSwimSet;

    // Track the active set index so only that queued item shows runtime details
    activeSwimSetIndex[currentLane] = swimSetQueues[currentLane].indexOf(nextSwimSet);

    // Load the active set into the pacer system
    loadSwimSetForExecution(activeSwimSets[currentLane]);

    // Start the pacer
    startPacerExecution();

    // Update displays (this creates the DOM elements)
    updateQueueDisplay();
    updatePacerButtons();

    // Initialize pacer status display AFTER DOM elements are created
    setTimeout(() => {
        initializePacerStatus();
    }, 10); // Small delay to ensure DOM is updated
}

function stopQueue() {
    const currentLane = currentSettings.currentLane;

    // Stop current execution
    stopPacerExecution();

    // Clear active set for current lane
    activeSwimSets[currentLane] = null;
    activeSwimSetIndex[currentLane] = -1;

    // Update displays
    updateQueueDisplay();
    updatePacerButtons();
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

function startPacerExecution() {
    // Use existing pacer start logic
    const currentLane = currentSettings.currentLane;
    const currentSet = getCurrentSwimmerSet();

    if (currentSet.length === 0) {
        alert('No swimmers configured for this lane');
        return;
    }

    // Create immutable copies for running state
    runningSets[currentLane] = JSON.parse(JSON.stringify(currentSet));
    runningSettings[currentLane] = JSON.parse(JSON.stringify(currentSettings));

    // Start the pacer for this lane
    laneRunning[currentLane] = true;
    currentSettings.isRunning = true;

    // Reset completion tracking for this lane
    completionHandled[currentLane] = false;

    // Initialize timing - always reset for new queue start
    pacerStartTimes[currentLane] = Date.now();

    // Send start command to ESP32
    // Send the swim set to the device and request immediate start
    const created = createdSwimSets[currentLane] || {
        id: Date.now(),
        lane: currentLane,
        swimmers: runningSets[currentLane] || [],
        settings: runningSettings[currentLane] || currentSettings,
        summary: ''
    };

    // If we have a created swim set with specific rounds, update runningSettings
    if (created && created.settings && created.settings.numRounds !== undefined) {
        runningSettings[currentLane].numRounds = created.settings.numRounds;
    }

    const payload = buildMinimalSwimSetPayload(created);
    // Include lane so device starts the set for the correct lane
    payload.lane = created.lane || currentLane;
    fetch('/runSwimSetNow', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    }).then(() => {
        console.log('runSwimSetNow sent');
    }).catch(() => {
        // fallback to old toggle when device not reachable
        sendStartCommand();
    });

    // Start status updates (timer display)
    startStatusUpdates();

    // Update status display
    updateStatus();
}

function stopPacerExecution() {
    const currentLane = currentSettings.currentLane;

    // Stop the pacer for this lane
    laneRunning[currentLane] = false;
    currentSettings.isRunning = false;

    // Stop status updates (timer display)
    stopStatusUpdates();

    // Clear running state
    runningSets[currentLane] = null;
    runningSettings[currentLane] = null;

    // Clear timing state to ensure fresh start next time
    pacerStartTimes[currentLane] = 0;

    // Update visual status indicators (border color)
    updateStatus();

    // Send stop command to ESP32
    sendStopCommand();
}

function sendStopCommand() {
    fetch('/toggle', { method: 'POST' })
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
    return {
        rounds: Number(settings.numRounds || currentSettings.numRounds),
        swimDistance: Number(settings.swimDistance || currentSettings.swimDistance),
        swimSeconds: Number(settings.swimTime || currentSettings.swimTime),
        restSeconds: Number(settings.restTime || currentSettings.restTime),
        swimmerInterval: Number(settings.swimmerInterval || currentSettings.swimmerInterval),
        type: 0,
        repeat: 0
    };
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
        const res = await fetch('/getSwimQueue');
        if (!res.ok) return;
        const deviceQueue = await res.json();

        // Build quick lookup structures for device entries: by clientTempId and by signature
        const deviceByClientTemp = new Map();
        const deviceSignatures = [];
        for (let i = 0; i < deviceQueue.length; i++) {
            const d = deviceQueue[i];
            if (d.clientTempId !== undefined && d.clientTempId !== null && String(d.clientTempId) !== '0' && String(d.clientTempId) !== '') {
                deviceByClientTemp.set(String(d.clientTempId), d);
            }
            const sig = `${Number(d.swimSeconds).toFixed(0)}|${d.restSeconds}|${d.rounds}|${Number(d.swimDistance).toFixed(0)}`;
            deviceSignatures.push(sig);
        }

        // Iterate local queue and mark swim sets as synced when their clientTempId or signature appears
        for (let i = 0; i < swimSetQueues[lane].length; i++) {
            const local = swimSetQueues[lane][i];
            // Only reconcile swim-sets (not action/rest items)
            if (local.type === 'rest' || local.type === 'action') continue;

            // Prefer direct clientTempId match when available
            if (local.clientTempId) {
                const d = deviceByClientTemp.get(String(local.clientTempId));
                if (d) {
                    local.synced = true;
                    if (d.id !== undefined) local.id = d.id;
                    continue; // matched
                }
            }

            // Fallback to signature heuristic
            const sig = `${Math.round(local.settings.swimTime)}|${local.settings.restTime}|${local.settings.numRounds}|${Number(local.settings.swimDistance).toFixed(0)}`;
            const idx = deviceSignatures.indexOf(sig);
            if (idx !== -1) {
                const d = deviceQueue[idx];
                local.synced = true;
                if (d.id !== undefined) local.id = d.id;
            }
        }
        updateQueueDisplay();
    } catch (e) {
        // ignore network errors
    }
}

// Periodic reconciliation timer id
let reconcileIntervalId = null;

// Completion handling options - change this to experiment with different behaviors
const COMPLETION_MODE = 'MARK_COMPLETE'; // Options: 'REMOVE_SET', 'MARK_COMPLETE'

function handleSetCompletion() {
    const currentLane = currentSettings.currentLane;

    if (!activeSwimSets[currentLane]) return;

    if (COMPLETION_MODE === 'REMOVE_SET') {
        // Option 1: Remove completed set from current lane's queue
        const completedSetIndex = swimSetQueues[currentLane].findIndex(set => set.id === activeSwimSets[currentLane].id);
        if (completedSetIndex !== -1) {
            swimSetQueues[currentLane].splice(completedSetIndex, 1);
        }
    } else if (COMPLETION_MODE === 'MARK_COMPLETE') {
        // Option 2: Mark set as complete in place within the queue
        const completedSetIndex = swimSetQueues[currentLane].findIndex(set => set.id === activeSwimSets[currentLane].id);
        if (completedSetIndex !== -1) {
            // Mark the set as completed in place
            swimSetQueues[currentLane][completedSetIndex].completed = true;
            swimSetQueues[currentLane][completedSetIndex].completedAt = new Date();
        }
    }

    // Clear active set for current lane
    activeSwimSets[currentLane] = null;
    activeSwimSetIndex[currentLane] = -1;

    // Reset completion flag for this lane
    completionHandled[currentLane] = false;

    // Stop current execution
    stopPacerExecution();

    // Check if there are more sets in current lane's queue (only count non-completed sets)
    const remainingSets = swimSetQueues[currentLane].filter(set => !set.completed);
    if (remainingSets.length > 0) {
        // Auto-advance to next non-completed set after a brief pause
        setTimeout(() => {
            startQueue(); // No more alert - just auto-start
        }, 1500);
    } else {
        // No more sets, update displays
        updateQueueDisplay();
        updatePacerButtons();
    }
}

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
        // Ensure we have the current lane first
        console.log("Fetching currentLane");
        const laneRes = await fetch('/currentLane');
        if (laneRes.ok) {
            const laneJson = await laneRes.json();
            console.log("Received currentLane");
            if (laneJson.currentLane !== undefined) {
                currentSettings.currentLane = Number(laneJson.currentLane);
            }
        }

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

        // isRunning
        if (dev.isRunning !== undefined) {
            currentSettings.isRunning = (dev.isRunning === 'true' || dev.isRunning === true);
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
    fetch('/toggle', { method: 'POST' })
        .then(response => response.text())
        .then(result => {
            console.log('Start command issued, server response:', result);
        })
        .catch(err => {
            console.log('Start command failed (standalone mode)');
        });
}
