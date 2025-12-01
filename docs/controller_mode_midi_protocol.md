# Deluge Controller Mode - MIDI Protocol Specification

## Overview

Controller Mode transforms Deluge into a generic MIDI controller where ALL hardware inputs send MIDI messages and ALL outputs (LEDs, display) are controlled via incoming MIDI. This allows remote scripts in DAWs (Ableton Live, Bitwig Studio, etc.) to implement custom controller behavior.

## Architecture

- **Deluge Hardware** → Sends all inputs as MIDI → **DAW Remote Script**
- **DAW Remote Script** → Sends LED/display commands as MIDI → **Deluge Hardware**

The Deluge acts as a "dumb" controller - all intelligence resides in the remote script.

## Configuration

Default configuration (customizable via `ControllerModeConfig`):

```
MIDI Channel: 1 (channel 0 in code)
Grid Size: 16x8 (128 pads)
Grid Base Note: 0
Button Base Note: 100
Encoder Base CC: 71
Note Layout: Row-major (note = base + y*width + x)
```

## MIDI Messages FROM Deluge (Outputs)

### Pad Grid (16x8 = 128 pads)

**Note On/Off Messages:**
```
Status: 0x90 (Note On, channel 1)
Note:   gridBaseNote + (y * gridWidth) + x
        Default: 0-127 for full 16x8 grid
Velocity: 127 (fixed - Deluge hardware does not support velocity sensing)

Status: 0x80 (Note Off, channel 1)
Note:   Same as above
Velocity: 0
```

**Note:** Deluge does not support velocity sensing or polyphonic aftertouch.

### Buttons

All buttons send **Note On/Off** messages:

```
Status: 0x90/0x80 (Note On/Off, channel 1)
Note:   buttonBaseNote + offset
Velocity: 127 (pressed) / 0 (released)
```

**Button Mapping:**
```
PLAY = 100           RECORD = 101         TAP_TEMPO = 102
SYNC_SCALING = 103   LEARN = 104          SCALE = 105
CROSS_SCREEN = 106   BACK = 107           LOAD = 108
SAVE = 109           KEYBOARD = 110       KIT = 111
SYNTH = 112          MIDI = 113           CV = 114
CLIP = 115           SONG = 116           AFFECT_ENTIRE = 117
SHIFT = 118          SELECT_ENC = 119

Gold Encoder Buttons (8 encoders): 120-127
Mod Encoder Buttons (8 encoders): 130-137
```

### Encoders

All encoders send **CC messages** with **relative encoding**:

```
Status: 0xB0 (CC, channel 1)
CC Number: encoderBaseCC + encoder_id
Value: 64 + delta
       64 = no change
       <64 = counter-clockwise (63, 62, 61...)
       >64 = clockwise (65, 66, 67...)
```

**Encoder Mapping:**
```
Gold Knobs 0-7:      CC 71-78
Horizontal Encoder:  CC 79
Vertical Encoder:    CC 80
Select Encoder:      CC 81
```

## MIDI Messages TO Deluge (Inputs)

### Pad LED Control - Simple (Velocity-based colors)

**Note On (set color):**
```
Status: 0x90 (Note On, channel 1)
Note:   Pad note (same as output mapping)
Velocity: Color index (0-127)

Color Palette:
  0       = Off (black)
  1       = Dim white (30,30,30)
  2-15    = Red (255,0,0)
  16-31   = Orange (255,127,0)
  32-47   = Yellow (255,255,0)
  48-63   = Green (0,255,0)
  64-79   = Cyan (0,255,255)
  80-95   = Blue (0,0,255)
  96-111  = Magenta (255,0,255)
  112-127 = White (255,255,255)
```

**Note Off (turn off LED):**
```
Status: 0x80 (Note Off, channel 1)
Note:   Pad note
Velocity: 0 (ignored)
```

### Pad LED Control - Full RGB (SysEx)

For precise RGB control:

```
F0 00 21 7D 01 20 xx yy rr gg bb F7

Where:
  F0           = SysEx start
  00 21 7D     = Manufacturer ID (Synthstrom)
  01           = Device ID (Deluge)
  20           = Command: SET_LED_COLOR
  xx           = Pad X coordinate (0-15)
  yy           = Pad Y coordinate (0-7)
  rr           = Red value (0-255)
  gg           = Green value (0-255)
  bb           = Blue value (0-255)
  F7           = SysEx end
```

### Button LED Control

```
Status: 0x90 (Note On = LED on) / 0x80 (Note Off = LED off)
Note:   Button note (same as button mapping)
Velocity: >0 for on, 0 for off
```

### Encoder LED Control

Gold encoder LEDs can be controlled individually:

```
Status: 0xB0 (CC, channel 1)
CC Number: encoderBaseCC + 20 + encoder_id
           Default: CC 91-98 (for encoders 0-7)
Value: 0-127 (LED brightness/state)
       0     = Off
       1-127 = On (brightness level if supported)
```

**Example:**
```
Control encoder 0 LED: CC 91
Control encoder 7 LED: CC 98
```

### Display Control (SysEx)

**Set Display Text:**
```
F0 00 21 7D 01 10 [text bytes...] F7

Example: "HELLO"
F0 00 21 7D 01 10 48 45 4C 4C 4F F7
```

**Set 7-Segment Display:**
```
F0 00 21 7D 01 11 ss ss ss ss F7

Where ss = segment data for each digit
```

**Set OLED Pixels:**
```
F0 00 21 7D 01 12 xx yy ww hh [pixel_data...] F7

Where:
  xx, yy = Starting position
  ww, hh = Width, height
  pixel_data = Pixel values
```

### Device Identity

**Device Inquiry (from DAW):**
```
F0 7E 00 06 01 F7
```

**Identity Reply (from Deluge):**
```
F0 7E 00 06 02 00 21 7D 00 01 00 01 01 00 00 00 F7

Where:
  00 21 7D = Manufacturer ID (Synthstrom)
  00 01    = Device family (Deluge)
  00 01    = Device model
  01 00 00 00 = Software version
```

## SysEx Command Summary

```
Command ID | Description
-----------|-------------
0x01       | Device Inquiry
0x10       | Set Display Text
0x11       | Set 7-Segment Display
0x12       | Set OLED Pixels
0x20       | Set LED Color (RGB)
0x21       | Set All LEDs (bulk operation)
```

## Usage Example: Ableton Live Remote Script

```python
# Simple example remote script

def on_pad_pressed(note, velocity):
    """Deluge sent us a pad press (velocity is always 127)"""
    # Calculate x, y from note number
    x = note % 16
    y = note // 16

    # Launch clip at this position
    session.scene(y).clip_slot(x).fire()

    # Set LED to green
    send_midi((0x90, note, 48))  # Green color index

def set_led_rgb(x, y, r, g, b):
    """Set specific pad to exact RGB color"""
    sysex = [0xF0, 0x00, 0x21, 0x7D, 0x01, 0x20,
             x, y, r, g, b, 0xF7]
    send_sysex(sysex)

def show_text(text):
    """Display text on Deluge screen"""
    sysex = [0xF0, 0x00, 0x21, 0x7D, 0x01, 0x10]
    sysex.extend([ord(c) for c in text])
    sysex.append(0xF7)
    send_sysex(sysex)
```

## Configuration Customization

To change grid layout (e.g., for Push 2 compatibility):

```cpp
ControllerModeConfig config;
config.gridWidth = 8;          // 8 columns
config.gridHeight = 8;         // 8 rows
config.gridBaseNote = 36;      // Start at MIDI note 36 (like Push)
config.buttonBaseNote = 100;
config.encoderBaseCC = 71;
config.rowMajorNotes = true;

controllerModeView.setConfig(config);
```

## Performance Characteristics

See [controller_mode_bandwidth_latency.md](controller_mode_bandwidth_latency.md) for detailed analysis of:
- MIDI bandwidth usage (typically <1% on USB MIDI)
- Round-trip latency (5-10ms typical)
- Comparison to commercial controllers
- **Recommendation: Always use USB MIDI** (not DIN MIDI) for optimal performance

## Notes

- **Exit**: Press SHIFT+BACK to exit controller mode
- **MIDI Channel**: All messages on configured channel (default: channel 1)
- **Hardware Limitations**: Deluge does not support velocity sensing or polyphonic aftertouch
- **SysEx**: Can be disabled via config for security
- **Color Palette**: Simple velocity-based palette for quick scripting
- **Full RGB**: SysEx for precise color control

## Remote Script Development

1. **Initialize**: Send device inquiry, wait for identity reply
2. **Setup**: Configure LEDs, display initial state
3. **Handle Inputs**: Process all pad/button/encoder messages
4. **Update Outputs**: Send LED/display commands as needed
5. **Cleanup**: Clear LEDs when script exits

The remote script has complete control over Deluge's functionality in controller mode!
