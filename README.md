# ESP32 Swim Pacer

A professional swim training pacer system built on ESP32 with a web-based interface for creating and managing swim sets across multiple lanes.

## ✅ VERIFIED WORKING CONFIGURATION

**Status:** Fully functional as of November 2, 2025
**Board:** ESP32 Dev Module
**SPIFFS:** Optimized to 1% usage (14KB of 1.4MB)
**Web Interface:** Complete 3-tab interface operational

### Quick Deploy
1. Run: `.\deploy-spiffs.ps1 -Port COM7`
2. Upload sketch via Arduino IDE ("ESP32 Dev Module")
3. Connect to WiFi: `SwimPacer_Config`
4. Browse: `http://192.168.4.1`

### GPIO Pin Assignments
- **Lane 1:** GPIO 18
- **Lane 2:** GPIO 19
- **Lane 3:** GPIO 21
- **Lane 4:** GPIO 2

## Features

### 🏊‍♂️ Professional Swim Training
- **Queue-based workout management** - Create, queue, and execute swim sets
- **Multi-lane support** - Independent timing for up to 4 swim lanes
- **Customizable intervals** - Set pace per 50m, rest intervals, and round counts
- **Real-time timing** - Precise countdown and interval timing with LED indicators

### 🎯 Modern Interface
- **Dynamic visual feedback** - Color-coded status indicators (red/green borders)
- **Responsive design** - Clean, professional interface optimized for pool deck use
- **Queue management** - Add, edit, and reorder swim sets before execution
- **In-place completion tracking** - Visual checkmarks for completed sets

### ⚡ Technical Capabilities
- **ESP32-based** - Reliable hardware platform with Wi-Fi connectivity
- **Web interface** - No app installation required, works on any device
- **Embedded HTML** - Complete interface stored in firmware for offline operation
- **Synchronized development** - Python scripts for seamless HTML/firmware updates

## Quick Start

### Hardware Requirements
- ESP32-WROOM-DA Module (ESP32-D0WD-V3 revision 3.1 or compatible)
- WS2812B LED strip(s) for lane timing visualization
- 5V power supply for LED strips

### One-Command Deployment
```powershell
.\deploy.ps1
```

This single script handles:
- ✅ Arduino sketch compilation and upload
- ✅ SPIFFS filesystem creation and upload
- ✅ Progress monitoring and error handling
- ✅ Automatic tool detection and configuration

### Manual Setup (if needed)
1. Install Arduino CLI and ESP32 board package
2. Set board configuration to `esp32:esp32:esp32da` (ESP32-WROOM-DA Module)
3. Upload sketch: `arduino-cli upload --fqbn esp32:esp32:esp32da --port COM7 swim_pacer.ino`
4. Upload SPIFFS: `esptool write_flash 0x290000 spiffs.bin`

### Access the Interface
1. Connect to Wi-Fi network: **"SwimPacer_Config"** (no password)
2. Open browser to: **http://192.168.4.1**
3. Start configuring your swim workouts!

### Creating Swim Sets
1. Configure your swim set parameters (pace, rest, rounds)
2. Click "Create & Add to Queue" to queue the set
3. Use "Start Queue" to begin executing queued sets
4. Monitor progress with real-time visual indicators

## File Structure

```
├── config_interface.html    # Standalone web interface
├── swim_pacer.ino          # ESP32 firmware with embedded HTML
├── update_esp32_html.py    # Sync script for development
├── DEVELOPMENT_STATUS.md   # Development documentation
├── SYNC_WORKFLOW.md       # Development workflow guide
└── .gitignore             # Git configuration
```

## Development

### Workflow
1. Edit `config_interface.html` for interface changes
2. Run `python update_esp32_html.py` to sync to firmware
3. Flash updated `.ino` file to ESP32

### Key Features Implemented
- ✅ Queue-based swim set management
- ✅ Multi-lane independent operation
- ✅ Dynamic visual status indicators
- ✅ Professional swimming terminology
- ✅ Comprehensive error handling
- ✅ Real-time timing and countdown

## Technical Details

**Hardware:** ESP32 microcontroller
**Interface:** HTML5/CSS3/JavaScript
**Communication:** HTTP requests between web interface and ESP32
**Timing:** Millisecond-precision interval management
**Storage:** SPIFFS for configuration persistence

## License

Open source project - feel free to use, modify, and distribute.

## Contributing

This is an active development project. Contributions, suggestions, and improvements are welcome!

---

*Built for swimmers, by swimmers. Perfect for coaches, clubs, and individual training.*