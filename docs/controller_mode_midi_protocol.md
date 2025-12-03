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
Button Base Note: 0 (used only for LED control mapping)
Encoder Base CC: 71
Note Layout: Row-major (note = base + y*width + x)
```

## MIDI Messages FROM Deluge (Outputs)

### Main Pad Grid (16x8 = 128 pads)

**Note On/Off Messages:**
```
Channel: config.midiChannel (default: 0 = MIDI channel 1)
Status: 0x90 (Note On, channel 1)
Note:   gridBaseNote + (y * gridWidth) + x
        Default: 0-127 for full 16x8 grid
Velocity: 127 (fixed - Deluge hardware does not support velocity sensing)

Status: 0x80 (Note Off, channel 1)
Note:   Same as above
Velocity: 0
```

**Note:** Deluge does not support velocity sensing or polyphonic aftertouch.

### Sidebar Pads (2 columns × 8 rows = 16 pads)

**SysEx Messages:**
```
F0 00 21 7B 01 30 [sidebar_col] [y] [state] F7

Where:
  F0           = SysEx start
  00 21 7B     = Manufacturer ID (Synthstrom)
  01           = Device ID (Deluge)
  30           = Command: SIDEBAR_PAD_EVENT
  sidebar_col  = Sidebar column (0-1 for x=16 and x=17)
  y            = Row (0-7)
  state        = 0x7F (pressed) / 0x00 (released)
  F7           = SysEx end
```

### Buttons

All buttons send **SysEx messages**:

```
F0 00 21 7B 01 40 [button_id] [state] F7

Where:
  F0           = SysEx start
  00 21 7B     = Manufacturer ID (Synthstrom)
  01           = Device ID (Deluge)
  40           = Command: BUTTON_EVENT
  button_id    = Button identifier (0-39)
  state        = 0x7F (pressed) / 0x00 (released)
  F7           = SysEx end
```

**Button ID Mapping:**
```
Main Buttons (0-23):
0=PLAY            1=RECORD          2=TAP_TEMPO       3=SYNC_SCALING
4=LEARN           5=SCALE_MODE      6=CROSS_SCREEN    7=BACK
8=LOAD            9=SAVE            10=KEYBOARD       11=KIT
12=SYNTH          13=MIDI           14=CV             15=CLIP_VIEW
16=SESSION_VIEW   17=AFFECT_ENTIRE  18=SHIFT          19=SELECT_ENC
20=TRIPLETS       21=X_ENC          22=Y_ENC          23=TEMPO_ENC

Gold Encoder Buttons (24-31):
24-31 = Gold knobs 0-7 (push buttons)

Mod Encoder Buttons (32-39):
32-39 = Mod/effect buttons 0-7
```

**Rationale:** Buttons use SysEx to avoid the 128-note limitation and provide clearer mapping independent of MIDI note assignments.

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
Gold Knobs 0-7:      CC 71-78 (encoderBaseCC + 0 to 7)
Horizontal Encoder:  CC 79 (encoderBaseCC + 8)
Vertical Encoder:    CC 80 (encoderBaseCC + 9)
Select Encoder:      CC 81 (encoderBaseCC + 10)
Tempo Encoder:       CC 73 (fixed - gold knob 2 position)
```

**Note:** The tempo encoder shares physical position with gold knob 2 (CC 73) but also allows firmware tempo changes.

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

### Sidebar LED Control - Full RGB (SysEx)

**SysEx message:**
```
F0 00 21 7B 01 31 [sidebar_col] [y] [r] [g] [b] F7

Where:
  F0           = SysEx start
  00 21 7B     = Manufacturer ID (Synthstrom)
  01           = Device ID (Deluge)
  31           = Command: SIDEBAR_LED_CONTROL
  sidebar_col  = Sidebar column (0-1 for the two columns)
  y            = Row (0-7)
  r            = Red value (0-127, scaled to 0-255 internally)
  g            = Green value (0-127, scaled to 0-255 internally)
  b            = Blue value (0-127, scaled to 0-255 internally)
  F7           = SysEx end
```

**Note:** Sidebar LEDs only support full RGB control via SysEx, not velocity-based colors.

### Pad LED Control - Full RGB (SysEx)

For precise RGB control:

```
F0 00 21 7B 01 20 xx yy rr gg bb F7

Where:
  F0           = SysEx start
  00 21 7B     = Manufacturer ID (Synthstrom)
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
Button LEDs are controlled via **SysEx**.

**SysEx Message:**
```
F0 00 21 7B 01 41 [button_id] [state] F7

Where:
  button_id = Button identifier (0-39)
  state     = 0 (Off) or 1 (On)
```

**Note:** SysEx (0x41) supports all buttons (including Gold/Mod buttons) and avoids MIDI note conflicts.

### Batch LED Update (SysEx)

To update multiple LEDs in a single message (efficient for animations):

```
F0 00 21 7B 01 22 [x1] [y1] [r1] [g1] [b1] [x2] [y2] [r2] [g2] [b2] ... F7

Where:
  22           = Command: BATCH_LED_UPDATE
  x, y         = Pad coordinates (x=0-15 for main grid, x=16-17 for sidebar)
  r, g, b      = RGB values (0-127, scaled to 0-255 internally)
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
F0 00 21 7B 01 10 [text bytes...] F7

Example: "HELLO"
F0 00 21 7B 01 10 48 45 4C 4C 4F F7
```

**Set 7-Segment Display (Text):**
```
F0 00 21 7B 01 11 [text bytes...] F7

Maximum 4 characters for 7-segment display.
Example: "A440"
F0 00 21 7B 01 11 41 34 34 30 F7
```

**Note:** The 7-segment display command accepts ASCII text (up to 4 characters), not raw segment data. The firmware will automatically convert the text to the appropriate 7-segment encoding.

**Set OLED Pixels:**
```
F0 00 21 7B 01 12 xx yy ww hh [pixel_data...] F7

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
F0 7E 00 06 02 00 21 7B 00 01 00 01 01 00 00 00 F7

Where:
  00 21 7B = Manufacturer ID (Synthstrom)
  00 01    = Device family (Deluge)
  00 01    = Device model
  01 00 00 xx = Software version
              xx: 00 = 7-segment display
                  01 = OLED display
```

## SysEx Command Summary

```
Command ID | Direction      | Description
-----------|----------------|-------------
0x01       | DAW -> Deluge  | Device Inquiry
0x10       | DAW -> Deluge  | Set Display Text
0x11       | DAW -> Deluge  | Set 7-Segment Display
0x12       | DAW -> Deluge  | Set OLED Pixels
0x20       | DAW -> Deluge  | Set LED Color (RGB) - Main Grid
0x21       | DAW -> Deluge  | Set All LEDs (bulk operation)
0x22       | DAW -> Deluge  | Batch LED Update (multiple LEDs)
0x30       | Deluge -> DAW  | Sidebar Pad Event (press/release)
0x31       | DAW -> Deluge  | Sidebar LED Control (RGB)
0x40       | Deluge -> DAW  | Button Event (press/release)
0x41       | DAW -> Deluge  | Button LED Control (On/Off)
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

def on_sidebar_pad_pressed(sysex_data):
    """Deluge sent us a sidebar pad press via SysEx"""
    # Parse: F0 00 21 7B 01 30 [col] [y] [state] F7
    if len(sysex_data) == 9 and sysex_data[5] == 0x30:
        col = sysex_data[6]  # 0 or 1
        y = sysex_data[7]    # 0-7
        pressed = sysex_data[8] == 0x7F

        if pressed:
            # Handle sidebar button press
            if col == 0:
                # Mute/unmute track
                session.scene(y).mute()
            else:
                # Solo track
                session.scene(y).solo()

def on_button_pressed(sysex_data):
    """Deluge sent us a button press via SysEx"""
    # Parse: F0 00 21 7B 01 40 [button_id] [state] F7
    if len(sysex_data) == 9 and sysex_data[5] == 0x40:
        button_id = sysex_data[6]
        pressed = sysex_data[7] == 0x7F

        # Button 0 = PLAY button
        if button_id == 0 and pressed:
            song.play()

def set_led_rgb(x, y, r, g, b):
    """Set specific pad to exact RGB color"""
    sysex = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x20,
             x, y, r, g, b, 0xF7]
    send_sysex(sysex)

def set_sidebar_led(col, y, r, g, b):
    """Set sidebar LED color (RGB 0-127 range)"""
    sysex = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x31,
             col, y, r, g, b, 0xF7]
    send_sysex(sysex)

def set_button_led(button_id, on):
    """Set button LED on/off via SysEx"""
    # F0 00 21 7B 01 41 [button_id] [state] F7
    sysex = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x41,
             button_id, 1 if on else 0, 0xF7]
    send_sysex(sysex)

def show_text(text):
    """Display text on Deluge screen"""
    sysex = [0xF0, 0x00, 0x21, 0x7B, 0x01, 0x10]
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
config.buttonBaseNote = 0;     // Button LED base note
config.encoderBaseCC = 71;     // Encoder CC base
config.rowMajorNotes = true;   // Row-major note layout
config.receiveDisplaySysex = true;  // Accept display control

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
- **MIDI Channels**:
  - Main grid pads/encoders: `config.midiChannel` (default: channel 1)
  - Sidebar pads: SysEx (0x30) - not MIDI notes
  - Buttons: SysEx (0x40) - not MIDI notes
- **Hardware Limitations**: Deluge does not support velocity sensing or polyphonic aftertouch
- **SysEx**: Can be disabled via config for security
- **Color Palette**: Simple velocity-based palette for quick scripting (main pads only)
- **Full RGB**: SysEx for precise color control (main pads and sidebar)
- **MIDI Loop Prevention**: Incoming MIDI is only accepted from the same USB cable used for output (see below)
- **Protocol Design**: Buttons and sidebar pads use SysEx to avoid the 128-note MIDI limitation and prevent conflicts

## MIDI Loop Prevention

**Problem:** Without proper filtering, MIDI messages sent from Deluge could be echoed back by the host computer's MIDI routing, creating infinite loops.

**Solution:** Controller mode implements bidirectional cable verification:

1. **Outgoing Messages:** All MIDI output uses a specific `activeCable_` (USB cable 0)
2. **Incoming Messages:** Only MIDI input from the **same cable** is accepted:
   ```cpp
   if (&fromCable != activeCable_) {
       return false;  // Ignore messages from other cables
   }
   ```

**Benefits:**
- Prevents MIDI loops even with software MIDI thru enabled
- Ensures remote script communicates only with its Deluge instance
- No conflict with DIN MIDI or other USB MIDI devices

**Recommendation:** Disable MIDI thru in your DAW or ensure routing doesn't echo controller mode messages back to Deluge.

## Remote Script Development

1. **Initialize**: Send device inquiry, wait for identity reply
2. **Setup**: Configure LEDs, display initial state
3. **Handle Inputs**: Process all pad/button/encoder messages
4. **Update Outputs**: Send LED/display commands as needed
5. **Cleanup**: Clear LEDs when script exits

The remote script has complete control over Deluge's functionality in controller mode!
