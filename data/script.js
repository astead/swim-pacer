// Detect if running in standalone mode (file:// protocol)
const isStandaloneMode = window.location.protocol === 'file:';

let currentSettings = {
    speed: 5.0,
    color: 'red',
    brightness: 196,
    pulseWidth: 1.0,
    restTime: 5,
    paceDistance: 50,
    initialDelay: 2,
    swimmerInterval: 1,
    delayIndicatorsEnabled: true,
    numSwimmers: 3,
    numRounds: 10,
    colorMode: 'individual',
    swimmerColor: '#0000ff',
    poolLength: '25',
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
    const poolFeet = poolYards * 3; // Convert yards to feet
    return poolFeet / paceSeconds;
}

function speedToPace(speedFps, poolYards = 50) {
    const poolFeet = poolYards * 3; // Convert yards to feet
    return poolFeet / speedFps;
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

function updateFromPace() {
    const paceInput = document.getElementById('pacePer50').value;
    const pace = parseTimeInput(paceInput);
    const paceDistance = currentSettings.paceDistance;
    // Convert pace to speed (feet per second) based on selected distance
    const distanceFeet = paceDistance * 3; // yards to feet
    const speed = distanceFeet / pace;
    currentSettings.speed = speed;
}

function updatePaceDistance() {
    const paceDistance = parseInt(document.getElementById('paceDistance').value);
    currentSettings.paceDistance = paceDistance;

    // Determine units based on pool length setting
    const units = currentSettings.poolLength.includes('m') ? 'meters' : 'yards';

    // Update the distance units label
    const distanceUnitsElement = document.getElementById('distanceUnits');
    if (distanceUnitsElement) {
        distanceUnitsElement.textContent = units;
    }

    // Recalculate speed based on current pace input and new distance
    updateFromPace();
    updateSettings();
}

function updateCalculations() {
    const poolLength = document.getElementById('poolLength').value;
    const stripLength = parseFloat(document.getElementById('stripLength').value);
    const ledsPerMeter = parseInt(document.getElementById('ledsPerMeter').value);

    // Update current settings
    currentSettings.poolLength = poolLength;
    currentSettings.stripLength = stripLength;
    currentSettings.ledsPerMeter = ledsPerMeter;

    // Update distance units when pool length changes
    updatePaceDistance();

    // Apply changes immediately
    updateSettings();
}

function updateNumLanes() {
    const numLanes = parseInt(document.getElementById('numLanes').value);
    currentSettings.numLanes = numLanes;
    updateLaneSelector();
    updateLaneNamesSection();
    updateSettings();
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
            updateSettings(); // Save the changes
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
    updateSettings();
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
    updateSettings();
}

function updateRestTime() {
    const restTime = document.getElementById('restTime').value;
    currentSettings.restTime = parseInt(restTime);
    const unit = parseInt(restTime) === 1 ? ' second' : ' seconds';
    document.getElementById('restTimeValue').textContent = restTime + unit;
    updateSettings();
}

function updateInitialDelay() {
    const initialDelay = document.getElementById('initialDelay').value;
    currentSettings.initialDelay = parseInt(initialDelay);
    const unit = parseInt(initialDelay) === 1 ? ' second' : ' seconds';
    document.getElementById('initialDelayValue').textContent = initialDelay + unit;
    updateSettings();
}

function updateSwimmerInterval() {
    const swimmerInterval = document.getElementById('swimmerInterval').value;
    currentSettings.swimmerInterval = parseInt(swimmerInterval);
    const unit = parseInt(swimmerInterval) === 1 ? ' second' : ' seconds';
    document.getElementById('swimmerIntervalValue').textContent = swimmerInterval + unit;
    updateSettings();
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

    updateSettings();
}

function updateUnderwatersEnabled() {
    const enabled = document.getElementById('underwatersEnabled').checked;
    currentSettings.underwatersEnabled = enabled;

    // Show/hide underwaters controls
    const controls = document.getElementById('underwatersControls');
    controls.style.display = enabled ? 'block' : 'none';

    // Update toggle labels
    const toggleOff = document.getElementById('toggleOff');
    const toggleOn = document.getElementById('toggleOn');

    if (enabled) {
        toggleOff.classList.remove('active');
        toggleOn.classList.add('active');
    } else {
        toggleOff.classList.add('active');
        toggleOn.classList.remove('active');
    }

    updateSettings();
}

function updateLightSize() {
    const lightSize = document.getElementById('lightSize').value;
    currentSettings.lightSize = parseFloat(lightSize);
    const unit = parseFloat(lightSize) === 1.0 ? ' foot' : ' feet';
    document.getElementById('lightSizeValue').textContent = lightSize + unit;
    updateSettings();
}

function updateFirstUnderwaterDistance() {
    const distance = document.getElementById('firstUnderwaterDistance').value;
    currentSettings.firstUnderwaterDistance = parseInt(distance);
    const unit = parseInt(distance) === 1 ? ' foot' : ' feet';
    document.getElementById('firstUnderwaterDistanceValue').textContent = distance + unit;
    updateSettings();
}

function updateUnderwaterDistance() {
    const distance = document.getElementById('underwaterDistance').value;
    currentSettings.underwaterDistance = parseInt(distance);
    const unit = parseInt(distance) === 1 ? ' foot' : ' feet';
    document.getElementById('underwaterDistanceValue').textContent = distance + unit;
    updateSettings();
}

function updateHideAfter() {
    const hideAfter = document.getElementById('hideAfter').value;
    currentSettings.hideAfter = parseInt(hideAfter);
    const unit = parseInt(hideAfter) === 1 ? ' second' : ' seconds';
    document.getElementById('hideAfterValue').textContent = hideAfter + unit;
    updateSettings();
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
    const numSwimmers = document.getElementById('numSwimmers').value;
    currentSettings.numSwimmers = parseInt(numSwimmers);
    updateSettings();
}

function updateNumRounds() {
    const numRounds = document.getElementById('numRounds').value;
    currentSettings.numRounds = parseInt(numRounds);
    updateSettings();
}

function updateColorMode() {
    const colorMode = document.querySelector('input[name="colorMode"]:checked').value;
    currentSettings.colorMode = colorMode;
    updateVisualSelection();

    // Only send color mode change, don't call updateSettings() which would reset swimmers
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
    if (colorGrid.children.length > 0) return; // Already populated

    // Define a palette of common colors
    const colors = [
        '#ff0000', '#00ff00', '#0000ff', '#ffff00', '#ff00ff', '#00ffff',
        '#800000', '#008000', '#000080', '#808000', '#800080', '#008080',
        '#ff8000', '#80ff00', '#8000ff', '#ff0080', '#0080ff', '#ff8080',
        '#ffa500', '#90ee90', '#add8e6', '#f0e68c', '#dda0dd', '#afeeee',
        '#ffffff', '#c0c0c0', '#808080', '#404040', '#202020', '#000000'
    ];

    colors.forEach(color => {
        const colorDiv = document.createElement('div');
        colorDiv.style.cssText = `
            width: 40px;
            height: 40px;
            background-color: ${color};
            border: 2px solid #333;
            border-radius: 50%;
            cursor: pointer;
            transition: transform 0.1s;
        `;
        colorDiv.onmouseover = () => colorDiv.style.transform = 'scale(1.1)';
        colorDiv.onmouseout = () => colorDiv.style.transform = 'scale(1)';
        colorDiv.onclick = () => selectColor(color);
        colorGrid.appendChild(colorDiv);
    });
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
        updateSettings();
    } else if (currentColorContext === 'surface') {
        // Updating surface color
        currentSettings.surfaceColor = color;
        document.getElementById('surfaceColorIndicator').style.backgroundColor = color;
        updateSettings();
    } else {
        // Updating coach config same color setting
        currentSettings.swimmerColor = color;
        document.getElementById('colorIndicator').style.backgroundColor = color;
        updateSettings();
    }

    currentColorContext = null; // Reset context
    closeColorPicker();
}

function updateSwimmerColor() {
    const swimmerColor = document.getElementById('swimmerColorPicker').value;
    currentSettings.swimmerColor = swimmerColor;

    // Update the color indicator
    document.getElementById('colorIndicator').style.backgroundColor = swimmerColor;

    updateSettings();
}

// Pacer status tracking variables - now lane-specific
let pacerStartTimes = [0, 0, 0, 0]; // Start time for each lane
let currentRounds = [1, 1, 1, 1]; // Current round for each lane
let completionHandled = [false, false, false, false]; // Track if completion has been handled for each lane
let statusUpdateInterval = null;
let currentColorContext = null; // Track which color picker context we're in

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
            pacePer50: parseTimeInput(document.getElementById('pacePer50').value),
            restTime: currentSettings.restTime,
            numRounds: currentSettings.numRounds,
            initialDelay: currentSettings.initialDelay,
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
    const paceSeconds = runningData.pacePer50;
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
    if (runningData.initialDelay > 0) {
        const nextEventEl = document.getElementById('nextEvent');
        if (nextEventEl) {
            nextEventEl.textContent = `Starting in ${runningData.initialDelay}s`;
        }

        const currentPhaseEl = document.getElementById('currentPhase');
        if (currentPhaseEl) {
            currentPhaseEl.textContent = 'Initial Delay';
        }
    } else {
        const nextEventEl = document.getElementById('nextEvent');
        if (nextEventEl) {
            nextEventEl.textContent = 'Starting now';
        }

        const currentPhaseEl = document.getElementById('currentPhase');
        if (currentPhaseEl) {
            currentPhaseEl.textContent = 'Swimming';
        }
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
    const initialDelaySeconds = runningData.initialDelay;
    const timeAfterInitialDelay = elapsedSeconds - initialDelaySeconds;

    // Check if we're still in the initial delay period
    if (timeAfterInitialDelay < 0) {
        // Still in initial delay phase
        document.getElementById('currentPhase').textContent = 'Initial Delay';
        document.getElementById('activeSwimmers').textContent = '0';
        document.getElementById('nextEvent').textContent = `Starting in ${Math.ceil(-timeAfterInitialDelay)}s`;

        // Show initial round timing during delay using running settings
        const paceSeconds = runningData.pacePer50;
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
    const paceSeconds = runningData.pacePer50;
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
    const lastSwimmerStartDelay = runningData.initialDelay + ((runningData.numSwimmers - 1) * runningData.swimmerInterval);
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
            const swimmerStartTime = runningData.initialDelay + (i * runningData.swimmerInterval);
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
    const currentPace = parseTimeInput(document.getElementById('pacePer50').value);

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
            interval: i === 0 ? currentSettings.initialDelay : currentSettings.initialDelay + (i * currentSettings.swimmerInterval), // First swimmer uses initial delay, others add swimmer intervals
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
            pacePer50: currentPace // Explicitly include the pace value
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

function generateSetSummary(swimmers, settings) {
    const paceDistance = settings.paceDistance;
    const avgPace = swimmers.length > 0 ? swimmers[0].pace : 30;
    const restTime = settings.restTime;
    const numRounds = settings.numRounds;

    return `${numRounds} x ${paceDistance}'s on the ${avgPace} with ${restTime} sec rest`;
}

function queueSwimSet() {
    const currentLane = currentSettings.currentLane;

    if (!createdSwimSets[currentLane]) {
        alert('No set to queue');
        return;
    }

    // Add to current lane's queue
    swimSetQueues[currentLane].push(createdSwimSets[currentLane]);

    // Clear created set for current lane
    createdSwimSets[currentLane] = null;

    // Return to configuration mode
    returnToConfigMode();

    // Update queue display
    updateQueueDisplay();

    // Show start button if this is the first set
    updatePacerButtons();
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
    document.getElementById('queueButtons').style.display = 'none';
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

            statusClass = `style="background: ${backgroundColor}; border: 1px solid ${borderColor}; padding: 8px 10px; margin: 5px 0; border-radius: 4px; ${isCompleted ? 'opacity: 0.8;' : ''}"`;

            html += `
                <div ${statusClass}>
                    <div style="display: flex; justify-content: space-between; align-items: center;">
                        <div style="font-weight: bold; color: ${isActive ? '#1976D2' : isCompleted ? '#28a745' : '#333'};">
                            ${isCompleted ? 'Γ£ô ' : ''}${swimSet.summary}
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

    // Show swimmer set for editing
    displaySwimmerSet();
    document.getElementById('configControls').style.display = 'none';
    document.getElementById('swimmerSet').style.display = 'block';

    // Switch to edit buttons
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
    if (swimSet.settings.pacePer50) {
        document.getElementById('pacePer50').value = swimSet.settings.pacePer50;
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
    sendStartCommand();

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
    // First update ESP32 with current work set settings
    updateSettings();

    // Small delay to ensure settings are applied before starting
    setTimeout(() => {
        fetch('/toggle', { method: 'POST' })
        .then(response => response.text())
        .then(result => {
            console.log('Start command sent:', result);
        })
        .catch(error => {
            console.log('Running in standalone mode - start command not sent');
        });
    }, 100); // 100ms delay
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
    const avgPace = currentSet.length > 0 ? currentSet[0].pace : parseTimeInput(document.getElementById('pacePer50').value);
    const restTime = currentSettings.restTime;
    const numRounds = currentSettings.numRounds;
    const laneName = currentSettings.laneNames[currentSettings.currentLane];

    setDetails.innerHTML = `${laneName}: ${numRounds} x ${paceDistance}'s on the ${avgPace} with ${restTime} sec rest`;

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

        row.innerHTML = `
            <div class="swimmer-color" style="background-color: ${displayColor}"
                 onclick="cycleSwimmerColor(${index})" title="Click to change color"></div>
            <div class="swimmer-info">Swimmer ${swimmer.id}</div>
            <div>Pace: <input type="number" class="swimmer-pace-input" value="${swimmer.pace}"
                 min="20" max="300" step="0.5" onchange="updateSwimmerPace(${index}, this.value)"> sec</div>
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
        currentSet[swimmerIndex].pace = parseFloat(newPace);
    }
}

function updateSettings() {
    // Skip server communication in standalone mode
    if (isStandaloneMode) {
        return;
    }

    fetch('/setSpeed', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `speed=${currentSettings.speed}`
    }).catch(error => {
        console.log('Speed update - server not available');
    });

    fetch('/setPulseWidth', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `pulseWidth=${currentSettings.pulseWidth}`
    }).catch(error => {
        console.log('Pulse width update - server not available');
    });

    fetch('/setStripLength', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `stripLength=${currentSettings.stripLength}`
    }).catch(error => {
        console.log('Strip length update - server not available');
    });

    fetch('/setLedsPerMeter', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `ledsPerMeter=${currentSettings.ledsPerMeter}`
    }).catch(error => {
        console.log('LEDs per meter update - server not available');
    });

    fetch('/setNumLanes', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `numLanes=${currentSettings.numLanes}`
    }).catch(error => {
        console.log('Number of lanes update - server not available');
    });

    fetch('/setRestTime', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `restTime=${currentSettings.restTime}`
    }).catch(error => {
        console.log('Rest time update - server not available');
    });

    fetch('/setPaceDistance', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `paceDistance=${currentSettings.paceDistance}`
    }).catch(error => {
        console.log('Pace distance update - server not available');
    });

    fetch('/setInitialDelay', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `initialDelay=${currentSettings.initialDelay}`
    }).catch(error => {
        console.log('Initial delay update - server not available');
    });

    fetch('/setSwimmerInterval', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `swimmerInterval=${currentSettings.swimmerInterval}`
    }).catch(error => {
        console.log('Swimmer interval update - server not available');
    });

    fetch('/setDelayIndicators', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `enabled=${currentSettings.delayIndicatorsEnabled}`
    }).catch(error => {
        console.log('Delay indicators update - server not available');
    });

    fetch('/setNumSwimmers', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `numSwimmers=${currentSettings.numSwimmers}`
    }).catch(error => {
        console.log('Number of swimmers update - server not available');
    });

    fetch('/setNumRounds', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `numRounds=${currentSettings.numRounds}`
    }).catch(error => {
        console.log('Number of rounds update - server not available');
    });

    fetch('/setCurrentLane', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `currentLane=${currentSettings.currentLane}`
    }).catch(error => {
        console.log('Current lane update - server not available');
    });

    // Send color mode and swimmer color settings
    fetch('/setColorMode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `colorMode=${currentSettings.colorMode}`
    }).catch(error => {
        console.log('Color mode update - server not available');
    });

    if (currentSettings.colorMode === 'same') {
        // Send the same color for all swimmers
        fetch('/setSwimmerColor', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `color=${encodeURIComponent(currentSettings.swimmerColor)}`
        }).catch(error => {
            console.log('Swimmer color update - server not available');
        });
    } else {
        // Send individual swimmer colors
        const currentSet = getCurrentSwimmerSet();
        if (currentSet && currentSet.length > 0) {
            const swimmerColors = currentSet.map(swimmer => swimmer.color || '#ff0000').join(',');
            fetch('/setSwimmerColors', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `colors=${encodeURIComponent(swimmerColors)}`
            }).catch(error => {
                console.log('Individual swimmer colors update - server not available');
            });
        }
    }

    // Send underwater settings if enabled
    if (currentSettings.underwatersEnabled) {
        fetch('/setUnderwaterSettings', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `enabled=true&firstUnderwaterDistance=${currentSettings.firstUnderwaterDistance}&underwaterDistance=${currentSettings.underwaterDistance}&surfaceDistance=${currentSettings.surfaceDistance}&hideAfter=${currentSettings.hideAfter}&lightSize=${currentSettings.lightSize}&underwaterColor=${encodeURIComponent(currentSettings.underwaterColor)}&surfaceColor=${encodeURIComponent(currentSettings.surfaceColor)}`
        }).catch(error => {
            console.log('Underwater settings update - server not available');
        });
    }
}

// Helper function to update all UI elements from settings
function updateAllUIFromSettings() {
    // Update input fields
    document.getElementById('numRounds').value = currentSettings.numRounds;
    document.getElementById('restTime').value = currentSettings.restTime;
    document.getElementById('numSwimmers').value = currentSettings.numSwimmers;
    document.getElementById('initialDelay').value = currentSettings.initialDelay;
    document.getElementById('swimmerInterval').value = currentSettings.swimmerInterval;
    document.getElementById('pacePer50').value = currentSettings.speed;
    document.getElementById('paceDistance').value = currentSettings.paceDistance;
    document.getElementById('brightness').value = Math.round((currentSettings.brightness - 20) * 100 / (255 - 20));

    // Update underwater fields
    document.getElementById('firstUnderwaterDistance').value = currentSettings.firstUnderwaterDistance;
    document.getElementById('underwaterDistance').value = currentSettings.underwaterDistance;
    document.getElementById('hideAfter').value = currentSettings.hideAfter;

    // Update radio button states
    if (currentSettings.colorMode === 'individual') {
        document.getElementById('individualColors').checked = true;
    } else {
        document.getElementById('sameColor').checked = true;
    }

    // Update display values
    updateNumRounds();
    updateRestTime();
    updateNumSwimmers();
    updateInitialDelay();
    updateSwimmerInterval();
    updateSpeed();
    updatePaceDistance();
    updateBrightness();
    updateFirstUnderwaterDistance();
    updateUnderwaterDistance();
    updateHideAfter();

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
    updateCalculations();
    initializeBrightnessDisplay();
    updateLaneSelector();
    updateLaneNamesSection();
    updateStatus();

    // Ensure visual selection is applied after DOM is ready
    updateVisualSelection();
    initializeQueueSystem();
});

// Color testing function
function testColors() {
    if (isStandaloneMode) {
        alert('Color test not available in standalone mode');
        return;
    }

    fetch('/testColors', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: ''
    }).then(response => response.text())
    .then(data => {
        alert('Color test completed! Check your LED strip and Serial Monitor for results.\n\nExpected:\nLED 0: RED\nLED 1: GREEN\nLED 2: BLUE\nLED 3: ORANGE');
    }).catch(error => {
        console.error('Color test failed:', error);
        alert('Color test failed - check console for details');
    });
}
