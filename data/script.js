// Swim Pacer JavaScript - Extracted for size optimization
let currentSettings = {
    numLanes: 2, poolLength: 22.86, stripLength: 23.0, ledsPerMeter: 30,
    pulseWidth: 1.0, speed: 1.2, restTime: 5, paceDistance: 50,
    initialDelay: 10, swimmerInterval: 4, delayIndicators: true,
    numSwimmers: 3, numRounds: 10, colorRed: 0, colorGreen: 0, colorBlue: 255,
    brightness: 196, isRunning: false, currentLane: 0, colorMode: 'same'
};

let swimSetQueues = [[], [], [], []];
let activeSwimSets = [null, null, null, null];
let completionHandled = [false, false, false, false];
let pacerIntervals = [null, null, null, null];
let statusIntervals = [null, null, null, null];

function showPage(page) {
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    document.querySelectorAll('.nav-tab').forEach(t => t.classList.remove('active'));
    document.getElementById(page).classList.add('active');
    document.querySelector(`[onclick="showPage('${page}')"]`).classList.add('active');
}

function updateCurrentLane() {
    currentSettings.currentLane = parseInt(document.getElementById('currentLane').value);
    updateDisplay();
}

function updateNumSwimmers() {
    currentSettings.numSwimmers = parseInt(document.getElementById('numSwimmers').value);
    updateCalculations();
}

function updateNumRounds() {
    const val = parseInt(document.getElementById('numRounds').value);
    currentSettings.numRounds = val;
    document.getElementById('numRoundsValue').textContent = val;
    updateCalculations();
}

function updateRestTime() {
    const val = parseInt(document.getElementById('restTime').value);
    currentSettings.restTime = val;
    document.getElementById('restTimeValue').textContent = val + 's';
    updateCalculations();
}

function updateSpeed() {
    const val = parseFloat(document.getElementById('pacePer50').value);
    currentSettings.speed = val;
    document.getElementById('speedValue').textContent = val.toFixed(1) + 's';
    updateCalculations();
}

function updatePaceDistance() {
    const val = parseInt(document.getElementById('paceDistance').value);
    currentSettings.paceDistance = val;
    document.getElementById('paceDistanceValue').textContent = val + 'y';
    updateCalculations();
}

function updateBrightness() {
    const val = parseInt(document.getElementById('brightness').value);
    currentSettings.brightness = Math.round(20 + val * (255 - 20) / 100);
    document.getElementById('brightnessValue').textContent = val + '%';
}

function updateCalculations() {
    const yards = currentSettings.paceDistance;
    const pacePer50 = currentSettings.speed;
    const pacePerYard = pacePer50 / 50;
    const totalTime = yards * pacePerYard;
    const totalWithRest = totalTime + currentSettings.restTime;

    document.getElementById('totalTime').textContent = totalTime.toFixed(1) + 's';
    document.getElementById('totalWithRest').textContent = totalWithRest.toFixed(1) + 's';

    const setDuration = currentSettings.numRounds * totalWithRest;
    document.getElementById('setDuration').textContent = (setDuration / 60).toFixed(1) + 'm';
}

async function sendSettings() {
    try {
        const response = await fetch('/settings', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(currentSettings)
        });
        return response.ok;
    } catch (error) {
        console.error('Error sending settings:', error);
        return false;
    }
}

async function getStatus() {
    try {
        const response = await fetch('/status');
        return response.ok ? await response.json() : null;
    } catch (error) {
        console.error('Error getting status:', error);
        return null;
    }
}

async function startPacer() {
    if (await sendSettings()) {
        const response = await fetch('/start', { method: 'POST' });
        if (response.ok) {
            currentSettings.isRunning = true;
            updatePacerButtons();
            startStatusUpdates();
        }
    }
}

async function stopPacer() {
    const response = await fetch('/stop', { method: 'POST' });
    if (response.ok) {
        currentSettings.isRunning = false;
        updatePacerButtons();
        stopStatusUpdates();
    }
}

function updatePacerButtons() {
    const startBtn = document.getElementById('startBtn');
    const stopBtn = document.getElementById('stopBtn');
    if (currentSettings.isRunning) {
        startBtn.style.display = 'none';
        stopBtn.style.display = 'block';
    } else {
        startBtn.style.display = 'block';
        stopBtn.style.display = 'none';
    }
}

function startStatusUpdates() {
    statusIntervals[currentSettings.currentLane] = setInterval(async () => {
        const status = await getStatus();
        if (status) updateStatus(status);
    }, 500);
}

function stopStatusUpdates() {
    statusIntervals.forEach((interval, i) => {
        if (interval) {
            clearInterval(interval);
            statusIntervals[i] = null;
        }
    });
}

function updateStatus(status) {
    const statusDiv = document.getElementById('detailedStatus');
    if (status.isRunning) {
        statusDiv.style.display = 'block';
        statusDiv.innerHTML = `
            <h4>Lane ${currentSettings.currentLane + 1} Status</h4>
            <p>Round: ${status.currentRound}/${currentSettings.numRounds}</p>
            <p>Phase: ${status.currentPhase}</p>
            <p>Time: ${status.elapsedTime}s</p>
        `;
    } else {
        statusDiv.style.display = 'none';
    }
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    updateCalculations();
    updatePacerButtons();
    showPage('main');
});