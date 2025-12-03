# Controller Mode Testing Guide

This guide explains how to test the Controller Mode implementation using the provided Python test script.

## Prerequisites

### Hardware
- Synthstrom Deluge with Controller Mode firmware flashed
- USB cable connected to computer
- Deluge must be in Controller Mode (accessible via Sound menu)

### Software
- Python 3.7 or later
- pip package manager

## Installation

1. Install Python dependencies:
```bash
pip install -r test_controller_mode_requirements.txt
```

Or manually:
```bash
pip install mido python-rtmidi
```

## Running the Test Script

1. **Connect Deluge via USB** to your computer

2. **Enter Controller Mode on Deluge:**
   - Power off the Deluge
   - Hold the **LEARN** button
   - Power on the Deluge while holding **LEARN**
   - Release **LEARN** once the display shows "CONTROLLER MODE" or similar

3. **Run the test script:**
```bash
python3 test_controller_mode.py
```

4. **Select MIDI ports:**
   - The script will attempt to auto-detect Deluge
   - If not found, manually select the input/output port numbers

## Test Script Features

### Startup Animation Sequence
When the script starts, it automatically plays:
1. **Test Grid** - Lights up corners (red) and edges (green/blue) to verify grid layout
2. **Spiral** - Colorful spiral from center outward
3. **Rainbow Wave** - Animated rainbow wave across the grid
4. **Pulse** - Pulsing white/blue pattern

### Interactive Commands

Once in interactive mode, you can use these commands:

| Command | Description |
|---------|-------------|
| `grid` | Show test grid pattern (corners + edges) |
| `rainbow` | Run rainbow wave animation (5 seconds) |
| `spiral` | Run spiral animation (3 seconds) |
| `pulse` | Run pulse animation (3 seconds) |
| `clear` | Turn off all pad LEDs |
| `buttons` | Test all button LEDs in sequence |
| `corner` | Light up corner pads in different colors |
| `status` | Show current state (pressed pads/buttons) |
| `quit` | Exit the test script |

### Real-time Feedback

The script provides real-time logging of all MIDI events:

**Pad Presses:**
```
[12:34:56.789] PAD: Pad ( 3,2) pressed - Note 35
[12:34:56.890] PAD: Pad ( 3,2) released - Note 35
```

**Button Presses:**
```
[12:34:57.123] BUTTON: PLAY pressed
[12:34:57.234] BUTTON: PLAY released
```

**Encoder Changes:**
```
[12:34:58.456] ENCODER: SELECT_ENCODER = 68 (delta: +4)
[12:34:58.567] ENCODER: HORIZONTAL_ENCODER = 60 (delta: -4)
```

### Visual Feedback

- **Pads:** Flash white when pressed
- **Buttons:** LED turns on briefly when pressed
- **All events logged to console with timestamps**

## Testing Checklist

Use this checklist to verify Controller Mode functionality:

### ✅ Pad Grid Testing
- [ ] All 128 pads (16×8) light up during startup animation
- [ ] Corner pads light up correctly (0,0), (15,0), (0,7), (15,7)
- [ ] Pressing any pad generates MIDI Note On/Off messages
- [ ] Pads flash white when pressed
- [ ] Released pads generate Note Off messages

### ✅ Button Testing
- [ ] All 20 buttons generate MIDI note messages when pressed
- [ ] Button names are logged correctly (PLAY, RECORD, etc.)
- [ ] `buttons` command lights up all button LEDs in sequence

### ✅ Encoder Testing
- [ ] Gold knob encoders generate CC messages (CC 71-78)
- [ ] Horizontal encoder generates CC 79
- [ ] Vertical encoder generates CC 80
- [ ] Select encoder generates CC 81
- [ ] Encoder deltas are calculated correctly (+/- from 64)

### ✅ LED Control
- [ ] Incoming MIDI Note On messages control pad colors
- [ ] Incoming MIDI Note Off messages turn pads off
- [ ] Button LEDs respond to MIDI Note On/Off
- [ ] All colors display correctly (RED, GREEN, BLUE, etc.)

### ✅ Performance
- [ ] Animations run smoothly without lag
- [ ] Pad presses are registered immediately (<10ms)
- [ ] No MIDI buffer overruns or dropped messages

## Troubleshooting

### MIDI Port Not Found
**Problem:** Script can't find Deluge MIDI ports

**Solutions:**
- Check USB cable connection
- Verify Deluge is powered on
- On Linux, check permissions: `sudo usermod -a -G audio $USER`
- List available ports: `python3 -m mido.ports`

### No Visual Response on Deluge
**Problem:** Animations don't appear on Deluge pads

**Solutions:**
- Verify Deluge is in Controller Mode (display should show "CONTROLLER MODE")
- Check MIDI output port is correct
- Restart Deluge (hold LEARN at startup) and reconnect
- Try `clear` command followed by `corner` command

### Pad Presses Not Detected
**Problem:** Script doesn't log pad presses

**Solutions:**
- Verify MIDI input port is correct
- Check Deluge MIDI settings (channel = 1)
- Ensure no other software is using the MIDI port
- Try pressing harder (some pads may need calibration)

### Button Presses Not Logged
**Problem:** Button presses aren't appearing in console

**Solutions:**
- Verify buttons are mapped to notes 100-119
- Check if SHIFT is stuck (may change button behavior)
- Some buttons may have special firmware behavior

### Encoder Not Working
**Problem:** Turning encoders doesn't generate CC messages

**Solutions:**
- Verify encoders are mapped to CC 71-81
- Check if Deluge is sending absolute vs relative values
- Try different encoders to isolate the issue

## Expected Output Example

```
======================================================================
DELUGE CONTROLLER MODE TEST SCRIPT
======================================================================
Available MIDI input ports:
  0: Deluge:Deluge MIDI 1 20:0

Available MIDI output ports:
  0: Deluge:Deluge MIDI 1 20:0

Auto-detected Deluge:
  Input: Deluge:Deluge MIDI 1 20:0
  Output: Deluge:Deluge MIDI 1 20:0
[12:34:56.123] MIDI: Connected to Deluge
[12:34:56.500] STARTUP: Beginning startup animation sequence
[12:34:56.501] ANIMATION: Drawing test grid
[12:35:00.123] ANIMATION: Starting spiral
[12:35:03.234] ANIMATION: Starting rainbow wave
[12:35:07.345] ANIMATION: Starting pulse
[12:35:10.456] STARTUP: Startup animation complete

======================================================================
CONTROLLER MODE TEST - INTERACTIVE MODE
======================================================================

Commands:
  grid     - Show test grid pattern
  rainbow  - Rainbow wave animation
  spiral   - Spiral animation
  pulse    - Pulse animation
  clear    - Clear all pads
  buttons  - Test all button LEDs
  corner   - Light up corner pads
  status   - Show current state
  quit     - Exit

Press pads, buttons, or turn encoders on Deluge to see events logged.
======================================================================

>>> [12:35:15.678] PAD: Pad ( 5,3) pressed - Note 53
[12:35:15.789] PAD: Pad ( 5,3) released - Note 53
[12:35:20.123] BUTTON: PLAY pressed
[12:35:20.234] BUTTON: PLAY released
[12:35:25.567] ENCODER: SELECT_ENCODER = 68 (delta: +4)
>>> corner
[12:35:30.890] COMMAND: Lighting corner pads
>>> quit
[12:35:35.123] SHUTDOWN: Shutting down...
[12:35:35.678] SHUTDOWN: Test complete
```

## Advanced Usage

### Custom Color Testing
You can modify the script to test custom colors by editing the `COLORS` dictionary:

```python
COLORS = {
    'CUSTOM1': 25,  # Test velocity value 25
    'CUSTOM2': 89,  # Test velocity value 89
    # ... add more
}
```

### Bandwidth Testing
Monitor MIDI bandwidth by watching the event log frequency:
- Normal operation: ~10-20 messages/second
- Heavy animation: ~100-200 messages/second
- Maximum theoretical: ~960 messages/second

### Latency Measurement
Measure round-trip latency by timing pad press to visual feedback:
```bash
# Press a pad and observe the white flash timing
# Expected: < 10ms from press to flash
```

## Related Documentation

- [Controller Mode MIDI Protocol](controller_mode_midi_protocol.md)
- [Bandwidth & Latency Analysis](controller_mode_bandwidth_latency.md)
- [Implementation Issues](controller_mode_implementation_issues.md)

## Reporting Issues

If you encounter bugs or unexpected behavior:

1. Note the exact sequence of events leading to the issue
2. Check the console log for error messages
3. Verify firmware version matches test script expectations
4. Document pad/button/encoder numbers involved
5. Report to Deluge firmware developers with logs

## Next Steps

After successful testing:
1. Develop remote scripts for Ableton Live or Bitwig Studio
2. Implement custom control surfaces
3. Test SysEx display control (requires additional implementation)
4. Build production-ready controller mappings
