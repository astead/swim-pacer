// Detect if running in standalone mode (file:// protocol)
const isStandaloneMode = window.location.protocol === 'file:';

let currentSettings = {
    // speed is meters per second (client and device now use m/s)
    speed: 1.693,
    color: 'red',
    brightness: 196,
    pulseWidth: 1.0,
    restTime: 5,
    paceDistance: 50,
    swimmerInterval: 1,
    delayIndicatorsEnabled: true,
    numSwimmers: 3,
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
    hideAfter: 3,
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

// Conversion functions for swimming
function paceToSpeed(paceSeconds, poolYards = 50) {
    // Return speed in meters/second for a given pace (seconds per pool length).
    // poolYards is the pool length in yards (default 50 yards). Convert to meters then divide by time.
    const poolMeters = poolYards * 0.9144;
    return poolMeters / paceSeconds; // meters per second
}

function speedToPace(speedFps, poolYards = 50) {
    // speed is meters/second; return pace (seconds per pool length)
    const poolMeters = poolYards * 0.9144;
    return poolMeters / speedFps;
}

// Helper function to parse time input (supports both "30" and "1:30" formats)
function parseTimeInput(timeStr) {
    if (!timeStr || timeStr.trim() === '') return 30; // Default to 30 seconds

    timeStr = timeStr.trim();

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

function updateFromPace() {
    const paceInput = document.getElementById('paceTimeSeconds').value;
    const pace = parseTimeInput(paceInput);
    const paceDistance = currentSettings.paceDistance;

    // Let's always set pace to speed (meters per second)
    // Compute pool distance in meters first, then divide by seconds to get m/s
    let poolMeters = paceDistance;
    if (currentSettings.poolLengthUnits === 'yards') {
        poolMeters = paceDistance * 0.9144; // yards -> meters
    }
    // Avoid divide-by-zero
    currentSettings.speed = (pace > 0) ? (poolMeters / pace) : 0;
}

function updatePaceDistance(triggerSave = true) {
    const paceDistance = parseInt(document.getElementById('paceDistance').value);
    currentSettings.paceDistance = paceDistance;

    // Recalculate speed based on current pace input and new distance
    updateFromPace();
    if (triggerSave) {
        // Save only the pace distance and derived speed
        sendPaceDistance(currentSettings.paceDistance);
        sendSpeed(currentSettings.speed);
    }
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
    // Recompute pace-derived internals but suppress the global save here.
    updatePaceDistance(false);

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
    updateLaneSelector();
    updateLaneNamesSection();
    sendNumLanes(numLanes);
}

function updateLaneSelector() {
    const laneSelector = document.getElementById('laneSelector');
    const currentLaneSelect = document.getElementById('currentLane');

    // Show/hide lane selector based on number of lanes
    if (currentSettings.numLanes > 1) {
        laneSelector.style.display = 'block';

        // Populate lane options
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
        laneSelector.style.display = 'none';
        currentSettings.currentLane = 0; // Default to lane 0 for single lane
    }
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
    const unit = parseFloat(pulseWidth) === 1.0 ? ' foot' : ' feet';
    document.getElementById('pulseWidthValue').textContent = pulseWidth + unit;
    sendPulseWidth(pulseWidth);
}

function updateRestTime() {
    const restTime = document.getElementById('restTime').value;
    currentSettings.restTime = parseInt(restTime);
    const unit = parseInt(restTime) === 1 ? ' second' : ' seconds';
    const rlabel = document.getElementById('restTimeValue');
    if (rlabel) rlabel.textContent = restTime + unit;
    // Also send rest time to device so it can be used as a default for swim-sets
    sendRestTime(currentSettings.restTime);
}



function updateSwimmerInterval() {
    const swimmerInterval = document.getElementById('swimmerInterval').value;
    currentSettings.swimmerInterval = parseInt(swimmerInterval);
    const unit = parseInt(swimmerInterval) === 1 ? ' second' : ' seconds';
    const silabel = document.getElementById('swimmerIntervalValue');
    if (silabel) silabel.textContent = swimmerInterval + unit;
    // Also send swimmer interval to device so it can be used as a default for swim-sets
    sendSwimmerInterval(currentSettings.swimmerInterval);
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
    const unit = parseFloat(lightSize) === 1.0 ? ' foot' : ' feet';
    document.getElementById('lightSizeValue').textContent = lightSize + unit;
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
    const unit = parseInt(hideAfter) === 1 ? ' second' : ' seconds';
    document.getElementById('hideAfterValue').textContent = hideAfter + unit;
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
    const unit = v === 1.0 ? ' foot' : ' feet';
    const label = document.getElementById('pulseWidthValue');
    if (label) label.textContent = v + unit;
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
    const unit = v === 1 ? ' second' : ' seconds';
    const label = document.getElementById('hideAfterValue');
    if (label) label.textContent = v + unit;
}

function setLightSizeUI(value) {
    if (value === undefined || value === null) return;
    const v = parseFloat(value);
    const el = document.getElementById('lightSize');
    if (el) el.value = v;
    currentSettings.lightSize = v;
    const unit = v === 1.0 ? ' foot' : ' feet';
    const label = document.getElementById('lightSizeValue');
    if (label) label.textContent = v + unit;
}

function setNumSwimmersUI(value) {
    if (value === undefined || value === null) return;
    const v = parseInt(value);
    const el = document.getElementById('numSwimmers');
    if (el) el.value = v;
    currentSettings.numSwimmers = v;
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
    const numSwimmers = document.getElementById('numSwimmers').value;
    currentSettings.numSwimmers = parseInt(numSwimmers);
    sendNumSwimmers(currentSettings.numSwimmers);
}

function updateNumRounds() {
    const numRounds = document.getElementById('numRounds').value;
    currentSettings.numRounds = parseInt(numRounds);
    sendNumRounds(currentSettings.numRounds);
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
// When true, broad settings writes are suppressed to avoid overwriting device defaults
let suppressSettingsWrites = false;

function updateStatus() {
    const queueDisplay = document.getElementById('queueDisplay');
    const queueTitle = queueDisplay.querySelector('h4');
    const toggleBtnElement = document.getElementById('toggleBtn');

    if (queueDisplay && queueTitle) {
        if (currentSettings.isRunning) {
            // Running: green border and title
            queueDisplay.style.borderLeft = '4px solid #28a745';
            queueTitle.style.color = '#28a745';
        } else {
            // Stopped: red border and title
            queueDisplay.style.borderLeft = '4px solid #dc3545';
            queueTitle.style.color = '#dc3545';
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
            paceDistance: currentSettings.paceDistance,
            paceTimeSeconds: parseTimeInput(document.getElementById('paceTimeSeconds').value),
            restTime: currentSettings.restTime,
            numRounds: currentSettings.numRounds,
            swimmerInterval: currentSettings.swimmerInterval,
            numSwimmers: currentSettings.numSwimmers,
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
    const paceSeconds = runningData.paceTimeSeconds;
    const restSeconds = runningData.restTime;
    const totalRoundTime = paceSeconds + restSeconds;
    const totalRoundMinutes = Math.floor(totalRoundTime / 60);
    const totalRoundSecondsOnly = Math.floor(totalRoundTime % 60);
    const totalTimeStr = `${totalRoundMinutes}:${totalRoundSecondsOnly.toString().padStart(2, '0')}`;

    const roundTimingEl = document.getElementById('roundTiming');
    if (roundTimingEl) {
        roundTimingEl.textContent = `0:00 / ${totalTimeStr}`;
    }

    const activeSwimmersEl = document.getElementById('activeSwimmers');
    if (activeSwimmersEl) {
        activeSwimmersEl.textContent = '0';
    }

    // Update set basics display using running settings
    const paceDistance = runningData.paceDistance;
    const numRounds = runningData.numRounds;
    const setBasicsEl = document.getElementById('setBasics');
    if (setBasicsEl) {
        setBasicsEl.textContent = `- ${numRounds} x ${paceDistance}'s`;
    }

    // Show initial delay countdown using running settings
    // Use swimmerInterval as the initial delay for the first swimmer
    const initialDelayDisplay = runningData.swimmerInterval || 0;
    const nextEventEl = document.getElementById('nextEvent');
    if (nextEventEl) {
        if (initialDelayDisplay > 0) nextEventEl.textContent = `Starting in ${initialDelayDisplay}s`; else nextEventEl.textContent = 'Starting now';
    }
    const currentPhaseEl = document.getElementById('currentPhase');
    if (currentPhaseEl) {
        currentPhaseEl.textContent = initialDelayDisplay > 0 ? 'Initial Delay' : 'Swimming';
    }

    const elapsedTimeEl = document.getElementById('elapsedTime');
    if (elapsedTimeEl) {
        elapsedTimeEl.textContent = '00:00';
    }
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
    const minutes = Math.floor(elapsedSeconds / 60);
    const seconds = elapsedSeconds % 60;
    document.getElementById('elapsedTime').textContent =
        `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;

    // Account for initial delay using running settings
    const initialDelaySeconds = runningData.swimmerInterval || 0;
    const timeAfterInitialDelay = elapsedSeconds - initialDelaySeconds;

    // Check if we're still in the initial delay period
    if (timeAfterInitialDelay < 0) {
        // Still in initial delay phase
        document.getElementById('currentPhase').textContent = 'Initial Delay';
        document.getElementById('activeSwimmers').textContent = '0';
        document.getElementById('nextEvent').textContent = `Starting in ${Math.ceil(-timeAfterInitialDelay)}s`;

        // Show initial round timing during delay using running settings
    const paceSeconds = runningData.paceTimeSeconds;
        const restSeconds = runningData.restTime;
        const totalRoundTime = paceSeconds + restSeconds;
        const totalRoundMinutes = Math.floor(totalRoundTime / 60);
        const totalRoundSecondsOnly = Math.floor(totalRoundTime % 60);
        const totalTimeStr = `${totalRoundMinutes}:${totalRoundSecondsOnly.toString().padStart(2, '0')}`;
        document.getElementById('roundTiming').textContent = `0:00 / ${totalTimeStr}`;

        document.getElementById('currentRound').textContent = '1';
        return;
    }

    // Calculate current phase and progress (after initial delay) using running settings
    const paceSeconds = runningData.paceTimeSeconds;
    const restSeconds = runningData.restTime;
    const totalRoundTime = paceSeconds + restSeconds;

    const timeInCurrentRound = timeAfterInitialDelay % totalRoundTime;

    // Format round timing display
    const currentRoundMinutes = Math.floor(timeInCurrentRound / 60);
    const currentRoundSeconds = Math.floor(timeInCurrentRound % 60);
    const totalRoundMinutes = Math.floor(totalRoundTime / 60);
    const totalRoundSecondsOnly = Math.floor(totalRoundTime % 60);

    const currentTimeStr = `${currentRoundMinutes}:${currentRoundSeconds.toString().padStart(2, '0')}`;
    const totalTimeStr = `${totalRoundMinutes}:${totalRoundSecondsOnly.toString().padStart(2, '0')}`;

    document.getElementById('roundTiming').textContent = `${currentTimeStr} / ${totalTimeStr}`;

    // Determine current phase
    if (timeInCurrentRound < paceSeconds) {
        document.getElementById('currentPhase').textContent = 'Swimming';
        document.getElementById('activeSwimmers').textContent = runningData.numSwimmers;
        const remainingSwimTime = paceSeconds - timeInCurrentRound;
        document.getElementById('nextEvent').textContent = `Rest in ${Math.ceil(remainingSwimTime)}s`;
    } else {
        document.getElementById('currentPhase').textContent = 'Rest Period';
        document.getElementById('activeSwimmers').textContent = '0';
        const remainingRestTime = totalRoundTime - timeInCurrentRound;
        document.getElementById('nextEvent').textContent = `Next round in ${Math.ceil(remainingRestTime)}s`;
    }

    // Update current round (after initial delay)
    const calculatedRound = Math.floor(timeAfterInitialDelay / totalRoundTime) + 1;
    if (calculatedRound !== currentRounds[currentLane] && calculatedRound <= runningData.numRounds) {
        currentRounds[currentLane] = calculatedRound;
        document.getElementById('currentRound').textContent = currentRounds[currentLane];
    }

    // Check if ALL swimmers have completed the set
    // Calculate when the last swimmer finishes
    const lastSwimmerStartDelay = (runningData.swimmerInterval || 0) + ((runningData.numSwimmers - 1) * runningData.swimmerInterval);
    const totalSetTimePerSwimmer = runningData.numRounds * totalRoundTime;
    const lastSwimmerFinishTime = lastSwimmerStartDelay + totalSetTimePerSwimmer;

    // Check if the entire set is complete (all swimmers finished)
    if (elapsedSeconds >= lastSwimmerFinishTime) {
        document.getElementById('currentPhase').textContent = 'Set Complete!';
        document.getElementById('nextEvent').textContent = 'All swimmers finished';
        document.getElementById('activeSwimmers').textContent = '0';

        // Handle queue completion (only once)
        if (activeSwimSets[currentLane] && !completionHandled[currentLane]) {
            completionHandled[currentLane] = true;
            // Set is complete, remove from queue and handle next set
            setTimeout(() => {
                handleSetCompletion();
            }, 2000); // Give user 2 seconds to see completion message
        }
    } else if (calculatedRound > runningData.numRounds) {
        // First swimmer(s) finished, but others still swimming
        const remainingTime = Math.ceil(lastSwimmerFinishTime - elapsedSeconds);
        const remainingMinutes = Math.floor(remainingTime / 60);
        const remainingSeconds = remainingTime % 60;
        const timeDisplay = remainingMinutes > 0 ?
            `${remainingMinutes}:${remainingSeconds.toString().padStart(2, '0')}` :
            `${remainingSeconds}s`;

        document.getElementById('currentPhase').textContent = 'Trailing swimmers finishing';
        document.getElementById('nextEvent').textContent = `Complete in ${timeDisplay}`;

        // Calculate how many swimmers are still active
        let activeSwimmers = 0;
        for (let i = 0; i < runningData.numSwimmers; i++) {
            const swimmerStartTime = (runningData.swimmerInterval || 0) + (i * runningData.swimmerInterval);
            const swimmerFinishTime = swimmerStartTime + totalSetTimePerSwimmer;
            if (elapsedSeconds < swimmerFinishTime) {
                activeSwimmers++;
            }
        }
        document.getElementById('activeSwimmers').textContent = activeSwimmers;
    }
}

function createSwimSet() {
    // Validate configuration first
    if (currentSettings.numSwimmers < 1) {
        alert('Please set at least 1 swimmer');
        return;
    }

    if (currentSettings.numRounds < 1) {
        alert('Please set at least 1 round');
        return;
    }

    const configControls = document.getElementById('configControls');
    const swimmerSetDiv = document.getElementById('swimmerSet');

    // Clear existing set for current lane
    setCurrentSwimmerSet([]);

    // Get current pace from the main settings
    const currentPace = parseTimeInput(document.getElementById('paceTimeSeconds').value);

    // Create swimmer configurations for current lane
    const newSet = [];
    for (let i = 0; i < currentSettings.numSwimmers; i++) {
        // Determine color based on color mode
        let swimmerColor;
        if (currentSettings.colorMode === 'same') {
            // Store the actual hex color directly to avoid shared "custom" reference
            swimmerColor = currentSettings.swimmerColor;
        } else {
            // Use predefined colors for individual mode
            swimmerColor = colorHex[swimmerColors[i]];
        }

        // Create individual swimmer object (no shared references)
        const newSwimmer = {
            id: i + 1,
            color: swimmerColor, // Store hex color directly
            pace: currentPace,
            interval: (i + 1) * currentSettings.swimmerInterval, // Use swimmerInterval for first-swimmer delay and subsequent offsets
            lane: currentSettings.currentLane // Track which lane this swimmer belongs to
        };

        newSet.push(newSwimmer);
        //console.log(`Created swimmer ${i + 1} for ${currentSettings.laneNames[currentSettings.currentLane]} with color: ${swimmerColor}`);
    }

    // Store the set for current lane
    setCurrentSwimmerSet(newSet);

    // Create swim set object with metadata
        createdSwimSets[currentSettings.currentLane] = {
        id: Date.now(), // Simple unique ID
        lane: currentSettings.currentLane,
        laneName: currentSettings.laneNames[currentSettings.currentLane],
        swimmers: newSet,
        settings: {
            ...currentSettings,
            paceTimeSeconds: currentPace // Explicitly include the pace value
        }, // Deep copy of current settings with pace
        summary: generateSetSummary(newSet, currentSettings)
    };

    // Display the set
    displaySwimmerSet();

    // Hide config controls and show swimmer set
    configControls.style.display = 'none';
    swimmerSetDiv.style.display = 'block';

    // Switch to queue buttons
    document.getElementById('configButtons').style.display = 'none';
    document.getElementById('queueButtons').style.display = 'block';
}

// Create or update the current lane's swimmer set from the configuration controls.
// If resetPaces is true, any per-swimmer custom pace values are overwritten with the
// current pace input value (useful when the user changes the swim time).
function createOrUpdateSwimmerSetFromConfig(resetPaces = false) {
    const currentLane = currentSettings.currentLane;
    let currentSet = getCurrentSwimmerSet() || [];

    const numSwimmers = parseInt(document.getElementById('numSwimmers').value) || currentSettings.numSwimmers;
    const currentPace = parseTimeInput(document.getElementById('paceTimeSeconds').value);
    const swimmerInterval = parseInt(document.getElementById('swimmerInterval').value) || currentSettings.swimmerInterval;

    // Ensure currentSettings reflect inputs
    currentSettings.numSwimmers = numSwimmers;
    currentSettings.swimmerInterval = swimmerInterval;
    currentSettings.numRounds = parseInt(document.getElementById('numRounds').value) || currentSettings.numRounds;
    currentSettings.restTime = parseInt(document.getElementById('restTime').value) || currentSettings.restTime;

    // If set is empty, create a fresh set using current inputs
    if (!currentSet || currentSet.length === 0) {
        const newSet = [];
        for (let i = 0; i < numSwimmers; i++) {
            let swimmerColor;
            if (currentSettings.colorMode === 'same') {
                swimmerColor = currentSettings.swimmerColor;
            } else {
                swimmerColor = colorHex[swimmerColors[i]];
            }
            const newSwimmer = {
                id: i + 1,
                color: swimmerColor,
                pace: currentPace,
                interval: (i + 1) * swimmerInterval,
                lane: currentLane
            };
            newSet.push(newSwimmer);
        }
        setCurrentSwimmerSet(newSet);
        // Also store as createdSwimSets so the rest of the UI expects a created set
        createdSwimSets[currentLane] = {
            id: Date.now(),
            lane: currentLane,
            laneName: currentSettings.laneNames[currentLane],
            swimmers: JSON.parse(JSON.stringify(newSet)),
            settings: { ...currentSettings, paceTimeSeconds: currentPace },
            summary: generateSetSummary(newSet, currentSettings)
        };
    } else {
        // Update existing set in-place
        // Resize if number of swimmers changed
        if (numSwimmers < currentSet.length) {
            currentSet = currentSet.slice(0, numSwimmers);
        } else if (numSwimmers > currentSet.length) {
            for (let i = currentSet.length; i < numSwimmers; i++) {
                let swimmerColor;
                if (currentSettings.colorMode === 'same') {
                    swimmerColor = currentSettings.swimmerColor;
                } else {
                    swimmerColor = colorHex[swimmerColors[i]];
                }
                currentSet.push({
                    id: i + 1,
                    color: swimmerColor,
                    pace: currentPace,
                    interval: (i + 1) * swimmerInterval,
                    lane: currentLane
                });
            }
        }

        // If pace changed and we must reset per-swimmer customizations, overwrite pace
        if (resetPaces) {
            for (let i = 0; i < currentSet.length; i++) {
                currentSet[i].pace = currentPace;
            }
        }

        // Recompute intervals and ids to keep consistent
        for (let i = 0; i < currentSet.length; i++) {
            currentSet[i].id = i + 1;
            currentSet[i].interval = (i + 1) * swimmerInterval;
            currentSet[i].lane = currentLane;
            // ensure color exists
            if (!currentSet[i].color) {
                currentSet[i].color = (currentSettings.colorMode === 'same') ? currentSettings.swimmerColor : colorHex[swimmerColors[i]];
            }
        }

        setCurrentSwimmerSet(currentSet);
        // Keep createdSwimSets in sync so queuing/start paths can use it
        createdSwimSets[currentLane] = {
            id: createdSwimSets[currentLane] ? createdSwimSets[currentLane].id : Date.now(),
            lane: currentLane,
            laneName: currentSettings.laneNames[currentLane],
            swimmers: JSON.parse(JSON.stringify(currentSet)),
            settings: { ...currentSettings, paceTimeSeconds: currentPace },
            summary: generateSetSummary(currentSet, currentSettings)
        };
    }

    // Refresh the swimmers UI if visible
    updateSwimmerSetDisplay();
}


function generateSetSummary(swimmers, settings) {
    const paceDistance = settings.paceDistance;
    const avgPace = swimmers.length > 0 ? swimmers[0].pace : 30;
    const restTime = settings.restTime;
    const numRounds = settings.numRounds;

    // Display pace as M:SS when > 60s, otherwise show seconds without decimals
    const avgPaceDisplay = (avgPace > 60) ? formatSecondsToMmSs(avgPace) : `${Math.round(avgPace)}s`;

    return `${numRounds} x ${paceDistance}'s on the ${avgPaceDisplay} with ${restTime} sec rest`;
}

function queueSwimSet() {
    const currentLane = currentSettings.currentLane;

    // Ensure we have a created set based on current inputs. If user is on the
    // Swimmer Customizations tab but hasn't explicitly created a set, build it
    // from the current controls so we can queue what they're seeing.
    try {
        if (!createdSwimSets[currentLane]) createOrUpdateSwimmerSetFromConfig(false);
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

// Enqueue a rest action from the special input box
function enqueueRestFromInput() {
    const v = document.getElementById('specialRestInput').value;
    const seconds = parseTimeInput(String(v));
    if (!seconds || seconds <= 0) {
        alert('Please enter a valid rest time (e.g., 30 or 0:30)');
        return;
    }
    const currentLane = currentSettings.currentLane;
    const actionItem = {
        id: Date.now(), // temporary optimistic id
        type: 'rest',
        seconds: seconds,
        summary: `Rest: ${formatSecondsToMmSs(seconds)}`
    };

    // Try to enqueue on device; if it fails, keep local queue only.
    // On success, reconcile the device-returned canonical id into our UI queue.
    fetch('/enqueueAction', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ lane: currentLane, action: actionItem })
    }).then(async resp => {
        if (!resp.ok) throw new Error('enqueueAction failed');
        let json = null;
        try { json = await resp.json(); } catch (e) { /* ignore */ }
        const canonicalId = (json && json.id) ? json.id : actionItem.id;
        const queued = JSON.parse(JSON.stringify(actionItem));
        queued.id = canonicalId;
        queued.synced = true;
        swimSetQueues[currentLane].push(queued);
        updateQueueDisplay();
        updatePacerButtons();
        setTimeout(reconcileQueueWithDevice, 200);
    }).catch(err => {
        console.log('Failed to enqueue rest on device, keeping local queue only');
        actionItem.synced = false;
        swimSetQueues[currentLane].push(actionItem);
        updateQueueDisplay();
        updatePacerButtons();
    });
}

// Enqueue a move action: target is 'start' or 'far'
function enqueueMoveAction(target) {
    const currentLane = currentSettings.currentLane;
    const actionItem = {
        id: Date.now(), // temporary optimistic id
        type: 'action',
        action: 'move',
        target: target,
        summary: (target === 'start') ? 'Move to starting wall' : 'Move to far wall'
    };
    fetch('/enqueueAction', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ lane: currentLane, action: actionItem })
    }).then(resp => {
        if (!resp.ok) throw new Error('enqueueAction failed');
        return resp.json().catch(() => null);
    }).then(json => {
        const canonicalId = (json && json.id) ? json.id : actionItem.id;
        const queued = JSON.parse(JSON.stringify(actionItem));
        queued.id = canonicalId;
        queued.synced = true;
        swimSetQueues[currentLane].push(queued);
        updateQueueDisplay();
        updatePacerButtons();
        setTimeout(reconcileQueueWithDevice, 200);
    }).catch(err => {
        console.log('Failed to enqueue move action on device, keeping local queue only');
        actionItem.synced = false;
        swimSetQueues[currentLane].push(actionItem);
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
    document.getElementById('configControls').style.display = 'block';
    document.getElementById('swimmerSet').style.display = 'none';

    // Switch back to config buttons
    document.getElementById('configButtons').style.display = 'block';
    // Keep queueButtons visible at all times so users can queue the current set
    // document.getElementById('queueButtons').style.display = 'block';
    document.getElementById('editButtons').style.display = 'none';
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
            const isActive = activeSwimSets[currentLane] && activeSwimSets[currentLane].id === swimSet.id;
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
            if (swimSet.type === 'rest' || swimSet.type === 'action') {
                const actionSummary = swimSet.summary || (swimSet.type === 'rest' ? `Rest: ${formatSecondsToMmSs(swimSet.seconds)}` : (swimSet.summary || 'Action'));
                const actionClass = 'style="background: #f5f5f5; border: 1px dashed #bbb; padding: 8px 10px; margin: 5px 0; border-radius: 4px;"';
                html += `
                    <div ${actionClass} class="queue-action-item">
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
            } else {
                statusClass = `style="background: ${backgroundColor}; border: 1px solid ${borderColor}; padding: 8px 10px; margin: 5px 0; border-radius: 4px; ${isCompleted ? 'opacity: 0.8;' : ''}"`;

                html += `
                    <div ${statusClass}>
                        <div style="display:flex; justify-content:space-between; align-items:center;">
                            <div style="font-size:12px; color:#28a745; margin-right:8px;">${swimSet.synced ? '✓ Synced' : ''}</div>
                        </div>
                        <div style="display:flex; justify-content:space-between; align-items:center;">
                            <div style="font-size:12px; color:#28a745; margin-right:8px;">${swimSet.synced ? '✓ Synced' : ''}</div>
                        </div>
                        <div style="display: flex; justify-content: space-between; align-items: center;">
                            <div style="font-weight: bold; color: ${isActive ? '#1976D2' : isCompleted ? '#28a745' : '#333'};">
                                ${swimSet.summary}
                            </div>
                            <div style="display: flex; gap: 5px;">
                                ${!isActive && !isCompleted ? `<button onclick="editSwimSet(${index})" style="padding: 2px 6px; font-size: 12px; background: #007bff; color: white; border: none; border-radius: 3px; cursor: pointer;">Edit</button>` : ''}
                                ${!isActive && !isCompleted ? `<button onclick="deleteSwimSet(${index})" style="padding: 2px 6px; font-size: 12px; background: #dc3545; color: white; border: none; border-radius: 3px; cursor: pointer;">Delete</button>` : ''}
                                ${isActive ? `<div style="font-weight: bold; color: #1976D2;">Total: <span id="elapsedTime">00:00</span></div>` : ''}
                                ${isCompleted ? `<div style="font-size: 12px; color: #28a745; font-weight: bold;">Completed</div>` : ''}
                            </div>
                        </div>
                        ${isActive ? `
                            <div style="margin-top: 8px; display: grid; grid-template-columns: 1fr 1fr; gap: 10px; font-size: 12px; color: #555;">
                                <div><strong>Round:</strong> <span id="currentRound">1</span> of <span id="totalRounds">10</span></div>
                                <div><strong>Round:</strong> <span id="roundTiming">0:00 / 0:00</span></div>
                                <div><strong>Active Swimmers:</strong> <span id="activeSwimmers">0</span></div>
                                <div><strong>Next Event:</strong> <span id="nextEvent">Starting...</span></div>
                            </div>
                            <div style="margin-top: 6px; background: #fff; border-radius: 3px; padding: 6px; font-size: 11px;">
                                <strong>Current Phase:</strong> <span id="currentPhase">Preparing to start</span>
                            </div>
                        ` : ''}
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

    // Switch to edit buttons (hide the create/config button group)
    document.getElementById('configButtons').style.display = 'none';
    document.getElementById('editButtons').style.display = 'block';
}

function loadSwimSetIntoConfig(swimSet) {
    // Restore settings
    Object.assign(currentSettings, swimSet.settings);

    // Set current lane
    currentSettings.currentLane = swimSet.lane;

    // Restore swimmer set
    setCurrentSwimmerSet(swimSet.swimmers);

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
    swimSetQueues[currentLane][editingSwimSetIndexes[currentLane]] = {
        ...swimSetQueues[currentLane][editingSwimSetIndexes[currentLane]],
        swimmers: currentSet,
        settings: { ...currentSettings },
        summary: generateSetSummary(currentSet, currentSettings)
    };

    editingSwimSetIndexes[currentLane] = -1;
    returnToConfigMode();
    updateQueueDisplay();
}

function cancelEdit() {
    const currentLane = currentSettings.currentLane;
    editingSwimSetIndexes[currentLane] = -1;
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

function startQueue() {
    const currentLane = currentSettings.currentLane;

    if (swimSetQueues[currentLane].length === 0) return;

    // Find the first non-completed swim set in current lane's queue
    const nextSwimSet = swimSetQueues[currentLane].find(set => !set.completed);
    if (!nextSwimSet) return; // No pending sets to start

    // Start the next non-completed swim set
    activeSwimSets[currentLane] = nextSwimSet;

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

    // Update displays
    updateQueueDisplay();
    updatePacerButtons();
}

function loadSwimSetForExecution(swimSet) {
    // Set the current lane
    currentSettings.currentLane = swimSet.lane;

    // Load settings
    Object.assign(currentSettings, swimSet.settings);

    // Update the DOM input fields with the swim set's settings
    if (swimSet.settings.paceTimeSeconds !== undefined) {
        const el = document.getElementById('paceTimeSeconds');
        if (el) el.value = formatSecondsToMmSs(swimSet.settings.paceTimeSeconds);
    }
    if (swimSet.settings.numRounds) {
        document.getElementById('numRounds').value = swimSet.settings.numRounds;
    }

    // Load swimmer set
    setCurrentSwimmerSet(swimSet.swimmers);

    // Update lane selector to reflect current lane
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

// Functions to communicate with ESP32
function sendStartCommand() {
    // Send only the specific fields that matter for starting the pacer.
    // Avoid issuing a broad, multi-endpoint write which may overwrite device state.

    // Geometry and LED mapping
    sendPoolLength(currentSettings.poolLength, currentSettings.poolLengthUnits);
    sendStripLength(currentSettings.stripLength);
    sendLedsPerMeter(currentSettings.ledsPerMeter);

    // Visual settings
    // sendBrightness expects a percent (0-100)
    try {
        const brightnessPercent = Math.round((currentSettings.brightness - 20) * 100 / (255 - 20));
        sendBrightness(brightnessPercent);
    } catch (e) {
        // ignore if conversion fails
    }
    sendPulseWidth(currentSettings.pulseWidth);

    // Lane and swimmer counts
    sendNumLanes(currentSettings.numLanes);
    sendNumSwimmers(currentSettings.numSwimmers);
    sendNumRounds(currentSettings.numRounds);

    // Pace-related
    sendPaceDistance(currentSettings.paceDistance);
    sendSpeed(currentSettings.speed);

    // Rest and swimmer interval defaults (help the device maintain consistent defaults)
    sendRestTime(currentSettings.restTime);
    sendSwimmerInterval(currentSettings.swimmerInterval);

    // Indicators and underwater config
    sendDelayIndicators(currentSettings.delayIndicatorsEnabled);
    sendUnderwaterSettings();

    // Color configuration
    if (currentSettings.colorMode === 'individual') {
        const set = getCurrentSwimmerSet();
        const colors = [];
        for (let i = 0; i < currentSettings.numSwimmers; i++) {
            if (set && set[i] && set[i].color) colors.push(set[i].color);
            else colors.push(currentSettings.swimmerColor);
        }
    sendSwimmerColors(colors.join(','));
    } else {
        sendSwimmerColor(currentSettings.swimmerColor);
    }

    // Finally, request the device to toggle/start. Small delay ensures prior posts are scheduled.
    setTimeout(() => {
        fetch('/toggle', { method: 'POST' })
        .then(response => response.text())
        .then(result => {
            console.log('Start command issued, server response:', result);
        })
        .catch(err => {
            console.log('Start command failed (standalone mode)');
        });
    }, 100);
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
    const paceDistance = currentSettings.paceDistance;
    const avgPace = currentSet.length > 0 ? currentSet[0].pace : parseTimeInput(document.getElementById('paceTimeSeconds').value);
    const restTime = currentSettings.restTime;
    const numRounds = currentSettings.numRounds;
    const laneName = currentSettings.laneNames[currentSettings.currentLane];

    const avgPaceDisplay = (avgPace > 60) ? formatSecondsToMmSs(avgPace) : `${Math.round(avgPace)}s`;
    setDetails.innerHTML = `${numRounds} x ${paceDistance}'s on the ${avgPaceDisplay} with ${restTime} sec rest`;

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

        // Format pace value for display: use M:SS when > 60s, else plain seconds
        const paceValueDisplay = (swimmer.pace > 60) ? formatSecondsToMmSs(swimmer.pace) : `${Math.round(swimmer.pace)}`;
        const paceUnitLabel = (swimmer.pace > 60) ? 'min:sec' : 'sec';

       row.innerHTML = `
          <div class="swimmer-color" style="background-color: ${displayColor}"
              onclick="cycleSwimmerColor(${index})" title="Click to change color"></div>
          <div class="swimmer-info">Swimmer ${swimmer.id}</div>
          <div class="swimmer-pace"> <input type="text" class="swimmer-pace-input" value="${paceValueDisplay}"
              placeholder="30 or 1:30" onchange="updateSwimmerPace(${index}, this.value)"> <span class="pace-unit">${paceUnitLabel}</span></div>
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

function updateSwimmerPace(swimmerIndex, newPace) {
    const currentSet = getCurrentSwimmerSet();
    if (currentSet[swimmerIndex]) {
        // parseTimeInput supports both seconds and M:SS formats
        const parsed = parseTimeInput(String(newPace));
        currentSet[swimmerIndex].pace = parsed;
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
    const paceSeconds = settings.paceTimeSeconds || parseTimeInput(document.getElementById('paceTimeSeconds').value);
    return {
        length: Number(settings.paceDistance || currentSettings.paceDistance),
        paceSeconds: Number(paceSeconds),
        rounds: Number(settings.numRounds || currentSettings.numRounds),
        restSeconds: Number(settings.restTime || currentSettings.restTime),
        type: 0,
        repeat: 0
    };
}

// Helper function to update all UI elements from settings
function updateAllUIFromSettings() {
    // Update input fields
    document.getElementById('numRounds').value = currentSettings.numRounds;
    document.getElementById('restTime').value = currentSettings.restTime;
    document.getElementById('numSwimmers').value = currentSettings.numSwimmers;
    document.getElementById('swimmerInterval').value = currentSettings.swimmerInterval;
    document.getElementById('paceDistance').value = currentSettings.paceDistance;
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
    setNumSwimmersUI(currentSettings.numSwimmers);
    setSwimmerIntervalUI(currentSettings.swimmerInterval);
    // update speed -> show human-friendly pace (seconds per selected distance)
    // Avoid inserting raw m/s into the pace input (it would be parsed as seconds).
    setPoolLengthUI(currentSettings.poolLength, currentSettings.poolLengthUnits);
    try {
        const paceInputEl = document.getElementById('paceTimeSeconds');
        const paceDistance = currentSettings.paceDistance || 50;
        const poolMeters = currentSettings.poolLengthUnits === 'yards' ? (paceDistance * 0.9144) : paceDistance;
        const paceSeconds = (currentSettings.speed && currentSettings.speed > 0) ? (poolMeters / currentSettings.speed) : 30;
        if (paceInputEl) paceInputEl.value = formatSecondsToMmSs(paceSeconds);
        // Recompute internals from the displayed pace (no save)
        updateFromPace();
        updatePaceDistance(false);
    } catch (e) {
        // fall back to safe defaults if DOM not ready
        const el = document.getElementById('paceTimeSeconds'); if (el) el.value = '0:30';
    }
    setBrightnessUI(Math.round((currentSettings.brightness - 20) * 100 / (255 - 20)));
    setFirstUnderwaterDistanceUI(currentSettings.firstUnderwaterDistance);
    setUnderwaterDistanceUI(currentSettings.underwaterDistance);
    setHideAfterUI(currentSettings.hideAfter);

    // Update visual selections
    updateVisualSelection();

    // Update lane selector
    updateLaneSelector();
}// Initialize queue display on page load
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
            const sig = `${Number(d.paceSeconds).toFixed(0)}|${d.restSeconds}|${d.rounds}|${Number(d.length).toFixed(0)}`;
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
            const sig = `${Math.round(local.settings.paceTimeSeconds)}|${local.settings.restTime}|${local.settings.numRounds}|${Number(local.settings.paceDistance).toFixed(0)}`;
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
document.addEventListener('DOMContentLoaded', function() {
    // Initialize all DOM-dependent functions
    updateCalculations(false);
    initializeBrightnessDisplay();
    updateLaneSelector();
    updateLaneNamesSection();
    updateStatus();

    // Ensure visual selection is applied after DOM is ready
    updateVisualSelection();
    initializeQueueSystem();
    // Attempt to fetch device settings from ESP32 and merge into UI defaults
    fetchDeviceSettingsAndApply();
    // Reconcile with device queue shortly after load
    setTimeout(reconcileQueueWithDevice, 300);
    // Start periodic reconciliation every 10s
    reconcileIntervalId = setInterval(reconcileQueueWithDevice, 10000);
    // Some browsers restore form control values after load (preserve user inputs).
    // To ensure labels stay in sync with actual slider values (which may be restored
    // by the browser after scripts run), schedule a short re-sync.
    setTimeout(syncRangeLabels, 60);
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
    const breadcrumb = document.getElementById('createBreadcrumb');
    const breadcrumbTab = document.getElementById('breadcrumbTab');
    const specialTab = document.getElementById('createSpecialTab');

    // Hide all specialized areas first
    const specialArea = document.getElementById('specialOptions');

    if (tabName === 'details') {
        detailsTab.classList.add('active');
        swimmersTab.classList.remove('active');
        if (specialTab) specialTab.classList.remove('active');
        document.getElementById('configControls').style.display = 'block';
        document.getElementById('swimmerSet').style.display = 'none';
        if (specialArea) specialArea.style.display = 'none';
        // Ensure Queue button visible
        const queueBtn = document.getElementById('queueBtn'); if (queueBtn) queueBtn.style.display = 'inline-block';
    } else {
        // Default to swimmers tab unless explicit 'special' requested
        if (tabName === 'special') {
            // Activate special tab
            detailsTab.classList.remove('active');
            swimmersTab.classList.remove('active');
            if (specialTab) specialTab.classList.add('active');
            document.getElementById('configControls').style.display = 'none';
            document.getElementById('swimmerSet').style.display = 'none';
            if (specialArea) specialArea.style.display = 'block';
            // Hide queue button when in special options
            const queueBtn = document.getElementById('queueBtn'); if (queueBtn) queueBtn.style.display = 'none';
        } else {
            detailsTab.classList.remove('active');
            swimmersTab.classList.add('active');
            if (specialTab) specialTab.classList.remove('active');
            document.getElementById('configControls').style.display = 'none';
            document.getElementById('swimmerSet').style.display = 'block';
            if (specialArea) specialArea.style.display = 'none';
            const queueBtn = document.getElementById('queueBtn'); if (queueBtn) queueBtn.style.display = 'inline-block';
            // Ensure a swimmer set exists based on current inputs so reloads that
            // restore the swimmers tab show the swimmer list immediately.
            try {
                createOrUpdateSwimmerSetFromConfig(false);
            } catch (e) {
                // Swallow errors to avoid breaking tab switch
                console.log('Failed to auto-create swimmer set on tab switch:', e);
            }
        }
    }

    try {
        if (!silent) localStorage.setItem('createTab', tabName);
    } catch (e) {}
}

// When the swim time input changes, update internal pace and reset per-swimmer paces
// so that swimmer customizations are ignored and all swimmers use the new pace.
document.addEventListener('DOMContentLoaded', function() {
    const paceEl = document.getElementById('paceTimeSeconds');
    if (paceEl) {
        paceEl.addEventListener('change', function() {
            // Update derived speed
            updateFromPace();
            // Rebuild the swimmer set and reset swimmer paces to the new value
            createOrUpdateSwimmerSetFromConfig(true);
        });
    }

    // Ensure switching to swimmers tab creates/updates the swimmer set automatically
    const detailsTabBtn = document.getElementById('createDetailsTab');
    const swimmersTabBtn = document.getElementById('createSwimmersTab');
    const specialTabBtn = document.getElementById('createSpecialTab');
    if (swimmersTabBtn) {
        swimmersTabBtn.addEventListener('click', function() {
            // Auto-create/update the set from current controls without resetting paces
            createOrUpdateSwimmerSetFromConfig(false);
        });
    }
    if (detailsTabBtn) {
        detailsTabBtn.addEventListener('click', function() {
            // No special action needed when going back to details
        });
    }
    if (specialTabBtn) {
        specialTabBtn.addEventListener('click', function() {
            // Nothing special to precompute for special options
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
        console.log("Received globalConfigSettings, underwaters:" + dev.underwatersEnabled);

        // Merge device settings into currentSettings with conversions
        if (dev.stripLengthMeters !== undefined) currentSettings.stripLength = parseFloat(dev.stripLengthMeters);
        if (dev.ledsPerMeter !== undefined) currentSettings.ledsPerMeter = parseInt(dev.ledsPerMeter);
        if (dev.numLanes !== undefined) currentSettings.numLanes = parseInt(dev.numLanes);
        if (dev.numSwimmers !== undefined) currentSettings.numSwimmers = parseInt(dev.numSwimmers);
        if (dev.poolLength !== undefined) currentSettings.poolLength = dev.poolLength;
        if (dev.poolLengthUnits !== undefined) currentSettings.poolLengthUnits = dev.poolLengthUnits;

        // Convert firmware speed (feet/sec) to m/s for client
        if (dev.speedMetersPerSecond !== undefined) {
            currentSettings.speed = parseFloat(dev.speedMetersPerSecond);
        }

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

function sendNumSwimmers(numSwimmers) {
    return postForm('/setNumSwimmers', `numSwimmers=${encodeURIComponent(numSwimmers)}`);
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

function sendSpeed(speed) {
    return postForm('/setSpeed', `speed=${encodeURIComponent(Number(speed))}`);
}

function sendPaceDistance(paceDistance) {
    return postForm('/setPaceDistance', `paceDistance=${encodeURIComponent(Number(paceDistance))}`);
}

function sendNumRounds(numRounds) {
    return postForm('/setNumRounds', `numRounds=${encodeURIComponent(Number(numRounds))}`);
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
    promises.push(sendNumSwimmers(currentSettings.numSwimmers));
    promises.push(sendNumRounds(currentSettings.numRounds));

    // Pace-related
    promises.push(sendPaceDistance(currentSettings.paceDistance));
    promises.push(sendSpeed(currentSettings.speed));

    // Rest and swimmer interval defaults
    promises.push(sendRestTime(currentSettings.restTime));
    promises.push(sendSwimmerInterval(currentSettings.swimmerInterval));

    // Indicators and underwater config
    promises.push(sendDelayIndicators(currentSettings.delayIndicatorsEnabled));
    promises.push(sendUnderwaterSettings());

    // Color configuration
    if (currentSettings.colorMode === 'individual') {
        const set = getCurrentSwimmerSet();
        const colors = [];
        for (let i = 0; i < currentSettings.numSwimmers; i++) {
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
