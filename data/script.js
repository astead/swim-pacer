// Detect if running in standalone mode (file:// protocol)
const isStandaloneMode = window.location.protocol === 'file:';

const max_lanes = 4;
const max_swimmers_per_lane = 10;
const default_swimmers_per_lane = 3;
let currentSettings = {
    numLanes: 2,
    currentLane: 0,
    laneNames: [
        'Lane 1',
        'Lane 2',
        'Lane 3',
        'Lane 4'],
    numSwimmersPerLane: [
        default_swimmers_per_lane,
        default_swimmers_per_lane,
        default_swimmers_per_lane,
        default_swimmers_per_lane],
    swimmerColors: [[],[],[],[]],
    brightness: 100,
    pulseWidth: 1.0,
    colorMode: 'individual',
    swimmerColor: '#0000ff',

    poolLength: 25,
    poolLengthUnits: 'yards',
    stripLengthMeters: 23,  // 75 feet = 23 meters
    ledsPerMeter: 30,
    numLedStrips: [1,1,1,1],
    gapBetweenStrips: 23,

    numRounds: 10,
    swimDistance: 50,
    swimTime: 30,
    restTime: 5,
    swimmerInterval: 4,

    delayIndicatorsEnabled: true,
    underwatersEnabled: false,
    lightSize: 1.0,
    firstUnderwaterDistance: 20,
    underwaterDistance: 20,
    hideAfter: 1,
    underwaterColor: '#0000ff',
    surfaceColor: '#00ff00',
};

// Swimmer set configuration - now lane-specific
let laneRunning = [false, false, false, false]; // Running state for each lane
let currentSwimmerIndex = -1; // Track which swimmer is being edited

// Enum for swim set status bit mask
const SWIMSET_STATUS_PENDING    = 0x0 << 0;
const SWIMSET_STATUS_SYNCHED    = 0x1 << 0;
const SWIMSET_STATUS_ACTIVE     = 0x1 << 1;
const SWIMSET_STATUS_COMPLETED  = 0x1 << 2;
const SWIMSET_STATUS_STOPPED    = 0x1 << 3;

// swim set type constants (match server enum)
const SWIMSET_TYPE = 0;
const REST_TYPE = 1;
const LOOP_TYPE = 2;
const MOVE_TYPE = 3;

const ENQUEUE_RETRY_MS = 15000; // wait 15s before retrying a pending enqueue

// Swim Set Queue Management - now lane-specific
let swimSetQueues = [[], [], [], []]; // Array of queued swim sets for each lane
let editingSwimSetIndexes = [-1, -1, -1, -1]; // Index of swim set being edited for each lane (-1 if creating new)

// single persistent working swim set info (keeps current UI values / edits)
let workingSwimSetInfo = null; // never cleared; updated from UI, edits, or device values

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

function timeLabelFromString(timeStr) {
    if (timeStr === undefined || timeStr === null) return "min:sec or sec";
    timeStr = String(timeStr).trim();
    if (timeStr.includes(':')) return "min:sec";
    if (parseFloat(timeStr) == 1) return "second";
    return "seconds";
}

function updateNumLedStrips(lane, value) {
    currentSettings.numLedStrips[lane] = value;
    sendNumLedStrips(lane, value);
}

function updateCalculations(triggerSave = true) {
    const poolLengthElement = document.getElementById('poolLength').value;
    const poolLength = parseFloat(poolLengthElement.split(' ')[0]);
    const poolLengthUnits = poolLengthElement.includes('m') ? 'meters' : 'yards';
    const stripLengthMeters = parseFloat(document.getElementById('stripLengthMeters').value);
    const ledsPerMeter = parseInt(document.getElementById('ledsPerMeter').value);
    const gapBetweenStrips = parseInt(document.getElementById('gapBetweenStrips').value);

    // Update current settings
    currentSettings.poolLength = poolLength;
    currentSettings.poolLengthUnits = poolLengthUnits;
    currentSettings.stripLengthMeters = stripLengthMeters;
    currentSettings.ledsPerMeter = ledsPerMeter;
    currentSettings.gapBetweenStrips = gapBetweenStrips;
    document.getElementById('distanceUnits').textContent = poolLengthUnits;

    // If caller requested saving, call the targeted send helpers so the
    // server only receives the changed values.
    if (triggerSave) {
        sendPoolLength(currentSettings.poolLength, currentSettings.poolLengthUnits);
        sendStripLength(currentSettings.stripLengthMeters);
        sendLedsPerMeter(currentSettings.ledsPerMeter);
        sendGapBetweenStrips(currentSettings.gapBetweenStrips);
    }
}

function updateNumLanes() {
    const numLanes = parseInt(document.getElementById('numLanes').value);
    currentSettings.numLanes = numLanes;
    console.log('updateNumLanes: calling updateLaneSelector after setting numLanes to what was selected in UI: ' + numLanes);
    updateLaneSelector();
    updateNumLedStripsSection();
    updateLaneNamesSection();
    sendNumLanes(numLanes);
}

async function updateCurrentLane() {
    const newLane = parseInt(document.getElementById('currentLane').value);
    console.log('updateCurrentLane: switching lanes: ' +
        currentSettings.currentLane + ' -> ' + newLane);
    currentSettings.currentLane = newLane;
    // Update workingSwimSetInfo lane so queued sets go to the correct lane
    if (workingSwimSetInfo) {
        workingSwimSetInfo.lane = newLane;
    }

    // What else should get triggered, maybe the swim set queue?
    // Update any UI elements that were lane dependent?
    //  num swimmers, num LED strips
    updateStatus();
    updateQueueDisplay();
    console.log('updateCurrentLane: Setting switching numSwimmers to numSwimmersPerLane[' + newLane + ']:' +
        currentSettings.numSwimmersPerLane[newLane]);
    document.getElementById('numSwimmers').value =
        currentSettings.numSwimmersPerLane[newLane];
    updatePacerButtons();

    // Immediately refresh the queue for the newly selected lane so the UI
    // reflects the device state without waiting for the periodic reconcile.
    if (!isStandaloneMode) {
        const queueListEl = document.getElementById('queueList');
        try {
            if (queueListEl) queueListEl.classList.add('loading'); // optional visual cue
            await reconcileQueueWithDevice();
        } catch (e) {
            console.warn('updateCurrentLane: reconcileQueueWithDevice failed', e);
        } finally {
            if (queueListEl) queueListEl.classList.remove('loading');
        }
    }
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

function updateNumLedStripsSection() {
    const numLedStripsList = document.getElementById('numLedStripsList');
    numLedStripsList.innerHTML = '';

    for (let i = 0; i < currentSettings.numLanes; i++) {
        const laneDiv = document.createElement('div');
        laneDiv.style.cssText = 'margin: 8px 0; display: flex; align-items: center; gap: 8px;';
        const label = document.createElement('span');
        label.style.cssText="margin:0; white-space:nowrap;";
        label.textContent = currentSettings.laneNames[i] + ":";

        const input = document.createElement('input');
        input.type = 'number';
        input.value = currentSettings.numLedStrips[i];
        input.min = 1;
        input.max=10;
        input.style.className="compact-input";
        input.style.cssText="width: 80px;";
        input.onchange = function() {
            updateNumLedStrips(i, this.value);
        };

        laneDiv.appendChild(label);
        laneDiv.appendChild(input);
        numLedStripsList.appendChild(laneDiv);
    }
}

function updateLaneNamesSection() {
    const laneNamesList = document.getElementById('laneNamesList');
    laneNamesList.innerHTML = '';

    for (let i = 0; i < currentSettings.numLanes; i++) {
        const laneDiv = document.createElement('div');
        laneDiv.style.cssText = 'margin: 8px 0; display: flex; align-items: center; gap: 8px;';

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

function setIndividualSwimmerColors() {
    console.log('setIndividualSwimmerColors called');
    // Return array of swimmer colors for current lane
    // This should be an array of length numSwimmersPerLane[currentLane]
    const lane = currentSettings.currentLane;
    currentSettings.swimmerColors[lane] = [];
    let currentSet = currentSettings.swimmerColors[lane];

    // pre-populate with swimmer config info:
    // Fill with default swimmer color based on swimmer index and color mode
    if (currentSettings.colorMode === 'individual') {
        for (let i = 0; i < currentSettings.numSwimmersPerLane[lane]; i++) {
            currentSet.push({
                color: colorHex[swimmerColors[i % swimmerColors.length]],
            });
        }
    } else {
        for (let i = 0; i < currentSettings.numSwimmersPerLane[lane]; i++) {
            currentSet.push({
                color: currentSettings.swimmerColor,
            });
        }
    }
}

function updateIndividualSwimmerColorDueToNumSwimmers() {
    // number of swimmers for this lane may have changed; reset individual swimmer colors
    const lane = currentSettings.currentLane;
    if (!currentSettings.swimmerColors[lane]) return;
    if (!Array.isArray(currentSettings.swimmerColors[lane])) return;
    // Do we have more or less swimmers now?
    const currentNumSwimmers = currentSettings.swimmerColors[lane].length;
    const desiredNumSwimmers = currentSettings.numSwimmersPerLane[lane];
    if (desiredNumSwimmers < currentNumSwimmers) {
        console.log('Truncating swimmer colors for lane', lane, 'from', currentNumSwimmers, 'to', desiredNumSwimmers);
        // Truncate the array
        currentSettings.swimmerColors[lane] = currentSettings.swimmerColors[lane].slice(0, desiredNumSwimmers);
    } else if (desiredNumSwimmers > currentNumSwimmers) {
        console.log('Adding swimmer colors for lane', lane, 'from', currentNumSwimmers, 'to', desiredNumSwimmers);
        // Add new swimmers with default color
        for (let i = currentNumSwimmers; i < desiredNumSwimmers; i++) {
            let color;
            if (currentSettings.colorMode === 'individual') {
                color = colorHex[swimmerColors[i % swimmerColors.length]];
            } else {
                color = currentSettings.swimmerColor;
            }
            currentSettings.swimmerColors[lane].push({ color: color });
        }
    }
}

function setIndividualSwimmerTime(swimmerIndex, swimTime) {
    const lane = currentSettings.currentLane;
    if (!workingSwimSetInfo) createOrUpdateFromConfig();
    // if swimmerIndex is -1, set all swimmers; otherwise set specific swimmer
    if (swimmerIndex === -1) {
        const swimmers = [];
        for (let i = 0; i < currentSettings.numSwimmersPerLane[lane]; i++) swimmers.push({ swimTime: swimTime });
        workingSwimSetInfo.swimmers = swimmers;
    } else if (workingSwimSetInfo && workingSwimSetInfo.swimmers && workingSwimSetInfo.swimmers[swimmerIndex]) {
        workingSwimSetInfo.swimmers[swimmerIndex].swimTime = swimTime;
    }
}

function updateIndividualSwimmerTimeDueToNumSwimmers() {
    // number of swimmers for this lane may have changed; reset individual swimmer swim times
    const lane = currentSettings.currentLane;
    if (!workingSwimSetInfo) createOrUpdateFromConfig();
    if (!workingSwimSetInfo || !workingSwimSetInfo.swimmers) return;
    // Do we have more or less swimmers now?
    const currentNumSwimmers = workingSwimSetInfo.swimmers.length;
    const desiredNumSwimmers = currentSettings.numSwimmersPerLane[lane];
    if (desiredNumSwimmers < currentNumSwimmers) {
        // Truncate the array
        workingSwimSetInfo.swimmers = workingSwimSetInfo.swimmers.slice(0, desiredNumSwimmers);
    } else if (desiredNumSwimmers > currentNumSwimmers) {
        // Add new swimmers with default swimTime
        for (let i = currentNumSwimmers; i < desiredNumSwimmers; i++) {
            workingSwimSetInfo.swimmers.push({ swimTime: workingSwimSetInfo.swimTime });
        }
    }
}

function updateNumRounds() {
    const numRounds = document.getElementById('numRounds').value;
    currentSettings.numRounds = parseInt(numRounds);
    if (!workingSwimSetInfo) createOrUpdateFromConfig();
    workingSwimSetInfo.numRounds = Number(numRounds);
    // Also send num rounds to device so it can be used as a default for swim-sets
    sendNumRounds(currentSettings.numRounds).catch(err => {
        console.debug('sendNumRounds failed (ignored):', err && err.message ? err.message : err);
    });
}

function updateSwimDistance() {
    const swimDistance = parseInt(document.getElementById('swimDistance').value);
    currentSettings.swimDistance = swimDistance;
    if (!workingSwimSetInfo) createOrUpdateFromConfig();
    workingSwimSetInfo.swimDistance = Number(swimDistance);
    // Save only the swim distance
    sendSwimDistance(currentSettings.swimDistance).catch(err => {
        console.debug('sendSwimDistance failed (ignored):', err && err.message ? err.message : err);
    });
}

function updateSwimTime() {
    const swimTime = document.getElementById('swimTime').value;
    currentSettings.swimTime = parseTimeInput(swimTime);
    document.getElementById('swimTimeLabel').textContent = timeLabelFromString(swimTime);
    if (!workingSwimSetInfo) createOrUpdateFromConfig();
    workingSwimSetInfo.swimTime = Number(currentSettings.swimTime);
    // Also send swim time to device so it can be used as a default for swim-sets
    sendSwimTime(currentSettings.swimTime).catch(err => {
        console.debug('sendSwimTime failed (ignored):', err && err.message ? err.message : err);
    });
    // Reset invidivual swimmmer swim times in current set
    setIndividualSwimmerTime(-1, currentSettings.swimTime);
}

function updateRestTime() {
    const restTime = document.getElementById('restTime').value;
    currentSettings.restTime = parseTimeInput(restTime);
    document.getElementById('restTimeLabel').textContent = timeLabelFromString(restTime);
    if (!workingSwimSetInfo) createOrUpdateFromConfig();
    workingSwimSetInfo.restTime = Number(currentSettings.restTime);
    // Also send rest time to device so it can be used as a default for swim-sets
    sendRestTime(currentSettings.restTime).catch(err => {
        console.debug('sendRestTime failed (ignored):', err && err.message ? err.message : err);
    });
}

function updateSwimmerInterval() {
    const swimmerInterval = document.getElementById('swimmerInterval').value;
    currentSettings.swimmerInterval = parseTimeInput(swimmerInterval);
    document.getElementById('swimmerIntervalLabel').textContent = timeLabelFromString(swimmerInterval);
    if (!workingSwimSetInfo) createOrUpdateFromConfig();
    workingSwimSetInfo.swimmerInterval = Number(currentSettings.swimmerInterval);
    // Also send swimmer interval to device so it can be used as a default for swim-sets
    sendSwimmerInterval(currentSettings.swimmerInterval).catch(err => {
        console.debug('sendSwimmerInterval failed (ignored):', err && err.message ? err.message : err);
    });
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

    console.log(`Underwaters: enabledArg:  ${enabledArg}, enabled: ${enabled}, triggerSave: ${triggerSave}`);

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
    sendUnderwaterSettings();
}

function updateFirstUnderwaterDistance() {
    const distance = document.getElementById('firstUnderwaterDistance').value;
    currentSettings.firstUnderwaterDistance = parseInt(distance);
    sendUnderwaterSettings();
}

function updateUnderwaterDistance() {
    const distance = document.getElementById('underwaterDistance').value;
    currentSettings.underwaterDistance = parseInt(distance);
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
    console.log(`updateNumSwimmers: Updating numSwimmersPerLane variable for lane ${lane} after changing drop down to ${numSwimmers}`);

    currentSettings.numSwimmersPerLane[lane] = numSwimmers;
    updateIndividualSwimmerColorDueToNumSwimmers();
    updateIndividualSwimmerTimeDueToNumSwimmers();
    displaySwimmerSet();
    sendNumSwimmers(lane, numSwimmers);
}

function updateColorMode() {
    const colorMode = document.querySelector('input[name="colorMode"]:checked').value;
    currentSettings.colorMode = colorMode;
    updateVisualSelection();
    setIndividualSwimmerColors();
    displaySwimmerSet();

    // Only send color mode change; avoid broad writes that would reset swimmers
    if (!isStandaloneMode) {
        // Use centralized postForm helper (handles standalone mode, suppress, timeout)
        sendColorMode(colorMode);
    }

    // When switching to same color mode, update any existing swimmers
    if (colorMode === 'same') {
        const lane = currentSettings.currentLane || 0;
        const currentSet = currentSettings.swimmerColors[lane];
        if (Array.isArray(currentSet)) {
            currentSet.forEach((swimmer) => {
                swimmer.color = currentSettings.swimmerColor;
            });
        }
    } else {
        // When switching to individual colors, reset swimmers to default colors
        const lane = currentSettings.currentLane || 0;
        const currentSet = currentSettings.swimmerColors[lane];
        if (Array.isArray(currentSet)) {
            currentSet.forEach((swimmer, index) => {
                swimmer.color = colorHex[swimmerColors[index % swimmerColors.length]];
            });
        }
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
    const lane = currentSettings.currentLane;
    if (currentSwimmerIndex >= 0) {
        // Updating individual swimmer color
        const currentColorSet = currentSettings.swimmerColors[lane];
        // Defensive programming: ensure we don't accidentally modify all swimmers
        if (currentSwimmerIndex < currentColorSet.length) {
            // Store the hex color directly to avoid shared reference issues
            currentColorSet[currentSwimmerIndex].color = color;
            console.log(`Updated swimmer ${currentSwimmerIndex} in ${currentSettings.laneNames[lane]} color to ${color}`);
            displaySwimmerSet();
            // Build CSV hex of colors for server
            const colorsCSV = currentColorSet.map(s => s.color).join(',');
            sendSwimmerColors(lane, colorsCSV);
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
        sendSwimmerColor(currentSettings.swimmerColor);
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
    sendSwimmerColor(swimmerColor);
}

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

function createOrUpdateFromConfig() {
    // Build a working swim set object from currentSettings and keep it persistent
    const lane = currentSettings.currentLane;
    const individualSwimTimes = [];
    for (let i = 0; i < currentSettings.numSwimmersPerLane[lane]; i++) {
        individualSwimTimes.push({ swimTime: currentSettings.swimTime });
    }

    workingSwimSetInfo = {
        lane: lane,
        swimmers: JSON.parse(JSON.stringify(individualSwimTimes)),
        numRounds: Number(currentSettings.numRounds),
        swimDistance: Number(currentSettings.swimDistance),
        swimTime: Number(currentSettings.swimTime),
        restTime: Number(currentSettings.restTime),
        swimmerInterval: Number(currentSettings.swimmerInterval),
        summary: "",
        // keep uniqueId if already present (client may assign later)
        uniqueId: workingSwimSetInfo && workingSwimSetInfo.uniqueId ? workingSwimSetInfo.uniqueId : undefined,
        type: 0,
        repeatRemaining: 0
    };
    console.log('createOrUpdateFromConfig -> workingSwimSetInfo updated:', workingSwimSetInfo);
}

// Resize data for a single lane to match device-reported swimmer count
function migrateSwimmerCountsToDeviceForLane(lane, deviceNumSwimmers) {
    if (lane < 0 || lane >= max_lanes) {
        console.log(`migrateSwimmerCountsToDeviceForLane: invalid lane ${lane}`);
        return;
    }
    if (!deviceNumSwimmers || deviceNumSwimmers < 1) {
        console.log(`migrateSwimmerCountsToDeviceForLane: invalid deviceNumSwimmers ${deviceNumSwimmers} for lane ${lane}`);
        return;
    }
    console.log(`migrateSwimmerCountsToDeviceForLane: migrating (from device) numSwimmersPerLane ${lane} to ${deviceNumSwimmers}`);
    // Record per-lane count in settings
    currentSettings.numSwimmersPerLane[lane] = deviceNumSwimmers;

    // If this is the currently selected lane, update UI controls
    if (currentSettings.currentLane === lane) {
        try {
            console.log(`Updating numSwimmersPerLane for lane ${lane} after migrating swimmer count from device`);
            document.getElementById('numSwimmers').value = deviceNumSwimmers;
            // Update swimmerColors array length for current lane
            updateIndividualSwimmerColorDueToNumSwimmers();
            // Update workingSwimSetInfo swimmers array length
            updateIndividualSwimmerTimeDueToNumSwimmers();
            displaySwimmerSet();
            updateQueueDisplay();
        } catch (e) {
            console.log('Failed to update UI after migrating swimmer count from device:', e);
        }
    } else {
        console.log(`Not updating UI controls for lane ${lane} since current lane is ${currentSettings.currentLane}`);
    }
}


function generateSetSummary(settings) {
    console.log('Generating set summary with settings:', settings);
    const swimDistance = settings.swimDistance;
    const swimTime = settings.swimTime;
    const restTime = settings.restTime;
    const numRounds = settings.numRounds;
    const lane = settings.lane;

    // Treat missing type as a regular swim set (common for optimistic/local objects)
    const type = (typeof settings.type === 'undefined') ? SWIMSET_TYPE : settings.type;

    if (type == SWIMSET_TYPE) {
        // distance label: use "50's" for plural numRounds, "50'" for a single round (no "'s")
        const distanceLabel = (numRounds === 1) ? `${swimDistance}` : `${swimDistance}'s`;

        // Display swim/rest time using compact formatting: 'Ns' for <=60s, 'M:SS' for >60s
        const SwimTimeDisplay = formatCompactTime(swimTime);
        const restDisplay = formatCompactTime(restTime);

        return `${numRounds} x ${distanceLabel} on the ${SwimTimeDisplay} with ${restDisplay} rest`;
    }
    if (type == LOOP_TYPE) {
        // Try to resolve the loop start in the current queue using the stable uniqueId.
        let ll = '?';
        if (settings.loopFromUniqueId) {
            const q = swimSetQueues[lane] || [];
            const startIdx = q.findIndex(it => normalizeUniqueId(it.uniqueId) === normalizeUniqueId(settings.loopFromUniqueId));
            if (startIdx !== -1) {
                // Try to find the loop entry position in the same queue; if present compute block length,
                // otherwise assume the loop covers from startIdx to the end of the queue.
                const loopIdx = q.findIndex(it => normalizeUniqueId(it.uniqueId) === normalizeUniqueId(settings.uniqueId));
                if (loopIdx !== -1 && loopIdx > startIdx) {
                    ll = Math.max(0, loopIdx - startIdx);
                } else {
                    ll = Math.max(0, q.length - startIdx);
                }
            }
            return `Repeat last ${ll} set(s) x ${numRounds}`;
        } else {
            return `Repeat some previous sets x ${numRounds}`;
        }
    }
    return '';
}

// Helper: generate a compact hex uniqueId (default 16 hex chars, no '0x')
function generateUniqueId(len = 16) {
    // start with time component for some ordering
    let s = Date.now().toString(16);
    try {
        // append crypto-quality random bytes when available
        const need = Math.ceil((len - s.length) / 2);
        if (need > 0 && window && window.crypto && window.crypto.getRandomValues) {
            const rnd = new Uint8Array(need);
            window.crypto.getRandomValues(rnd);
            const rndHex = Array.from(rnd).map(b => b.toString(16).padStart(2, '0')).join('');
            s += rndHex;
        } else {
            // fallback to Math.random
            while (s.length < len) s += Math.floor(Math.random() * 0xFFFFFFFF).toString(16);
        }
    } catch (e) {
        // fallback safe path
        while (s.length < len) s += Math.floor(Math.random() * 0xFFFFFFFF).toString(16);
    }
    s = s.padEnd(len, '0').slice(0, len);
    return s.toLowerCase();
}

function normalizeUniqueId(v) {
    if (v === undefined || v === null) return '';
    let s = String(v).toLowerCase().trim();
    if (s.startsWith('0x')) s = s.slice(2);
    // allow only hex chars (optional): strip non-hex
    s = s.replace(/[^0-9a-f]/g, '');
    return s;
}

function shouldRetryEnqueue(local) {
    if (!local) return true;
    if (!local.enqueuePending) return true;
    if (!local.enqueueRequestedAt) return true;
    return (Date.now() - local.enqueueRequestedAt) >= ENQUEUE_RETRY_MS;
}

// central helper to send enqueue requests and update local optimistic entry state
async function sendEnqueuePayload(payload, local) {
    // payload: object to POST to /enqueueSwimSet
    // local: local optimistic entry in swimSetQueues[lane] that should be updated
    if (isStandaloneMode) {
        // nothing to do in standalone mode
        return { suppressed: true };
    }

    try {
        const resp = await fetch('/enqueueSwimSet', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });

        if (!resp.ok) {
            if (resp.errorCode) {
                switch (resp.errorCode) {
                    case 1:
                        console.log('sendEnqueuePayload ERROR from /enqueueSwimSet: Swim set already exists on device.');
                        if (local) {
                            local.synced = true;
                            local.enqueuePending = false;
                            updateQueueDisplay();
                            return { ok: true };
                        }
                        break;
                    case 2:
                        console.log('sendEnqueuePayload ERROR from /enqueueSwimSet: Device queue is full; cannot enqueue swim set.');
                        break;
                    case 3:
                        console.log('sendEnqueuePayload ERROR from /enqueueSwimSet: Invalid lane.');
                        break;
                    default:
                        console.log('sendEnqueuePayload ERROR from /enqueueSwimSet: Failed to enqueue swim set on device with error code:', resp.errorCode);
                        break;
                }
            } else {
                console.log('sendEnqueuePayload ERROR from /enqueueSwimSet: No error code, uniqueId:', payload.uniqueId);
            }
            // keep local marked pending so retry logic can resend later
            if (local) {
                local.enqueuePending = true;
                local.enqueueRequestedAt = Date.now();
                local.synced = false;
                updateQueueDisplay();
            }
            return { ok: false, status: resp.status };
        }

        const j = await resp.json().catch(()=>null);
        if (local && j && j.uniqueId) local.uniqueId = String(j.uniqueId).toLowerCase();
        if (local) {
            local.enqueuePending = false;
            local.synced = true;
            updateQueueDisplay();
        }
        return { ok: true, json: j };
    } catch (err) {
        console.warn('sendEnqueuePayload: network error enqueueing', payload.uniqueId, err);
        if (local) {
            local.enqueuePending = true;
            local.enqueueRequestedAt = Date.now();
            local.synced = false;
            updateQueueDisplay();
        }
        return { ok: false, error: err };
    }
}

function queueSwimSet() {
    const lane = currentSettings.currentLane;

    // Ensure we have a working swim set based on current inputs. If user is on the
    // Swimmer Customizations tab but hasn't explicitly created a set, build it
    // from the current controls so we can queue what they're seeing.
    try {
        if (!workingSwimSetInfo) {
            console.log('WARNING: No working swim set found, creating from current config');
            createOrUpdateFromConfig();
        }
    } catch (e) {
        console.log('ERROR: Failed to create working swim set before queueing:', e);
    }

    if (!workingSwimSetInfo) {
        alert('ERROR: No set to queue');
        return;
    }

    // Build minimal payload and send to device queue. Include lane so device
    // can enqueue into the correct per-lane queue.
    workingSwimSetInfo.uniqueId = generateUniqueId(16);
    workingSwimSetInfo.summary = generateSetSummary(workingSwimSetInfo);
    console.log('queueSwimSet: buildMinimalSwimSetPayload:', workingSwimSetInfo);
    const payload = buildMinimalSwimSetPayload(workingSwimSetInfo);
    const local = {...payload};

    // optimistic local entry creation (or find existing local entry)
    local.synced = false;
    local.enqueuePending = true;
    local.enqueueRequestedAt = Date.now();

    // push into local queue UI/state as you already do
    swimSetQueues[lane] = swimSetQueues[lane] || [];
    swimSetQueues[lane].push(local);
    updateQueueDisplay();
    updatePacerButtons();

    // Send using centralized helper
    sendEnqueuePayload(payload, local).then(result => {
        if (!result.ok && !result.duplicate) {
            // Optionally inspect result.status to provide finer UX messages
            console.log('queueSwimSet: enqueue request returned not-ok', result.status || result.error);
        }
    });
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

    // Add single Save/Load controls at top (for the whole queue)
    const controlsDiv = document.createElement('div');
    controlsDiv.style.cssText = 'display:flex; gap:8px; margin-bottom:8px;';
    const saveBtn = document.createElement('button');
    saveBtn.textContent = 'Save Queue';
    saveBtn.onclick = () => openSaveDialog();
    const loadBtn = document.createElement('button');
    loadBtn.textContent = 'Load Queue';
    loadBtn.onclick = () => openLoadDialog();
    controlsDiv.appendChild(saveBtn);
    controlsDiv.appendChild(loadBtn);
    listEl.appendChild(controlsDiv);

    if (queue.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'queue-empty';
        empty.textContent = 'Queue is empty';
        listEl.appendChild(empty);
        return;
    }
    // Render each queue entry
    queue.forEach((entry, idx) => {
        const row = document.createElement('div');
        row.className = 'queue-item';
        // Ensure queue row is a horizontal flex container so left + actions align
        row.style.display = 'flex';
        row.style.alignItems = 'center';
        row.style.justifyContent = 'space-between';

        // Left area wrapper so spacing/layout remains predictable
        const leftWrap = document.createElement('div');
        leftWrap.className = 'queue-left';
        // Allow left area to flex and shrink so the actions remain visible
        leftWrap.style.display = 'flex';
        leftWrap.style.alignItems = 'center';
        leftWrap.style.flex = '1 1 auto';
        leftWrap.style.minWidth = '0'; // required for overflow to work inside flex item

        // Status is a number (bitmask)
        const statusVal = entry.status;
        const isCompleted = !!(statusVal & SWIMSET_STATUS_COMPLETED);
        const isActive = !!(statusVal & SWIMSET_STATUS_ACTIVE);
        const isDeletedPending = !!entry.deletedPending;
        const isStopped = !!(statusVal & SWIMSET_STATUS_STOPPED);

        // Normal label generation (reuse previous logic but inside leftWrap)
        const label = document.createElement('span');
        label.className = 'queue-label';
        // Prevent label from pushing action buttons off-screen; ellipsize long text
        label.style.overflow = 'hidden';
        label.style.textOverflow = 'ellipsis';
        label.style.whiteSpace = 'nowrap';
        label.style.display = 'inline-block';
        label.style.minWidth = '0';
        // Special case for LOOP_TYPE entries so they appear descriptive
        if (entry.type === LOOP_TYPE) {
            // Try to resolve the loop start in the current queue using the stable uniqueId.
            let ll = '?';
            if (entry.loopFromUniqueId) {
                const q = swimSetQueues[lane] || [];
                const startIdx = q.findIndex(it => normalizeUniqueId(it.uniqueId) === normalizeUniqueId(entry.loopFromUniqueId));
                if (startIdx !== -1) {
                    // loop entry is at current position 'idx'; block length is items between startIdx and idx (exclusive)
                    ll = Math.max(0, idx - startIdx);
                }
            }
            const iters = entry.numRounds || 0;
            label.textContent = entry.summary || `Repeat last ${ll} set(s) x ${iters}`;
        } else {
            label.textContent = entry.summary || (`${entry.numRounds} x ${entry.swimDistance}'s`);
        }
        leftWrap.appendChild(label);

        // Build rest of row (reuse existing actions code but moved to be flexible)
        const actions = document.createElement('span');
        actions.className = 'queue-actions';
        // Keep actions fixed (no shrinking) and laid out horizontally
        actions.style.display = 'flex';
        actions.style.gap = '8px';
        actions.style.flex = '0 0 auto';
        actions.style.marginLeft = '12px';

        // Completed
        if (isCompleted && !isActive) {
            row.classList.add('completed');
            const info = document.createElement('span');
            info.className = 'queue-info';
            info.textContent = 'Completed';
            leftWrap.appendChild(info);

            const removeBtn = document.createElement('button');
            removeBtn.textContent = 'Remove';
            removeBtn.onclick = () => { deleteSwimSet(idx); };
            actions.appendChild(removeBtn);

            row.appendChild(leftWrap);
            row.appendChild(actions);
            listEl.appendChild(row);
            return;
        }

        // Active / stopped / pending-delete / normal branches follow the same structure
        // (for brevity keep the rest of your existing action buttons)
        if (isActive) {
            row.classList.add('running');
            const statusSpan = document.createElement('span');
            statusSpan.className = 'queue-status';
            let roundText = (entry.currentRound !== undefined) ? `${entry.currentRound} / ${entry.numRounds}` : 'In progress';
            if (entry.type === LOOP_TYPE) {
                roundText = `${(entry.numRounds - entry.repeatRemaining)} / ${entry.numRounds}`;
            }
            statusSpan.textContent = roundText;
            leftWrap.appendChild(statusSpan);

            row.appendChild(leftWrap);
            listEl.appendChild(row);
            return;
        }

        // Stopped
        if (isStopped) {
            row.classList.add('stopped');
            const statusSpan = document.createElement('span');
            statusSpan.className = 'queue-status';
            const roundText = (entry.currentRound !== undefined) ? `${entry.currentRound} / ${entry.numRounds}` : '';
            statusSpan.textContent = roundText;
            leftWrap.appendChild(statusSpan);

            const editBtn = document.createElement('button');
            editBtn.textContent = 'Edit';
            editBtn.onclick = () => { editSwimSet(idx); };
            actions.appendChild(editBtn);

            const runBtn = document.createElement('button');
            runBtn.textContent = 'Run';
            runBtn.onclick = () => { startSwimSet(idx); };
            actions.appendChild(runBtn);

            const delBtn = document.createElement('button');
            delBtn.textContent = 'Delete';
            delBtn.onclick = () => { deleteSwimSet(idx); };
            actions.appendChild(delBtn);

            row.appendChild(leftWrap);
            row.appendChild(actions);
            listEl.appendChild(row);
            return;
        }

        // If this entry is marked as pending deletion, render as such (but keep visible).
        if (entry.deletedPending) {
            row.classList.add('pending-delete');
            const status = document.createElement('span');
            status.className = 'queue-status';
            status.textContent = 'Deleting...';
            leftWrap.appendChild(status);

            const disabledBtn = document.createElement('button');
            disabledBtn.textContent = 'Delete';
            disabledBtn.disabled = true;
            actions.appendChild(disabledBtn);

            row.appendChild(leftWrap);
            row.appendChild(actions);
            listEl.appendChild(row);
            return;
        }

        // Normal row: Edit / Run / Delete
        const editBtn = document.createElement('button');
        editBtn.textContent = 'Edit';
        editBtn.onclick = () => { editSwimSet(idx); };
        actions.appendChild(editBtn);

        const runBtn = document.createElement('button');
        runBtn.textContent = 'Run';
        runBtn.onclick = () => { startSwimSet(idx); };
        actions.appendChild(runBtn);

        const delBtn = document.createElement('button');
        delBtn.textContent = 'Delete';
        delBtn.onclick = () => { deleteSwimSet(idx); };
        actions.appendChild(delBtn);

        row.appendChild(leftWrap);
        row.appendChild(actions);
        listEl.appendChild(row);
    });

    // Save/Load helper functions (placed near queue rendering)
    window.openSaveDialog = function() {
        const lane = getCurrentLaneFromUI();
        const name = prompt('Enter a name for this save (alphanumeric, dashes allowed):', 'my_save');
        if (!name) return;
        doSaveQueue(lane, name);
    };

    async function doSaveQueue(lane, name) {
        try {
            const payload = { lane: lane, name: name };
            const resp = await fetch('/saveQueue', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(payload)});
            const j = await resp.json();
            if (j.ok) {
                alert('Queue saved successfully');
            } else {
                let msg = j.error || 'unknown error';
                if (msg === 'no_space') msg = 'Not enough storage space on device to save the queue.';
                else if (msg === 'open_failed') msg = 'Failed to open temp file for writing on device.';
                else if (msg === 'write_failed') msg = 'Device failed to write data to storage.';
                else if (msg === 'rename_failed') msg = 'Failed to finalize save file on device.';
                alert('Save failed: ' + msg);
            }
        } catch (e) {
            alert('Save failed (network): ' + e);
        }
    }

    // Modal-based load dialog: fetch available saves and show modal picker
    window.openLoadDialog = async function() {
        const lane = getCurrentLaneFromUI();
        try {
            const resp = await fetch('/listSaves', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({lane: lane})});
            const j = await resp.json();
            if (!j.ok) { alert('Failed to list saves'); return; }
            const saves = j.saves || [];
            showLoadModal(lane, saves.map(s => s.name || s));
        } catch (e) {
            alert('Failed to fetch saves: ' + e);
        }
    };

    // Modal helpers for load selection
    window.showLoadModal = function showLoadModal(lane, saves) {
        var modal = document.getElementById('loadModal');
        var body = document.getElementById('loadModalBody');
        body.innerHTML = '';
        var okBtn = document.getElementById('loadModalOk');
        okBtn.removeAttribute('data-chosen');
        okBtn.removeAttribute('data-lane');
        if (!saves || saves.length === 0) {
            body.innerHTML = '<div>No saved queues available.</div>';
            okBtn.disabled = true;
        } else {
            var list = document.createElement('div');
            list.style.display = 'flex';
            list.style.flexDirection = 'column';
            list.style.gap = '6px';
            saves.forEach(function(name) {
                var item = document.createElement('button');
                item.textContent = name;
                item.setAttribute('data-save-name', name);
                item.style.textAlign = 'left';
                item.onclick = function() {
                    // clear previous selection visuals
                    var prev = body.querySelectorAll('.selected');
                    prev.forEach(function(p){ p.classList.remove('selected'); p.style.background=''; });
                    item.classList.add('selected');
                    item.style.background = '#ddd';
                    okBtn.setAttribute('data-chosen', name);
                    okBtn.setAttribute('data-lane', lane);
                    okBtn.disabled = false;
                };
                list.appendChild(item);
            });
            body.appendChild(list);
            okBtn.disabled = true;
        }
        modal.style.display = 'flex';
    };

    window.hideLoadModal = function hideLoadModal() {
        var modal = document.getElementById('loadModal');
        modal.style.display = 'none';
        var okBtn = document.getElementById('loadModalOk');
        if (okBtn) { okBtn.removeAttribute('data-chosen'); okBtn.removeAttribute('data-lane'); }
    };

    window.loadModalConfirm = function loadModalConfirm() {
        var okBtn = document.getElementById('loadModalOk');
        var name = okBtn.getAttribute('data-chosen');
        var lane = parseInt(okBtn.getAttribute('data-lane'));
        if (!name) return;
        hideLoadModal();
        doLoadQueue(lane, name);
    };

    async function doLoadQueue(lane, name) {
        try {
            const payload = { lane: lane, name: name };
            const resp = await fetch('/loadQueue', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(payload)});
            const j = await resp.json();
            if (j.ok) {
                alert(`Loaded: added ${j.added || 0}, skipped ${j.skipped || 0}, failed ${j.failed || 0}`);
                reconcileQueueWithDevice();
            } else {
                alert('Load failed');
            }
        } catch (e) {
            alert('Load failed (network): ' + e);
        }
    }
}

// Show the repeat selection modal and populate with current queue items
function showRepeatModal() {
    console.log('showRepeatModal called');
    const lane = currentSettings.currentLane || 0;
    const queue = swimSetQueues[lane] || [];
    const body = document.getElementById('repeatModalBody');
    const modal = document.getElementById('repeatModal');
    if (!body || !modal) return;

    body.innerHTML = '';
    repeatModalSelectedStart = 0;

    queue.forEach((entry, idx) => {
        const r = document.createElement('div');
        r.className = 'repeat-row';
        r.dataset.index = String(idx);
        r.onclick = () => setRepeatModalStart(idx);
        const lbl = document.createElement('label');
        lbl.textContent = `${idx + 1}. ${entry.summary || generateSetSummary(entry)}`;
        r.appendChild(lbl);
        body.appendChild(r);
    });

    // default iterations = 1
    const itersEl = document.getElementById('repeatIterations');
    if (itersEl) itersEl.value = 1;

    // initial visual update (highlight full queue)
    updateRepeatModalSelectionVisual();

    modal.style.display = 'flex';
}

function setRepeatModalStart(idx) {
    repeatModalSelectedStart = idx;
    updateRepeatModalSelectionVisual();
}

function updateRepeatModalSelectionVisual() {
    const body = document.getElementById('repeatModalBody');
    if (!body) return;
    const rows = body.querySelectorAll('.repeat-row');
    rows.forEach(r => {
        const i = parseInt(r.dataset.index, 10);
        if (!isNaN(i) && i >= repeatModalSelectedStart) {
            r.classList.add('repeat-selected');
        } else {
            r.classList.remove('repeat-selected');
        }
    });
}

// Hide modal
function hideRepeatModal() {
    const modal = document.getElementById('repeatModal');
    if (modal) modal.style.display = 'none';
}

// Called when modal "Create Repeat" pressed
function repeatModalConfirm() {
    const lane = currentSettings.currentLane || 0;
    const queue = swimSetQueues[lane] || [];
    if (!queue || queue.length === 0) {
        hideRepeatModal();
        return;
    }
    const startIndex = (typeof repeatModalSelectedStart === 'number') ? repeatModalSelectedStart : 0;
    const itersEl = document.getElementById('repeatIterations');
    let iterations = itersEl ? parseInt(itersEl.value, 10) : 1;
    if (isNaN(iterations) || iterations < 1) iterations = 1;

    // compute stable uniqueId for loop start and enqueue using that id
    const loopFromUniqueId = queue[startIndex] && queue[startIndex].uniqueId ? String(queue[startIndex].uniqueId) : undefined;
    enqueueLoopEntry(lane, loopFromUniqueId, iterations);
    hideRepeatModal();
}

// Create and enqueue LOOP_TYPE entry (optimistic local entry + POST)
function enqueueLoopEntry(lane, loopFromUniqueId, iterations) {
    const uniqueId = generateUniqueId(16);

    // Resolve a human-friendly summary by locating the start item in the current queue
    const q = swimSetQueues[lane] || [];
    const startIdx = (loopFromUniqueId ? q.findIndex(it => normalizeUniqueId(it.uniqueId) === normalizeUniqueId(loopFromUniqueId)) : -1);
    const blockLen = (startIdx >= 0) ? Math.max(0, q.length - startIdx) : '?';
    const summary = (startIdx >= 0)
        ? `Repeat last ${blockLen} set${blockLen !== 1 ? 's' : ''} x ${iterations}`
        : `Repeat x ${iterations}`;

    const payload = {
        lane: lane,
        numRounds: iterations,
        swimDistance: 0,
        swimTime: 0,
        restTime: 0,
        swimmerInterval: 0,
        type: LOOP_TYPE,
        repeatRemaining: iterations,
        loopFromUniqueId: loopFromUniqueId,
        summary: summary,
        uniqueId: uniqueId
    };

    const local = {...payload};
    local.synced = false;
    local.enqueuePending = true;
    local.enqueueRequestedAt = Date.now();

    swimSetQueues[lane] = swimSetQueues[lane] || [];
    swimSetQueues[lane].push(local);
    updateQueueDisplay();
    updatePacerButtons();

    // Delegate network enqueue to helper
    sendEnqueuePayload(payload, local).then(result => {
        if (!result.ok && !result.duplicate) {
            console.log('enqueueLoopEntry: enqueue failed', result.status || result.error);
        }
    });
}

// Replace old repeatQueue invocation with modal opener
function repeatQueue() {
    console.log('repeatQueue called');
    showRepeatModal();
}

function updatePacerButtons() {
    console.log('UpdatePacerButtons ENTER');
    const currentLane = currentSettings.currentLane;
    const pacerButtons = document.getElementById('pacerButtons');
    const startBtn = document.getElementById('startBtn');
    const stopBtn = document.getElementById('stopBtn');
    const copyLeftBtn = document.getElementById('copyLeftBtn');
    const copyRightBtn = document.getElementById('copyRightBtn');
    const repeatBtn = document.getElementById('repeatBtn');


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

        console.log('We have queue items, should be showing copy and repeat buttons');
        // show/hide copy buttons but preserve layout by toggling visibility
        if (copyLeftBtn) {
            if (currentLane > 0) {
                copyLeftBtn.style.visibility = 'visible';
            } else {
                copyLeftBtn.style.visibility = 'hidden';
            }
        }
        if (copyRightBtn) {
            if (currentSettings.numLanes && currentLane < (currentSettings.numLanes - 1)) {
                copyRightBtn.style.visibility = 'visible';
            } else {
                copyRightBtn.style.visibility = 'hidden';
            }
        }
        if (repeatBtn) {
            console.log('found repeatBtn element, setting visibility to visible');
            repeatBtn.style.visibility = 'visible';
        } else {
            console.log('could not find repeatBtn element');
        }
    } else {
        pacerButtons.style.display = 'none';
        copyLeftBtn.style.visibility = 'hidden';
        copyRightBtn.style.visibility = 'hidden';
        repeatBtn.style.visibility = 'hidden';
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
    // Set current lane
    currentSettings.currentLane = swimSet.lane;
    // Check if the lane differs from the current UI selection
    if (currentSettings.currentLane !== getCurrentLaneFromUI()) {
        console.log('loadSwimSetIntoConfig: Current lane differs from UI selection, updating UI to match loaded swim set lane');
        // Update any UI elements that depend on current lane
        updateLaneUI(currentSettings.currentLane);
    }

    // Restore settings
    // Copy settings into currentSettings (shallow copy) so we can modify freely
    Object.assign(currentSettings, JSON.parse(JSON.stringify(swimSet || {})));

    // Put setting into workingSwimSetInfo for the lane being edited
    createOrUpdateFromConfig();

    // Restore swimmer specific times
    if (Array.isArray(swimSet.swimmers)) {
        swimSet.swimmers.forEach((swimmer, index) => {
            setIndividualSwimmerTime(index, swimmer.swimTime);
        });
    }

    // Update swim set settings
    updateSwimSetUIFromSettings();
}

function saveSwimSet() {
    const lane = currentSettings.currentLane;

    if (editingSwimSetIndexes[lane] === -1) return;

    // Prepare local copy of the queued entry
    const existing = swimSetQueues[lane][editingSwimSetIndexes[lane]] || {};
    const newEntry = JSON.parse(JSON.stringify(existing));
    if (!workingSwimSetInfo) {
        console.log("ERROR: saveSwimSet: workingSwimSetInfo is not defined");
        createOrUpdateFromConfig();
    }
    newEntry.swimmers = JSON.parse(JSON.stringify(workingSwimSetInfo.swimmers));
    newEntry.numRounds = workingSwimSetInfo.numRounds;
    newEntry.swimDistance = workingSwimSetInfo.swimDistance;
    newEntry.swimTime = workingSwimSetInfo.swimTime;
    newEntry.restTime = workingSwimSetInfo.restTime;
    newEntry.swimmerInterval = workingSwimSetInfo.swimmerInterval;
    newEntry.summary = generateSetSummary(newEntry);
    newEntry.uniqueId = existing.uniqueId;

    // Prepare payload for update endpoint
    console.log('saveSwimSet: buildMinimalSwimSetPayload:', newEntry);
    const payload = buildMinimalSwimSetPayload(newEntry);

    // Try to update on the device; if network fails, apply local-only change
    console.log('saveSwimSet calling /updateSwimSet with payload:', payload);
    fetch('/updateSwimSet', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    }).then(async resp => {
        if (!resp.ok) {
            console.log('saveSwimSet: ERROR server update failed with error', resp.error);
            newEntry.synced = false;
            newEntry.updatePending = true;
            newEntry.updateRequestedAt = Date.now();
            swimSetQueues[lane][editingSwimSetIndexes[lane]] = newEntry;
            editingSwimSetIndexes[lane] = -1;
            returnToConfigMode();
            updateQueueDisplay();
        }
        const json = await resp.json().catch(() => null);
        // Merge server-assigned values back into our local entry
        if (json && json.uniqueId) newEntry.uniqueId = String(json.uniqueId);
        newEntry.synced = true;
        newEntry.updatePending = false;
        swimSetQueues[lane][editingSwimSetIndexes[lane]] = newEntry;
        console.log("updated successfully on server, replacing local with ", newEntry);
        // Clear editing state and return to create mode
        editingSwimSetIndexes[lane] = -1;
        returnToConfigMode();
        updateQueueDisplay();
    }).catch(err => {
        // Device not reachable or update failed — keep local change and mark unsynced
        console.debug('saveSwimSet: ERROR network/update - marking updatePending', err && err.message ? err.message : err);
        newEntry.synced = false;
        newEntry.updatePending = true;
        newEntry.updateRequestedAt = Date.now();
        swimSetQueues[lane][editingSwimSetIndexes[lane]] = newEntry;
        editingSwimSetIndexes[lane] = -1;
        returnToConfigMode();
        updateQueueDisplay();
    });
}

function cancelEdit() {
    const currentLane = currentSettings.currentLane;
    // Discard edits and return to configuring a new swim set
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
    } else if (item.uniqueId) {
        console.log('Attempting delete on server for item with uniqueId:', item.uniqueId);
    } else {
        console.log('No id or uniqueId to match swim set for deletion on server, removing locally only');
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
        if (item.uniqueId) body.matchUniqueId = String(item.uniqueId);
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
            console.log('Server delete failed for item with uniqueId:', item.uniqueId, ', will keep item pending for retry');
            return false;
        }
    } catch (e) {
        // Network failed - keep pending
        console.log('Network error during delete attempt for item with uniqueId:', item.uniqueId, ', will keep item pending for retry');
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
                        matchUniqueId: it.uniqueId || undefined,
                        uniqueId: it.uniqueId || undefined,
                        lane: lane,
                        numRounds: it.numRounds,
                        swimDistance: it.swimDistance,
                        swimTime: it.swimTime,
                        restTime: it.restTime,
                        swimmerInterval: it.swimmerInterval,
                        type: it.type || 0,
                        repeatRemaining: it.repeatRemaining || 0,
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
                        if (j && j.uniqueId) it.uniqueId = j.uniqueId;
                        swimSetQueues[lane][i] = it;
                        updateQueueDisplay();
                    } else {
                        console.log('Retry update failed for swim set with uniqueId:', it.uniqueId, ', will retry later');
                    }
                } catch (e) {
                    console.log('Retry update network error for swim set with uniqueId:', it.uniqueId, ', will retry later');
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

    // Send new order to device: POST lane & order (comma-separated ids or UniqueIds)
    const order = swimSetQueues[currentLane]
        .map(s => normalizeUniqueId(s.uniqueId))
        .filter(x => x.length > 0)
        .join(',');
    console.log('handleDrop calling /reorderSwimQueue');
    fetch('/reorderSwimQueue', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ lane: currentLane, order })
    }).then(resp => {
        if (!resp.ok) {
            console.log('Reorder request failed on server', resp.status, resp.statusText);
            throw new Error('reorder failed');
        }
        // Optionally reconcile response in the future
    }).catch(err => {
        console.log('reorder request failed, will keep local order until reconciliation', err);
    });
}

// Server-first start: request device to start the head (or specific index)
// Returns Promise<boolean>
async function startSwimSet(requestedIndex) {
     // Prevent duplicate starts
    const lane = getCurrentLaneFromUI();
    if (currentSettings.laneRunning && currentSettings.laneRunning[lane]) {
        console.warn('startSwimSet: lane', lane, 'already running, ignoring duplicate start');
        return;
    }
    const queue = swimSetQueues[lane] || [];

    // choose head or requested index (first non-completed)
    let headIndex = -1;
    if (typeof requestedIndex === 'number' && requestedIndex >= 0 && requestedIndex < queue.length) {
        headIndex = requestedIndex;
    } else {
        // use the status bit mask to find the first non-completed set
        headIndex = queue.findIndex(s => !(s.status & SWIMSET_STATUS_COMPLETED));
    }
    if (headIndex === -1) {
        console.warn('startSwimSet: no pending swim set to start');
        return false;
    }
    const head = queue[headIndex];

    const payload = { lane };
    if (head.uniqueId) payload.matchUniqueId = String(head.uniqueId);

    console.log('startSwimSet: sending POST to /startSwimSet with payload:', payload);
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 5000); // 5s timeout
    try {
        // TODO: I've noticed in startSwimSet (and likely stopSwimSet) that we don't
        // update the UI elements till after we return from the endpoint call to
        // /startSwimSet.   From a responsiveness perspective, it might be better to
        // update the UI immediately, and then throw up some alert if the status isn't
        // updated within a certain amount of time.
        const resp = await fetch('/startSwimSet', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
            signal: controller.signal
        });
        clearTimeout(timeoutId);

        if (!resp.ok) {
            const txt = await resp.text().catch(()=>null);
            console.warn('Device rejected start:', resp.status, txt);
            return false;
        }
        const json = await resp.json().catch(()=>null);
        if (json && json.uniqueId) head.uniqueId = String(json.uniqueId);

        // Only update UI after server confirms
        laneRunning[lane] = true;
        updateQueueDisplay();
        updatePacerButtons();
        updateStatus();

        return true;
    } catch (e) {
        clearTimeout(timeoutId);
        updateQueueDisplay();
        console.warn('startSwimSet failed (no server confirmation)', e && e.message ? e.message : e);
        return false;
    }
}

// Server-first stop: ask device to stop, then clear local running state on confirmation
async function stopSwimSet() {
    const lane = getCurrentLaneFromUI();
    // Ask server to stop this lane
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 5000);
    try {
        console.log("calling /stopSwimSet for lane " + lane);
        const resp = await fetch('/stopSwimSet', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ lane }),
            signal: controller.signal
        });
        clearTimeout(timeoutId);
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
        clearTimeout(timeoutId);
        console.warn('stopSwimSet failed (no server confirmation)', e && e.message ? e.message : e);
        return false;
    }
}

function displaySwimmerSet() {
    const lane = currentSettings.currentLane;

    // Update set details in swim practice nomenclature
    const setDetails = document.getElementById('setDetails');

    const numRounds = workingSwimSetInfo.numRounds;
    const swimDistance = workingSwimSetInfo.swimDistance;
    const swimTime = workingSwimSetInfo.swimTime;
    const restTime = workingSwimSetInfo.restTime;

    const swimTimeDisplay = (swimTime > 60) ? formatSecondsToMmSs(swimTime) : `${Math.round(swimTime)}s`;
    setDetails.innerHTML = `${numRounds} x ${swimDistance}'s on the ${swimTimeDisplay} with ${restTime} sec rest`;

    const swimmerList = document.getElementById('swimmerList');
    swimmerList.innerHTML = '';

    console.log('displaySwimmerSet: pulling swimmer color from list: ', currentSettings.swimmerColors);
    for (let i = 0; i < currentSettings.numSwimmersPerLane[lane]; i++) {
        const row = document.createElement('div');
        row.className = 'swimmer-row';

        // Determine the actual color value for display
        let displayColor = currentSettings.swimmerColors[lane][i].color;

        // Check if we have swimmer specific times, otherwise use swim set time
        let swimTimeToUse = workingSwimSetInfo.swimTime;
        if (workingSwimSetInfo.swimmers[i] && workingSwimSetInfo.swimmers[i].swimTime > 0) {
            swimTimeToUse = workingSwimSetInfo.swimmers[i].swimTime;
        }
        // Format swimTime value for display: use M:SS when > 60s, else plain seconds
        let swimTimeDisplay = (swimTimeToUse > 60) ? formatSecondsToMmSs(swimTimeToUse) : `${Math.round(swimTimeToUse)}`;
        let swimTimeUnitLabel = (swimTimeToUse > 60) ? 'min:sec' : 'sec';

       row.innerHTML = `
          <div class="swimmer-color" style="background-color: ${displayColor}"
              onclick="cycleSwimmerColor(${i})" title="Click to change color"></div>
          <div class="swimmer-info">Swimmer ${i + 1}</div>
          <div class="swimmer-swimTime"> <input type="text" class="swimmer-swimTime-input" value="${swimTimeDisplay}"
              placeholder="30 or 1:30" onchange="updateSwimmerSwimTime(${i}, this.value)"> <span class="swimTime-unit">${swimTimeUnitLabel}</span></div>
       `;

        swimmerList.appendChild(row);
    };
}

function cycleSwimmerColor(swimmerIndex) {
    const lane = currentSettings.currentLane;
    // Defensive programming: validate swimmer index
    if (swimmerIndex < 0 || swimmerIndex >= currentSettings.numSwimmersPerLane[lane]) {
        console.error(`Invalid swimmer index: ${swimmerIndex}`);
        return;
    }

    // Set the current swimmer being edited and open color picker
    currentSwimmerIndex = swimmerIndex;
    console.log(`Opening color picker for swimmer ${swimmerIndex} in ${currentSettings.laneNames[lane]}`);
    populateColorGrid();
    document.getElementById('customColorPicker').style.display = 'block';
}

function updateSwimmerSwimTime(swimmerIndex, newSwimTime) {
    const lane = currentSettings.currentLane;
    if (!workingSwimSetInfo) createOrUpdateFromConfig();
    if (workingSwimSetInfo &&
        workingSwimSetInfo.swimmers &&
        swimmerIndex >= 0 &&
        swimmerIndex < workingSwimSetInfo.swimmers.length) {

        // parseTimeInput supports both seconds and M:SS formats
        const parsed = parseTimeInput(String(newSwimTime));
        workingSwimSetInfo.swimmers[swimmerIndex].swimTime = parsed;
        // Refresh display to update unit labels if needed
        updateSwimmerSetDisplay();
    }
}
// Use targeted send* helpers (e.g., sendPoolLength, sendBrightness) instead of broad writes
// so we avoid overwriting unrelated device state.

// Build the minimal swim set payload expected by the device
function buildMinimalSwimSetPayload(createdSet) {
    const newSet = {
        lane: createdSet.lane,
        uniqueId: createdSet.uniqueId,
        numRounds: Number(createdSet.numRounds),
        swimDistance: Number(createdSet.swimDistance),
        swimTime: Number(createdSet.swimTime),
        restTime: Number(createdSet.restTime),
        swimmerInterval: Number(createdSet.swimmerInterval),
        type: Number(createdSet.type),
        swimmers: createdSet.swimmers.map(swimmer => ({
            swimTime: Number(swimmer.swimTime)
        })),
        summary: createdSet.summary,
        repeatRemaining: Number(createdSet.repeatRemaining)
    };
    console.log('buildMinimalSwimSetPayload generated payload:', newSet);
    return newSet;
}

function updateSwimSetUIFromSettings() {
    // Update workingSwimSetInfo swimmers array length
    updateIndividualSwimmerTimeDueToNumSwimmers();

    document.getElementById('numRounds').value = currentSettings.numRounds;
    document.getElementById('swimDistance').value = currentSettings.swimDistance;
    document.getElementById('swimTime').value = currentSettings.swimTime;
    document.getElementById('restTime').value = currentSettings.restTime;
    document.getElementById('swimmerInterval').value = currentSettings.swimmerInterval;
}

// Helper function to update all UI elements from settings
function updateAllUIFromSettings() {
    // Update input fields
    document.getElementById('numLanes').value = currentSettings.numLanes;
    console.log(`updateAllUIFromSettings: Updating numSwimmersPerLane value for lane ${currentSettings.currentLane} to ${currentSettings.numSwimmersPerLane[currentSettings.currentLane]}`);
    document.getElementById('numSwimmers').value =
        currentSettings.numSwimmersPerLane[currentSettings.currentLane];
    // Update swimmerColors array length for current lane
    updateIndividualSwimmerColorDueToNumSwimmers();

    const percent = Math.round(((currentSettings.brightness - 20) * 100) / (255 - 20));
    const clamped = Math.max(0, Math.min(100, percent));
    document.getElementById('brightness').value = clamped;
    document.getElementById('brightnessValue').textContent = clamped + '%';
    document.getElementById('pulseWidth').value = currentSettings.pulseWidth;
    document.getElementById('pulseWidthUnit').textContent = "ft"
    document.getElementById('colorIndicator').style.backgroundColor = currentSettings.swimmerColor;
    // Update radio button states
    if (currentSettings.colorMode === 'individual') {
        document.getElementById('individualColors').checked = true;
    } else {
        document.getElementById('sameColor').checked = true;
    }

    document.getElementById('poolLength').value =
        (currentSettings.poolLengthUnits === 'yards' ?
            currentSettings.poolLength : currentSettings.poolLength + "m");
    document.getElementById('stripLengthMeters').value = currentSettings.stripLengthMeters;
    document.getElementById('ledsPerMeter').value = currentSettings.ledsPerMeter;
    updateNumLedStripsSection();
    document.getElementById('gapBetweenStrips').value = currentSettings.gapBetweenStrips;

    // Update underwater fields
    // Update underwaters checkbox and labels WITHOUT triggering server updates
    updateUnderwatersEnabled(false, currentSettings.underwatersEnabled);
    const underwaterColorIndicatorEl = document.getElementById('underwaterColorIndicator');
    if (underwaterColorIndicatorEl) underwaterColorIndicatorEl.style.backgroundColor = currentSettings.underwaterColor;
    const surfaceColorIndicatorEl = document.getElementById('surfaceColorIndicator');
    if (surfaceColorIndicatorEl) surfaceColorIndicatorEl.style.backgroundColor = currentSettings.surfaceColor;
    document.getElementById('lightSize').value = currentSettings.lightSize;
    document.getElementById('lightSizeUnit').textContent = "ft";
    document.getElementById('firstUnderwaterDistance').value = currentSettings.firstUnderwaterDistance;
    document.getElementById('underwaterDistance').value = currentSettings.underwaterDistance;
    document.getElementById('hideAfter').value = currentSettings.hideAfter;
    document.getElementById('hideAfterUnit').textContent =
        currentSettings.hideAfter === 1 ? ' second' : ' seconds';

    // Update swim set fields
    updateSwimSetUIFromSettings();

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
        const deviceByClient = new Map();

        for (let i = 0; i < deviceQueue.length; i++) {
            const d = deviceQueue[i];
            // filter by lane if device provides lane
            if (d.lane !== undefined && Number(d.lane) !== Number(lane)) continue;
            const key = normalizeUniqueId(d.uniqueId);
            if (key) deviceByClient.set(key, d);
        }

        // Ensure swimSetQueues[lane] exists
        swimSetQueues[lane] = swimSetQueues[lane] || [];

        // Try to reconcile local entries in-place using uniqueId matching
        // to get any updates from the device
        for (let i = 0; i < swimSetQueues[lane].length; i++) {
            const local = swimSetQueues[lane][i];
            if (!local) continue;

            // Skip items we intentionally flagged as pending delete (do not re-sync them back)
            if (local.deletedPending) continue;

            // Verify item belongs to current lane before retrying
            if (local.lane !== undefined && Number(local.lane) !== Number(lane)) {
                console.warn('reconcile: mismatched lane, local entry has lane:', local.lane, ' vs  current lane:', lane);
                continue;
            }

            let matchedDevice = null;

            // Match by uniqueId (stable client-generated identifier)
            if (!matchedDevice && local.uniqueId) {
                console.log('Attempting match by uniqueId for local swim set:', local.uniqueId);
                const key = normalizeUniqueId(local.uniqueId);
                matchedDevice = deviceByClient.get(key);
                if (matchedDevice) {
                    console.log('Successfully matched by uniqueId');
                    matchedDevice.matched = true;
                }
            }

            // Merge authoritative fields from device when matched
            if (matchedDevice) {
                console.log('Merging fields from matched device:', matchedDevice);
                local.synced = true;
                if (matchedDevice.uniqueId !== undefined) local.uniqueId = String(matchedDevice.uniqueId);
                // status & runtime fields
                if (matchedDevice.completed !== undefined) local.completed = !!matchedDevice.completed;
                if (matchedDevice.status !== undefined) local.status = matchedDevice.status;
                if (matchedDevice.currentRound !== undefined) local.currentRound = Number(matchedDevice.currentRound);
                if (matchedDevice.startedAt) local.startedAt = matchedDevice.startedAt;
                if (matchedDevice.completedAt) local.completedAt = matchedDevice.completedAt;
                // authoritative swim-set parameters (coerce to numbers)
                if (matchedDevice.numRounds !== undefined) local.numRounds = Number(matchedDevice.numRounds);
                if (matchedDevice.swimDistance !== undefined) local.swimDistance = Number(matchedDevice.swimDistance);
                if (matchedDevice.swimTime !== undefined) local.swimTime = Number(matchedDevice.swimTime);
                if (matchedDevice.restTime !== undefined) local.restTime = Number(matchedDevice.restTime);
                if (matchedDevice.swimmerInterval !== undefined) local.swimmerInterval = Number(matchedDevice.swimmerInterval);
                // loop/repeat metadata
                if (matchedDevice.loopFromUniqueId !== undefined) local.loopFromUniqueId = String(matchedDevice.loopFromUniqueId);
                // accept either 'repeat' or server-side 'repeatRemaining' field names
                if (matchedDevice.repeat !== undefined) local.repeatRemaining = Number(matchedDevice.repeat);
                else if (matchedDevice.repeatRemaining !== undefined) local.repeatRemaining = Number(matchedDevice.repeatRemaining);
                // type
                if (matchedDevice.type !== undefined) local.type = Number(matchedDevice.type);
                // swimmers array (preserve color + swimTime)
                if (Array.isArray(matchedDevice.swimmers)) {
                    local.swimmers = matchedDevice.swimmers.map(s => ({
                        swimTime: Number(s.swimTime || local.swimTime)
                    }));
                }
                // recompute/generate summary from authoritative fields
                local.summary = generateSetSummary(local);

                //debug output
                console.log('Reconciled local swim set with device entry:', local, matchedDevice);
            } else {
                console.log('No matching device found for local swim set:', local);

                // Skip if an enqueue is already pending and we shouldn't retry yet
                if (!shouldRetryEnqueue(local)) {
                    console.log('reconcile: skipping enqueue for pending local set', local.uniqueId);
                    continue;
                }

                local.enqueuePending = true;
                local.enqueueRequestedAt = Date.now();
                local.synced = false;

                console.log('reconcileQueueWithDevice: buildMinimalSwimSetPayload:', local);
                const payload = buildMinimalSwimSetPayload(local);

                // Send this queue item again, using centralized enqueue helper
                sendEnqueuePayload(payload, local).then(result => {
                    if (!result.ok && !result.duplicate) {
                        console.log('reconcile: re-enqueue failed', result.status || result.error);
                    }
                });
            }
        }

        // Now add any device entries that we don't have locally
        for (let i = 0; i < deviceQueue.length; i++) {
            const d = deviceQueue[i];
            // filter by lane if device provides lane
            if (d.lane !== undefined && Number(d.lane) !== Number(lane)) continue;
            if (d.matched) continue; // already matched

            // Build new local entry from device entry
            const newEntry = {
                lane: Number(lane),
                uniqueId: d.uniqueId ? String(d.uniqueId) : undefined,
                numRounds: Number(d.numRounds || 0),
                swimDistance: Number(d.swimDistance || 0),
                swimTime: Number(d.swimTime || 0),
                restTime: Number(d.restTime || 0),
                swimmerInterval: Number(d.swimmerInterval || 0),
                status: d.status !== undefined ? d.status : 0,
                type: d.type !== undefined ? Number(d.type) : SWIMSET_TYPE,
                repeatRemaining: d.repeatRemaining !== undefined ? Number(d.repeatRemaining) : 0,
                loopFromUniqueId: d.loopFromUniqueId !== undefined ? String(d.loopFromUniqueId) : "",
                swimmers: Array.isArray(d.swimmers) ? d.swimmers.map(s => ({ swimTime: Number(s.swimTime||d.swimTime||0) })) : [],
                currentRound: d.currentRound !== undefined ? Number(d.currentRound) : undefined,
                startedAt: d.startedAt !== undefined ? d.startedAt : undefined,
                completedAt: d.completedAt !== undefined ? d.completedAt : undefined,
                synced: true,
            };
            newEntry.summary = generateSetSummary(newEntry);

            console.log('Adding new swim set from device to local queue:', newEntry.uniqueId);
            // if status is SWIMSET_STATUS_COMPLETED add to the beginning of the queue
            if (newEntry.status & SWIMSET_STATUS_COMPLETED) {
                swimSetQueues[lane].unshift(newEntry);
            } else {
                swimSetQueues[lane].push(newEntry);
            }
        }

        if (deviceStatus) {
            // We receive:
            // isRunning (bool)
            // laneRunning[lanes] (array of bool)
            currentSettings.isRunning = !!deviceStatus.isRunning;
            if (Array.isArray(deviceStatus.laneRunning)) {
                for (let l = 0; l < deviceStatus.laneRunning.length; l++) {
                    laneRunning[l] = !!deviceStatus.laneRunning[l];
                }
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

    // Since this isn't initialized as a default, ensure individual swimmer colors array is ready
    setIndividualSwimmerColors();

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

    // Initialize rest of the UI now that device values are applied and queue reconciled
    updateCalculations(false);
    initializeBrightnessDisplay();
    console.log('DOM fully loaded: calling updateLaneSelector with current lane:', currentSettings.currentLane);
    updateLaneSelector();
    updateNumLedStripsSection();
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
                // startSwimSet returns a Promise<boolean>
                startSwimSet().then(ok => {
                    if (!ok) console.warn('Device did not confirm start');
                }).catch(err => console.error('run start error', err));
            });
        }

        const stopBtn = document.getElementById('stopBtn');
        if (stopBtn) {
            stopBtn.addEventListener('click', () => {
                stopSwimSet().then(ok => {
                    if (!ok) console.warn('Device did not confirm stop');
                }).catch(err => console.error('stop error', err));
            });
        }
    } catch (e) {
        console.warn('Failed to attach start/stop listeners', e);
    }
});

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
            const lane = currentSettings.currentLane;

            // set created swim sets if not already done
            if (!workingSwimSetInfo) {
               createOrUpdateFromConfig()
            }

            // Ensure individual swimmer colors are set
            setIndividualSwimmerColors();

            // Update the UI
            // TODO: Maybe this should only update a subset
            updateAllUIFromSettings();
            updateSwimmerSetDisplay();
        } catch (e) {
            console.log('Error checking created set on tab switch, falling back to create:', e);
            setIndividualSwimmerTime(-1, currentSettings.swimTime);
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
            setIndividualSwimmerTime(-1,swimTimeEl.value);
        });
    }

    // Ensure switching to swimmers tab creates/updates the swimmer set automatically
    const detailsTabBtn = document.getElementById('createDetailsTab');
    const swimmersTabBtn = document.getElementById('createSwimmersTab');
    if (swimmersTabBtn) {
        swimmersTabBtn.addEventListener('click', function() {
            // No special action needed when going to swimmers tab?
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
        if (dev.stripLengthMeters !== undefined) currentSettings.stripLengthMeters = parseFloat(dev.stripLengthMeters);
        if (dev.ledsPerMeter !== undefined) currentSettings.ledsPerMeter = parseInt(dev.ledsPerMeter);
        // numLedStrips is per lane, so it is an array of ints
        if (dev.numLedStrips !== undefined && Array.isArray(dev.numLedStrips)) {
            console.log('DEBUG: Applying server\'s numLedStrips array to local settings');
            for (let li = 0; li < dev.numLedStrips.length && li < max_lanes; li++) {
                const n = parseInt(dev.numLedStrips[li]);
                if (!isNaN(n) && n >= 1) {
                    console.log('DEBUG: migrating numLedStrips for lane', li, 'to', n);
                    currentSettings.numLedStrips[li] = n;
                } else {
                    console.log('Skipping invalid numLedStrips for lane', li, ':', dev.numLedStrips[li]);
                }
            }
        }
        if (dev.gapBetweenStrips !== undefined) currentSettings.gapBetweenStrips = parseInt(dev.gapBetweenStrips);
        if (dev.pulseWidth !== undefined) currentSettings.pulseWidth = parseInt(dev.pulseWidth);
        if (dev.numLanes !== undefined) currentSettings.numLanes = parseInt(dev.numLanes);
        if (dev.numSwimmersPerLane !== undefined && Array.isArray(dev.numSwimmersPerLane)) {
            console.log('DEBUG: Applying server\'s numSwimmersPerLane array to local settings');
            // Prefer per-lane counts
            try {
                for (let li = 0; li < dev.numSwimmersPerLane.length && li < max_lanes; li++) {
                    const n = parseInt(dev.numSwimmersPerLane[li]);
                    if (!isNaN(n) && n >= 0 && n <= max_swimmers_per_lane) {
                        console.log('DEBUG: migrating numSwimmersPerLane for lane', li, 'to', n);
                        migrateSwimmerCountsToDeviceForLane(li, n);
                    } else {
                        console.log('Skipping invalid numSwimmersPerLane for lane', li, ':', dev.numSwimmersPerLane[li]);
                    }
                }
            } catch (e) {
                // fallback to global value
            }
        }
        if (dev.poolLength !== undefined) currentSettings.poolLength = dev.poolLength;
        if (dev.poolLengthUnits !== undefined) currentSettings.poolLengthUnits = dev.poolLengthUnits;
        if (dev.brightness !== undefined) currentSettings.brightness = parseInt(dev.brightness);

        // Colors as bytes -> hex
        if (dev.colorRed !== undefined && dev.colorGreen !== undefined && dev.colorBlue !== undefined) {
            currentSettings.swimmerColor = rgbBytesToHex(Number(dev.colorRed), Number(dev.colorGreen), Number(dev.colorBlue));
        }

        // Underwaters enabled + colors
        if (dev.underwatersEnabled !== undefined) currentSettings.underwatersEnabled = dev.underwatersEnabled;
        if (dev.underwaterColor !== undefined) currentSettings.underwaterColor = dev.underwaterColor;
        if (dev.surfaceColor !== undefined) currentSettings.surfaceColor = dev.surfaceColor;
        if (dev.firstUnderwaterDistanceFeet !== undefined) currentSettings.firstUnderwaterDistance = parseFloat(dev.firstUnderwaterDistanceFeet);
        if (dev.underwaterDistanceFeet !== undefined) currentSettings.underwaterDistance = parseFloat(dev.underwaterDistanceFeet);
        if (dev.underwatersSizeFeet !== undefined) currentSettings.lightSize = parseFloat(dev.underwatersSizeFeet);
        if (dev.hideAfterSeconds !== undefined) currentSettings.hideAfterSeconds = parseFloat(dev.hideAfterSeconds);

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

        setIndividualSwimmerColors();

        console.log('fetchDeviceSettingsAndApply: current settings after apply:', currentSettings);

        // After merging, update full UI
        console.log('fetchDeviceSettingsAndApply: calling updateAllUIFromSettings to update UI settings');
        updateAllUIFromSettings();
        // Re-enable outbound writes now that device defaults are applied
        suppressSettingsWrites = false;
    } catch (e) {
        console.log('Device settings fetch failed:', e);
        // Ensure we don't leave suppression enabled on error
        suppressSettingsWrites = false;
    }
}

// Helper: clone queue entries and enqueue to target lane
async function copyLaneQueueTo(fromLane, toLane) {
    if (fromLane === toLane) return;
    // source queue (support both per-lane and global queue setups)
    let source = (swimSetQueues && Array.isArray(swimSetQueues) && Array.isArray(swimSetQueues[fromLane])) ? swimSetQueues[fromLane] : [];
    if (!source || source.length === 0) return;

    // ensure destination exists for per-lane shape
    if (swimSetQueues && Array.isArray(swimSetQueues) && Array.isArray(swimSetQueues[toLane])) {
        // nothing
    } else if (typeof swimSetQueue !== 'undefined' && Array.isArray(swimSetQueue)) {
        // fine
    } else {
        console.warn('No recognized queue structure to copy into');
        return;
    }

    for (const item of source) {
        // shallow clone then normalize fields
        const clone = JSON.parse(JSON.stringify(item));
        clone.lane = toLane;
        clone.summary = generateSetSummary(item);
        // Create a new uniqueId as this will be a new entry
        clone.uniqueId = generateUniqueId(16);
        clone.synced = false;
        clone.enqueuePending = true;
        clone.enqueueRequestedAt = Date.now();

        // push locally
        if (swimSetQueues && Array.isArray(swimSetQueues) && Array.isArray(swimSetQueues[toLane])) {
            swimSetQueues[toLane].push(clone);
        }

        updateQueueDisplay();

        // attempt to enqueue on server (sequential, best-effort). If standalone, skip.
        if (!isStandaloneMode) {
            const payload = buildMinimalSwimSetPayload(clone);
            // Use centralized helper to send enqueue and update the local optimistic entry
            sendEnqueuePayload(payload, clone).then(result => {
                if (!result.ok && !result.duplicate) {
                    console.warn('enqueue failed for copied set to lane', toLane, result.status || result.error);
                }
            });
        }
    }
}

// UI handlers
function copyLeft() {
    const lane = currentSettings.currentLane;
    if (lane > 0) copyLaneQueueTo(lane, lane - 1);
}
function copyRight() {
    const lane = currentSettings.currentLane;
    if (currentSettings.numLanes && lane < (currentSettings.numLanes - 1)) copyLaneQueueTo(lane, lane + 1);
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

function sendStripLength(stripLengthMeters) {
    return postForm('/setStripLength', `stripLengthMeters=${encodeURIComponent(stripLengthMeters)}`);
}

function sendPoolLength(poolLength, poolUnits) {
    return postForm('/setPoolLength', `poolLength=${encodeURIComponent(poolLength)}&poolLengthUnits=${encodeURIComponent(poolUnits)}`);
}

function sendLedsPerMeter(ledsPerMeter) {
    return postForm('/setLedsPerMeter', `ledsPerMeter=${encodeURIComponent(ledsPerMeter)}`);
}

function sendNumLedStrips(lane, numLedStrips) {
    return postForm('/setNumLedStrips', `lane=${encodeURIComponent(lane)}&numLedStrips=${encodeURIComponent(numLedStrips)}`);
}

function sendGapBetweenStrips(gapBetweenStrips) {
    return postForm('/setGapBetweenStrips', `gapBetweenStrips=${encodeURIComponent(gapBetweenStrips)}`);
}

function sendNumLanes(numLanes) {
    return postForm('/setNumLanes', `numLanes=${encodeURIComponent(numLanes)}`);
}

function sendDelayIndicators(enabled) {
    return postForm('/setDelayIndicators', `enabled=${enabled ? 'true' : 'false'}`);
}

function sendNumSwimmers(lane, numSwimmers) {
    let body = `lane=${encodeURIComponent(lane)}&numSwimmers=${encodeURIComponent(numSwimmers)}`;
    return postForm('/setNumSwimmers', body);
}

function sendColorMode(mode) {
    return postForm('/setColorMode', `colorMode=${encodeURIComponent(mode)}`);
}

function sendSwimmerColor(hex) {
    return postForm('/setSwimmerColor', `color=${encodeURIComponent(hex)}`);
}

function sendSwimmerColors(lane, csvHex) {
    return postForm('/setSwimmerColors', `lane=${encodeURIComponent(lane)}&colors=${encodeURIComponent(csvHex)}`);
}

function sendUnderwaterSettings() {
    const u = currentSettings;
    const body = `enabled=${u.underwatersEnabled ? 'true' : 'false'}&firstUnderwaterDistance=${encodeURIComponent(u.firstUnderwaterDistance)}&underwaterDistance=${encodeURIComponent(u.underwaterDistance)}&hideAfter=${encodeURIComponent(u.hideAfter)}&lightSize=${encodeURIComponent(u.lightSize)}&underwaterColor=${encodeURIComponent(u.underwaterColor)}&surfaceColor=${encodeURIComponent(u.surfaceColor)}`;
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

function sendSwimTime(swimTime) {
    return postForm('/setSwimTime', `swimTime=${encodeURIComponent(Number(parseTimeInput(swimTime)))}`);
}

function sendRestTime(restTime) {
    return postForm('/setRestTime', `restTime=${encodeURIComponent(Number(restTime))}`);
}

function sendSwimmerInterval(intervalSeconds) {
    return postForm('/setSwimmerInterval', `swimmerInterval=${encodeURIComponent(Number(intervalSeconds))}`);
}
