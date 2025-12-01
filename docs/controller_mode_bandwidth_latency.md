# Deluge Controller Mode - Bandwidth & Latency Analysis

## MIDI Bandwidth Calculations

### MIDI Protocol Basics
- **MIDI Baud Rate**: 31,250 bits/second
- **USB MIDI**: ~1.5 Mbps (much faster than DIN MIDI)
- **Message Overhead**: Start bit + 8 data bits + stop bit = 10 bits per byte

### Message Sizes

#### Output Messages (Deluge → DAW)

**Note On/Off (3 bytes):**
```
Status:   1 byte (0x90 or 0x80)
Note:     1 byte (0-127)
Velocity: 1 byte (0 or 127)
Total:    3 bytes = 30 bits @ DIN MIDI = 0.96 ms per message
```

**Control Change (3 bytes):**
```
Status: 1 byte (0xB0)
CC:     1 byte (0-127)
Value:  1 byte (0-127)
Total:  3 bytes = 30 bits @ DIN MIDI = 0.96 ms per message
```

#### Input Messages (DAW → Deluge)

**LED Control via Note On (3 bytes):**
```
Same as Note On: 3 bytes = 0.96 ms
```

**Full RGB LED via SysEx (12 bytes):**
```
F0 00 21 7D 01 20 xx yy rr gg bb F7
Total: 12 bytes = 120 bits = 3.84 ms per pad
```

**Display Text via SysEx (variable):**
```
Header:    6 bytes
Text:      N bytes
Footer:    1 byte
Total:     (7 + N) bytes

Example "HELLO" (5 chars):
(7 + 5) = 12 bytes = 3.84 ms
```

---

## Usage Scenarios

### Scenario 1: Rapid Pad Tapping (Worst Case)

**Assumptions:**
- User tapping pads rapidly: 10 pads/second
- Each pad = Note On + Note Off = 6 bytes

**Bandwidth:**
```
10 pads/sec × 6 bytes/pad = 60 bytes/sec
@ DIN MIDI: 60 × 10 bits = 600 bits/sec
Utilization: 600 / 31,250 = 1.9%

@ USB MIDI: Negligible (< 0.004%)
```

### Scenario 2: Button Mashing (Worst Case)

**Assumptions:**
- 10 buttons pressed simultaneously
- Each button = Note On = 3 bytes

**Bandwidth:**
```
10 buttons × 3 bytes = 30 bytes
Burst time @ DIN MIDI: 30 × 10 bits = 300 bits = 9.6 ms
@ USB MIDI: < 0.2 ms
```

### Scenario 3: Encoder Twisting

**Assumptions:**
- 8 encoders sending updates at 30 Hz each
- Each update = 3 bytes (CC message)

**Bandwidth:**
```
8 encoders × 30 Hz × 3 bytes = 720 bytes/sec
@ DIN MIDI: 720 × 10 bits = 7,200 bits/sec
Utilization: 7,200 / 31,250 = 23%

@ USB MIDI: < 0.5%
```

### Scenario 4: Full Grid LED Update (DAW → Deluge)

**Using Simple Note On (velocity = color):**
```
128 pads × 3 bytes = 384 bytes
@ DIN MIDI: 384 × 10 bits = 3,840 bits = 123 ms (8.1 fps)
@ USB MIDI: < 2 ms (500+ fps)
```

**Using Full RGB SysEx:**
```
128 pads × 12 bytes = 1,536 bytes
@ DIN MIDI: 1,536 × 10 bits = 15,360 bits = 491 ms (2 fps)
@ USB MIDI: < 10 ms (100 fps)
```

### Scenario 5: Combined Usage (Realistic Performance)

**Typical session:**
- Pads: 5 presses/sec = 30 bytes/sec
- Buttons: 2 presses/sec = 6 bytes/sec
- Encoders: 4 encoders @ 20 Hz = 240 bytes/sec
- LED updates: 20 pads/sec = 60 bytes/sec
- Display updates: 1/sec = 12 bytes/sec

**Total:**
```
Combined: 348 bytes/sec
@ DIN MIDI: 3,480 bits/sec = 11.1% utilization
@ USB MIDI: < 0.25% utilization
```

---

## Latency Analysis

### Round-Trip Latency (Pad Press → LED Update)

#### Via DIN MIDI:
```
1. Pad press detected:           < 1 ms (hardware scan)
2. MIDI Note On transmitted:     0.96 ms (3 bytes @ DIN)
3. USB to computer:              < 1 ms (USB latency)
4. DAW processing:               1-10 ms (depends on DAW)
5. Remote script processing:     < 1 ms (Python/JS)
6. MIDI LED command sent:        0.96 ms (3 bytes @ DIN)
7. USB to Deluge:                < 1 ms
8. LED update rendered:          < 1 ms (next frame)

Total Round-Trip: 5-16 ms (typical: 8-10 ms)
```

#### Via USB MIDI (recommended):
```
1. Pad press detected:           < 1 ms
2. USB MIDI Note On:             < 0.5 ms (USB bulk transfer)
3. DAW processing:               1-10 ms
4. Remote script processing:     < 1 ms
5. USB MIDI LED command:         < 0.5 ms
6. LED update rendered:          < 1 ms

Total Round-Trip: 3-14 ms (typical: 5-7 ms)
```

### Jitter Analysis

**DIN MIDI:**
- Fixed timing: 0.32 ms per byte
- Predictable, but slow
- Jitter: < 0.1 ms (very low)

**USB MIDI:**
- Packet-based: 1 ms USB frame
- Much faster, but slight jitter possible
- Jitter: 0.5-2 ms (acceptable)

---

## Performance Recommendations

### For Best Responsiveness:

1. **Use USB MIDI** (not DIN)
   - 10-50x faster than DIN MIDI
   - Negligible bandwidth concerns
   - Lower latency

2. **Use Simple LED Control** (Note velocity, not RGB SysEx)
   - 4x faster than full RGB SysEx
   - 3 bytes vs 12 bytes per update
   - Still provides 127 colors

3. **Batch LED Updates**
   - Update only changed pads
   - Don't refresh entire grid every frame
   - Use differential updates

4. **Limit Display Updates**
   - Update display text only on change
   - Don't stream text (1-2 updates/sec max)

5. **Optimize Remote Script**
   - Process MIDI in dedicated thread
   - Don't block on MIDI I/O
   - Buffer outgoing LED commands

### Bandwidth Budget (USB MIDI)

**Total available:** ~1.5 Mbps (188 KB/sec)

**Typical usage:** < 500 bytes/sec (< 0.3%)

**Maximum sustainable:**
- 100 pad updates/sec
- 50 encoder updates/sec
- 10 display updates/sec
- Still < 5% bandwidth usage

**Conclusion:** Bandwidth is NOT a concern with USB MIDI.

---

## Latency Budget

### Target: < 10 ms round-trip (imperceptible)

**Breakdown:**
```
Hardware detection:      1 ms   (10%)
MIDI transmission:       1 ms   (10%)
DAW + script:           5 ms   (50%)
Return MIDI:            1 ms   (10%)
LED rendering:          2 ms   (20%)
------------------------
Total:                 10 ms   (100%)
```

### Optimization Strategies:

1. **Minimize DAW Latency**
   - Use low buffer sizes (64-128 samples @ 48kHz = 1.3-2.7 ms)
   - Disable unnecessary audio processing
   - Use dedicated MIDI thread in remote script

2. **Optimize Remote Script**
   - Pre-calculate LED states
   - Use lookup tables for colors
   - Avoid floating-point math in MIDI callback

3. **Batch Operations**
   - Group MIDI messages when possible
   - Don't send redundant updates

### Expected Performance:

**Best case:** 3-5 ms (professional controller level)
**Typical case:** 5-10 ms (excellent, imperceptible)
**Worst case:** 10-20 ms (acceptable for most use cases)

---

## Comparison to Commercial Controllers

### Ableton Push 2:
- **Latency:** 3-5 ms (USB HID + proprietary protocol)
- **Bandwidth:** Not MIDI-limited (USB HID, custom protocol)

### Akai APC40 MKII:
- **Latency:** 5-10 ms (USB MIDI)
- **Bandwidth:** Same MIDI limitations as Deluge

### Novation Launchpad Pro:
- **Latency:** 5-8 ms (USB MIDI)
- **Bandwidth:** Same MIDI limitations as Deluge

**Deluge Controller Mode:**
- **Latency:** 5-10 ms (comparable to APC40/Launchpad)
- **Bandwidth:** Identical to other USB MIDI controllers
- **Performance:** Professional-grade when using USB MIDI

---

## Implementation Notes

### USB MIDI Buffer Sizes (from codebase)

**Peripheral Mode (Deluge as device):**
```
Send buffer: 32 × 4 bytes = 128 bytes
Ring buffer: 1024 × 4 bytes = 4096 bytes
Total capacity: 4224 bytes buffered
```

**At 60 fps LED updates:**
```
128 pads × 3 bytes = 384 bytes/frame
384 bytes × 60 fps = 23,040 bytes/sec
Buffer can hold: 4224 / 384 = 11 frames (183 ms @ 60fps)
```

**Conclusion:** Ample buffering for burst LED updates.

### Theoretical Maximum Performance

**USB MIDI (1.5 Mbps):**
```
Message throughput: ~50,000 messages/sec (3 bytes/msg)

LED update rate: 50,000 / 128 pads = 390 fps
Encoder update rate: 50,000 / 8 = 6,250 updates/sec/encoder

Practical limits are much lower due to:
- DAW processing time
- Remote script overhead
- Display rendering (60 fps limit)
```

---

## Summary

### USB MIDI (Recommended):
✅ Latency: 5-10 ms (excellent)
✅ Bandwidth: < 1% usage (plenty of headroom)
✅ Performance: Professional controller-grade
✅ Suitable for: Live performance, studio use, all scenarios

### DIN MIDI (Not Recommended):
⚠️ Latency: 10-20 ms (acceptable but slower)
⚠️ Bandwidth: 10-30% usage (manageable but tight)
⚠️ Full grid updates: 123 ms (too slow for real-time)
❌ Not suitable for: Fast LED animations, frequent full-grid updates

**Recommendation:** Always use USB MIDI for controller mode. DIN MIDI should only be used if absolutely necessary, and with limited LED feedback.
