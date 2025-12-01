# Controller Mode - Implementation Issues & Fixes Required

## Critical Issues 🔴

### 1. **No MIDI Cable/Device Selection Architecture**

**Problem:** The implementation has no way to select which MIDI output (USB vs DIN) to use.

**Current Code:**
```cpp
midiEngine.sendNote(MIDISource::INTERNAL, true, note, 127, config_.midiChannel + 1);
```

**Issues:**
- Uses `MIDISource::INTERNAL` which may not route correctly
- No cable selection UI/configuration
- SysEx requires a specific `MIDICable*` reference
- Remote scripts can't choose USB vs DIN output

**Required Fix:**
1. Add `MIDICable* activeCable_` member to `ControllerModeView`
2. Add cable selection in `opened()` or configuration
3. Use `activeCable->sendMessage()` instead of `midiEngine.sendNote()`
4. Use `activeCable->sendSysex()` for SysEx messages

**Files to Modify:**
- `src/deluge/gui/views/controller_mode_view.h` - add `MIDICable* activeCable_`
- `src/deluge/gui/views/controller_mode_view.cpp` - change all send calls

---

### 2. **SysEx Identity Reply Not Sent**

**Location:** `controller_mode_view.cpp:419`

**Current Code:**
```cpp
void ControllerModeView::sendIdentityReply() {
    uint8_t identity[] = {...};
    // Send via MIDI engine
    // midiEngine.sendSysex(identity, sizeof(identity));  // <-- COMMENTED OUT!
}
```

**Impact:** Remote scripts cannot detect Deluge controller!

**Fix:** Uncomment and use cable:
```cpp
if (activeCable_) {
    activeCable_->sendSysex(identity, sizeof(identity));
}
```

---

### 3. **Missing ccReceivedForMidiLearn() Override**

**Problem:** Encoder LED control via MIDI CC will not work.

**Required:** Add override in controller_mode_view.h:
```cpp
bool ccReceivedForMidiLearn(MIDICable& fromCable, int32_t channel,
                           int32_t cc, int32_t value) override;
```

**Implementation in .cpp:**
```cpp
bool ControllerModeView::ccReceivedForMidiLearn(MIDICable& fromCable,
                                                 int32_t channel, int32_t cc, int32_t value) {
    if (channel != config_.midiChannel) {
        return false;
    }
    handleMidiCCForControl(channel, cc, value);
    return true;
}
```

---

### 4. **No Note Off Handling for LEDs**

**Problem:** Remote scripts send Note Off to turn LEDs off, but only `noteOnReceivedForMidiLearn` is implemented.

**Current Behavior:** Note Off messages (velocity=0) are ignored for LED control.

**Fix:** Handle velocity=0 as LED off in existing handler:
```cpp
void ControllerModeView::handleMidiNoteForLED(int32_t channel, int32_t note, int32_t velocity) {
    // ... existing code handles velocity=0 correctly
    if (velocity == 0) {
        padColors_[x][y] = {0, 0, 0};  // Already implemented
    }
}
```

**Status:** Actually implemented correctly! Just needs verification.

---

## Major Issues 🟡

### 5. **Empty LED Update Stubs**

**Locations:**
- `controller_mode_view.cpp:529` - `updateButtonLEDs()`
- `controller_mode_view.cpp:534` - `updateEncoderLEDs()`

**Current Code:**
```cpp
void ControllerModeView::updateButtonLEDs() {
    // Update button LEDs based on state set by remote script
    // This would interface with actual button LED hardware
}

void ControllerModeView::updateEncoderLEDs() {
    // Update gold encoder LEDs based on state set by remote script
    // This would interface with actual encoder LED hardware
    uiNeedsRendering(this);
}
```

**Problem:** Functions exist but don't actually control any hardware!

**Required Research:**
1. Find button LED control interface in codebase
2. Find encoder LED control interface
3. Implement actual hardware control

**Search Patterns:**
```bash
grep -r "setButtonLED\|buttonLED\|indicator.*led" src/
grep -r "setModLED\|encoderLED" src/
```

---

### 6. **SysEx Receive Not Integrated**

**Problem:** `handleMidiSysexForDisplay()` exists but may not be called.

**Investigation Needed:**
- Check if `midiSysexReceived()` in MidiEngine routes to UI
- May need to override a sysex handler in base class
- Check how other views receive SysEx

**Search:**
```bash
grep -r "sysexReceived\|SysexReceived" src/deluge/gui/
```

---

### 7. **Display Text Not Shown on 7-Segment**

**Problem:** `displayText_` is set via SysEx but never displayed on 7-seg hardware.

**Current:**
```cpp
void ControllerModeView::updateDisplay() {
    if (displayText_[0] != '\0') {
        display->displayPopup(displayText_);  // Only shows popup, not 7-seg!
    }
}
```

**Required:** Find 7-segment display interface and update it.

---

## Minor Issues 🟢

### 8. **No Input Validation**

- MIDI note calculations don't validate ranges
- Config parameters not validated
- SysEx message format not validated

**Add Checks:**
```cpp
if (x < 0 || x >= kDisplayWidth || y < 0 || y >= kDisplayHeight) {
    return; // Invalid pad coordinates
}
```

---

### 9. **No MIDI Loop Prevention**

All sends use `MIDISource::INTERNAL` which may cause MIDI loops if thru is enabled.

**Consider:** Using a dedicated `MIDISource::CONTROLLER_MODE` source type.

---

### 10. **Memory Safety - Fixed Arrays**

Arrays like `padColors_[16][8]` use compile-time sizes. If `config_.gridWidth/Height` change, arrays are wrong size.

**Options:**
1. Validate config matches compiled sizes
2. Use dynamic allocation
3. Document that grid size is fixed

---

## Testing Checklist

- [ ] Compile test (does it build?)
- [ ] MIDI output test (do pads send notes?)
- [ ] MIDI input test (do LEDs respond to Note On?)
- [ ] CC input test (do encoder LEDs respond to CC?)
- [ ] SysEx test (does identity reply send?)
- [ ] Display test (does display update?)
- [ ] Cable selection test (USB vs DIN routing)
- [ ] Performance test (latency measurements)

---

## Priority Fix Order

1. **CRITICAL:** Add MIDI cable selection architecture
2. **CRITICAL:** Fix SysEx sending (uncomment + use cable)
3. **CRITICAL:** Add `ccReceivedForMidiLearn()` override
4. **MAJOR:** Implement `updateButtonLEDs()`
5. **MAJOR:** Implement `updateEncoderLEDs()`
6. **MAJOR:** Verify SysEx receive routing
7. **MINOR:** Add input validation
8. **MINOR:** Fix 7-segment display output

---

## Files Requiring Changes

### Header (.h)
- `src/deluge/gui/views/controller_mode_view.h`
  - Add `MIDICable* activeCable_` member
  - Add `ccReceivedForMidiLearn()` override
  - Add cable selection methods

### Implementation (.cpp)
- `src/deluge/gui/views/controller_mode_view.cpp`
  - Fix `sendIdentityReply()` - uncomment SysEx send
  - Add cable selection in `opened()`
  - Change all `midiEngine.send*()` to use `activeCable_`
  - Implement `ccReceivedForMidiLearn()`
  - Implement `updateButtonLEDs()`
  - Implement `updateEncoderLEDs()`
  - Fix `updateDisplay()` for 7-segment

### Documentation
- `docs/controller_mode_midi_protocol.md` - already updated ✅
- `docs/controller_mode_bandwidth_latency.md` - already created ✅

---

## Code Examples Needed

### Find Button LED Control:
```bash
grep -rn "LED.*button\|button.*LED" src/deluge/hid/
grep -rn "indicator.*setLed" src/
```

### Find Encoder LED Control:
```bash
grep -rn "mod.*LED\|encoder.*LED\|gold.*LED" src/
grep -rn "setKnobIndicator" src/
```

### Find 7-Segment Display:
```bash
grep -rn "displayNum\|display.*7seg\|numericDriver" src/
```

---

## Next Steps

1. Research MIDI cable selection pattern in codebase
2. Research LED control interfaces
3. Implement critical fixes (#1-#3)
4. Test compilation
5. Test with simple remote script
6. Implement major fixes (#4-#6)
7. Final testing and documentation
