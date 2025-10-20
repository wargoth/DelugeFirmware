# Deluge MIDI SysEx Communication Specification

## Overview

The Synthstrom Audible Deluge implements a comprehensive MIDI System Exclusive (SysEx) protocol for bi-directional communication with external devices and host computers. This specification covers USB device identification, protocol structure, command sets, and implementation examples.

## USB Device Identification

### Deluge USB MIDI Interface

The Deluge presents itself as a USB MIDI device with the following identification:

- **Vendor ID (VID)**: `0x16D0`
- **Product ID (PID)**: `0x0CE2`
- **Device Class**: `0` (Composite Device)
- **Interface Class**: `1` (Audio)
- **Interface Subclass**: `3` (MIDI Streaming)
- **USB Speed**: High Speed (USB 2.0)
- **Endpoints**: 2 (IN/OUT)

### Device Discovery Code Example

```python
import usb.core

# Find Deluge USB device
deluge_dev = usb.core.find(idVendor=0x16D0, idProduct=0x0CE2)
if deluge_dev:
    print(f"Found Deluge: VID={deluge_dev.idVendor:04X}, PID={deluge_dev.idProduct:04X}")
```

## SysEx Protocol Structure

### Basic Message Format

All Deluge SysEx messages follow this structure:

```
F0 00 21 7B 01 <CMD> [DATA...] F7
```

Where:
- `F0`: SysEx Start byte
- `00 21 7B 01`: Deluge manufacturer ID (4 bytes)
- `CMD`: Command type (1 byte)
- `DATA`: Command-specific payload (variable length)
- `F7`: SysEx End byte

### Manufacturer ID Breakdown

```cpp
const uint8_t DELUGE_SYSEX_ID_BYTE0 = 0x00;  // Universal non-real-time
const uint8_t DELUGE_SYSEX_ID_BYTE1 = 0x21;  // Educational/development use
const uint8_t DELUGE_SYSEX_ID_BYTE2 = 0x7B;  // Synthstrom Audible
const uint8_t DELUGE_SYSEX_ID_BYTE3 = 0x01;  // Deluge device ID
```

## Command Types

The Deluge supports several SysEx command categories:

```cpp
enum SysexCommands : uint8_t {
    Ping = 0,       // Connectivity test
    Popup = 1,      // Display message on Deluge screen
    HID = 2,        // Human Interface Device access
    Debug = 3,      // Debug/development commands
    Json = 4,       // JSON-based file operations
    JsonReply = 5,  // JSON response messages
    Pong = 0x7F     // Ping response
};
```

## Data Encoding

### 7-bit to 8-bit Packing

Binary data in SysEx messages is encoded using a 7-to-8 bit packing scheme to ensure compatibility with MIDI's 7-bit data constraint:

- Groups of 7 bytes are encoded as 8 bytes
- First byte contains the high bits (MSB) of the following 7 bytes
- Remaining 7 bytes have their MSB cleared

```cpp
// Pack 8-bit data to 7-bit MIDI format
int32_t pack_8bit_to_7bit(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len);

// Unpack 7-bit MIDI data to 8-bit format
int32_t unpack_7bit_to_8bit(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len);
```

### RLE Compression

For display data, the Deluge uses Run-Length Encoding (RLE) compression:

```cpp
// Pack with RLE compression
int32_t pack_8to7_rle(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len);

// Unpack RLE compressed data
int32_t unpack_7to8_rle(uint8_t* dst, int32_t dst_size, uint8_t* src, int32_t src_len);
```

## Command Implementations

### 1. Ping/Pong (Connectivity Test)

**Ping Request:**
```
F0 00 21 7B 01 00 F7
```

**Pong Response:**
```
F0 00 21 7B 01 7F F7
```

**C++ Implementation:**
```cpp
void sendPing(MIDICable& cable) {
    uint8_t ping[] = {0xF0, 0x00, 0x21, 0x7B, 0x01, 0x00, 0xF7};
    cable.sendSysex(ping, sizeof(ping));
}

void handlePong(uint8_t* data, int32_t len) {
    if (len >= 7 && data[5] == 0x7F) {
        // Pong received
        D_PRINTLN("Deluge responded to ping");
    }
}
```

### 2. Popup Messages

Display a text message on the Deluge screen:

```
F0 00 21 7B 01 01 <text_data> F7
```

**Example:**
```cpp
void sendPopupMessage(MIDICable& cable, const char* message) {
    uint8_t header[] = {0xF0, 0x00, 0x21, 0x7B, 0x01, 0x01};
    uint8_t footer[] = {0xF7};
    
    // Send header
    cable.sendSysex(header, sizeof(header));
    
    // Send message (ensure 7-bit clean)
    size_t len = strlen(message);
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = message[i] & 0x7F;
        cable.sendSysex(&byte, 1);
    }
    
    // Send footer
    cable.sendSysex(footer, sizeof(footer));
}
```

### 3. HID (Display) Commands

The HID subsystem provides access to the Deluge's display and controls:

#### OLED Display Data Request

```
F0 00 21 7B 01 02 <subcmd> F7
```

**Subcommands:**
- `00`: Request current display (uncompressed)
- `01`: Request current display (RLE compressed)  
- `02`: Start continuous display updates (uncompressed)
- `03`: Start continuous display updates (forced/compressed)
- `04`: Swap display type

**Implementation:**
```cpp
void requestOLEDDisplay(MIDICable& cable, bool compressed) {
    uint8_t cmd[] = {0xF0, 0x00, 0x21, 0x7B, 0x01, 0x02, 
                     compressed ? 0x01_u8 : 0x00_u8, 0xF7};
    cable.sendSysex(cmd, sizeof(cmd));
}

void handleOLEDData(uint8_t* data, int32_t len) {
    if (len < 9) return;
    
    bool compressed = (data[7] == 0x01);
    uint8_t startPos = data[8];
    
    // Extract display data
    int32_t dataSize = len - 10; // Remove header + footer
    uint8_t displayBuffer[OLED_BUFFER_SIZE];
    
    if (compressed) {
        unpack_7to8_rle(displayBuffer, sizeof(displayBuffer), 
                        data + 9, dataSize);
    } else {
        unpack_7bit_to_8bit(displayBuffer, sizeof(displayBuffer), 
                           data + 9, dataSize);
    }
    
    // Process display data (128x64 monochrome bitmap)
    processOLEDFrame(displayBuffer);
}
```

#### 7-Segment Display Request

```
F0 00 21 7B 01 02 01 00 F7
```

### 4. Debug Commands

Debug commands provide development and diagnostic capabilities:

```
F0 00 21 7B 01 03 <subcmd> [data] F7
```

**Debug Subcommands:**
- `00`: Enable/disable debug output
- `01`: Load firmware packet (development)
- `02`: Verify and run loaded firmware (development)

**Enable Debug Output:**
```cpp
void enableDebugOutput(MIDICable& cable, bool enable) {
    uint8_t cmd[] = {0xF0, 0x00, 0x21, 0x7B, 0x01, 0x03, 0x00, 
                     enable ? 0x01_u8 : 0x00_u8, 0xF7};
    cable.sendSysex(cmd, sizeof(cmd));
}
```

### 5. JSON File Operations

The Deluge implements a JSON-based file system protocol for reading/writing songs, samples, and settings:

```
F0 00 21 7B 01 04 <seq> <json_data> [00] [binary_data] F7
```

**Components:**
- `seq`: Sequence number (1-127) for request/response matching
- `json_data`: JSON command object
- `00`: Optional separator for binary data
- `binary_data`: Optional 7-bit packed binary payload

#### JSON Protocol Structure

**Request Format:**
```json
{"command": {"param1": "value1", "param2": "value2"}}
```

**Response Format:**
```json
{"^command": {"result": "value", "err": 0}}
```

#### File Operations

**Open File:**
```json
{"open": {"path": "/SONGS/SONG001.XML", "write": 0}}
```

**Response:**
```json
{"^open": {"fid": 1, "size": 2048, "err": 0}}
```

**Read File:**
```json
{"read": {"fid": 1, "offset": 0, "length": 512}}
```

**Response includes binary data:**
```
F0 00 21 7B 01 05 <seq> <json_response> 00 <packed_file_data> F7
```

**Directory Listing:**
```json
{"dir": {"path": "/SONGS", "offset": 0}}
```

**Delete File:**
```json
{"delete": {"path": "/SONGS/SONG001.XML"}}
```

**Create Directory:**
```json
{"mkdir": {"path": "/SAMPLES/NEW_FOLDER"}}
```

**C++ JSON Implementation Example:**
```cpp
void sendFileOpenRequest(MIDICable& cable, const char* path, bool write) {
    char jsonCmd[256];
    snprintf(jsonCmd, sizeof(jsonCmd), 
             "{\"open\": {\"path\": \"%s\", \"write\": %d}}", 
             path, write ? 1 : 0);
    
    uint8_t header[] = {0xF0, 0x00, 0x21, 0x7B, 0x01, 0x04, 0x01}; // seq=1
    cable.sendSysex(header, sizeof(header));
    
    // Send JSON (7-bit clean)
    size_t len = strlen(jsonCmd);
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = jsonCmd[i] & 0x7F;
        cable.sendSysex(&byte, 1);
    }
    
    uint8_t footer[] = {0xF7};
    cable.sendSysex(footer, sizeof(footer));
}
```

## Device-Specific Extensions

### Lumi Keys Integration

The Deluge includes specific support for ROLI Lumi Keys controllers:

- **Vendor ID**: `0x2af4`
- **Product ID**: `0xe00`

**Features:**
- Automatic MPE mode configuration
- Scale and root note synchronization
- Color-coded key lighting
- Multi-zone support

```cpp
class MIDIDeviceLumiKeys : public MIDICableUSBHosted {
    static constexpr uint16_t lumiKeysVendorProductPairs[1][2] = {{0x2af4, 0xe00}};
    
    void setRootNote(int16_t rootNote);
    void setScale(Scale scale);
    void setMIDIMode(MIDIMode midiMode);
    void setMPEZone(MPEZone mpeZone);
};
```

## Error Handling

### Common Error Codes

JSON responses include error codes:

- `0`: Success
- `1`: File not found
- `2`: Access denied
- `3`: Insufficient space
- `4`: Invalid parameter

### Timeout Handling

- Request timeout: 5 seconds
- Continuous display updates: Auto-stop after 2 seconds of inactivity
- Buffer overflow protection: Messages truncated at buffer limits

## Performance Considerations

### Buffer Management

```cpp
// SysEx formatting buffer (shared)
uint8_t sysex_fmt_buffer[1024];

// Display update throttling
if (cable.sendBufferSpace() < 512) {
    // Throttle updates when buffer is > 50% full
    uiTimerManager.setTimer(TimerName::SYSEX_DISPLAY, 100);
    return;
}
```

### Real-time Constraints

- SysEx processing occurs on the main thread (non-audio)
- File operations check `sdRoutineLock` to avoid audio interruption
- Display updates are throttled based on USB buffer availability

## Implementation Examples

### Python Host Application

```python
import rtmidi
import json
import time

class DelugeController:
    def __init__(self):
        self.midi_out = rtmidi.MidiOut()
        self.midi_in = rtmidi.MidiIn()
        self.seq_num = 1
        
        # Find Deluge MIDI ports
        out_ports = self.midi_out.get_ports()
        in_ports = self.midi_in.get_ports()
        
        deluge_out = next((i for i, name in enumerate(out_ports) 
                          if 'Deluge' in name), None)
        deluge_in = next((i for i, name in enumerate(in_ports) 
                         if 'Deluge' in name), None)
        
        if deluge_out is not None:
            self.midi_out.open_port(deluge_out)
        if deluge_in is not None:
            self.midi_in.open_port(deluge_in)
    
    def send_sysex(self, data):
        """Send SysEx message to Deluge"""
        self.midi_out.send_message(data)
    
    def ping(self):
        """Send ping to Deluge"""
        ping_msg = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x00, 0xF7]
        self.send_sysex(ping_msg)
    
    def popup(self, text):
        """Display popup message on Deluge"""
        header = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x01]
        footer = [0xF7]
        
        # Convert text to 7-bit clean bytes
        text_bytes = [ord(c) & 0x7F for c in text]
        
        message = header + text_bytes + footer
        self.send_sysex(message)
    
    def file_operation(self, command_dict):
        """Send JSON file operation command"""
        json_str = json.dumps(command_dict)
        
        header = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x04, self.seq_num]
        json_bytes = [ord(c) & 0x7F for c in json_str]
        footer = [0xF7]
        
        message = header + json_bytes + footer
        self.send_sysex(message)
        
        self.seq_num = (self.seq_num % 127) + 1
        return self.seq_num - 1
    
    def open_song(self, path, write=False):
        """Open a song file"""
        cmd = {"open": {"path": path, "write": 1 if write else 0}}
        return self.file_operation(cmd)
    
    def read_file(self, fid, offset=0, length=512):
        """Read from open file"""
        cmd = {"read": {"fid": fid, "offset": offset, "length": length}}
        return self.file_operation(cmd)

# Usage example
deluge = DelugeController()
deluge.ping()
deluge.popup("Hello from Python!")

# Open a song file
seq = deluge.open_song("/SONGS/SONG001.XML", write=False)
print(f"Sent open command with sequence {seq}")
```

### Web Interface Integration

The Deluge's JSON protocol is designed for web browser integration:

```javascript
class DelugeWebInterface {
    constructor() {
        this.sequenceCallbacks = new Map();
        this.currentSequence = 1;
    }
    
    async requestMIDIAccess() {
        this.midiAccess = await navigator.requestMIDIAccess({ sysex: true });
        
        // Find Deluge
        for (let output of this.midiAccess.outputs.values()) {
            if (output.name.includes('Deluge')) {
                this.midiOutput = output;
                break;
            }
        }
        
        for (let input of this.midiAccess.inputs.values()) {
            if (input.name.includes('Deluge')) {
                this.midiInput = input;
                this.midiInput.onmidimessage = this.handleMidiMessage.bind(this);
                break;
            }
        }
    }
    
    sendJsonCommand(command, callback) {
        const sequence = this.currentSequence++;
        if (this.currentSequence > 127) this.currentSequence = 1;
        
        if (callback) {
            this.sequenceCallbacks.set(sequence, callback);
        }
        
        const jsonStr = JSON.stringify(command);
        const header = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x04, sequence];
        const jsonBytes = Array.from(jsonStr).map(c => c.charCodeAt(0) & 0x7F);
        const message = new Uint8Array([...header, ...jsonBytes, 0xF7]);
        
        this.midiOutput.send(message);
        return sequence;
    }
    
    handleMidiMessage(event) {
        const data = Array.from(event.data);
        
        // Check for Deluge SysEx message
        if (data.length >= 7 && 
            data[0] === 0xF0 && data[1] === 0x00 && 
            data[2] === 0x21 && data[3] === 0x7B && data[4] === 0x01) {
            
            const command = data[5];
            
            if (command === 0x05) { // JSON Reply
                this.handleJsonReply(data);
            }
        }
    }
    
    handleJsonReply(data) {
        const sequence = data[6];
        const jsonData = data.slice(7, -1); // Remove header and 0xF7
        const jsonStr = String.fromCharCode(...jsonData);
        
        try {
            const response = JSON.parse(jsonStr);
            const callback = this.sequenceCallbacks.get(sequence);
            
            if (callback) {
                callback(response);
                this.sequenceCallbacks.delete(sequence);
            }
        } catch (e) {
            console.error('Failed to parse JSON response:', e);
        }
    }
}

// Usage
const deluge = new DelugeWebInterface();
await deluge.requestMIDIAccess();

deluge.sendJsonCommand(
    {"dir": {"path": "/SONGS", "offset": 0}},
    (response) => {
        console.log('Directory listing:', response);
    }
);
```

## Protocol Extensions and Future Development

### Planned Extensions

- **Audio streaming**: Real-time audio data transfer
- **Parameter automation**: Automated parameter control
- **Preset management**: Synth preset upload/download
- **Sample streaming**: Direct sample loading over MIDI

### Custom Command Implementation

To add new SysEx commands to the Deluge firmware:

1. **Add command enum:**
```cpp
enum SysexCommands : uint8_t {
    // ... existing commands ...
    CustomCommand = 6,
};
```

2. **Implement handler:**
```cpp
void handleCustomCommand(MIDICable& cable, uint8_t* data, int32_t len) {
    // Process custom command
    if (len < 3) return;
    
    uint8_t subCommand = data[1];
    // Handle subcommands...
    
    // Send response if needed
    uint8_t response[] = {0xF0, 0x00, 0x21, 0x7B, 0x01, 0x06, 0x00, 0xF7};
    cable.sendSysex(response, sizeof(response));
}
```

3. **Register in dispatcher:**
```cpp
void sysexReceived(MIDICable& cable, uint8_t* data, int32_t len) {
    if (len < 6) return;
    
    // Verify Deluge ID
    if (data[1] != 0x00 || data[2] != 0x21 || data[3] != 0x7B || data[4] != 0x01) {
        return;
    }
    
    switch (data[5]) {
        case CustomCommand:
            handleCustomCommand(cable, data + 6, len - 7);
            break;
        // ... other cases ...
    }
}
```

---

This specification provides a complete reference for implementing MIDI SysEx communication with the Synthstrom Audible Deluge. The protocol's modular design allows for both simple operations (ping/pong, popups) and complex file system interactions, making it suitable for a wide range of applications from basic device control to comprehensive DAW integration.