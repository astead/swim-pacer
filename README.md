# ESP32 Swim Pacer

A professional swim training pacer system built on ESP32 with a web-based interface for creating and managing swim sets across multiple lanes.

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

### Hardware Setup
1. Flash `swim_pacer.ino` to your ESP32 board
2. Connect ESP32 to Wi-Fi network
3. Access the web interface via ESP32's IP address

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