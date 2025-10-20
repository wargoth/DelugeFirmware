# Deluge Firmware Debugging & Logging

## Debug Logging System Architecture

### Primary Interface
- **Macros**: `D_PRINTLN()`, `D_PRINT()`, `D_PRINT_RAW()` from `io/debug/log.h`
- **Output Methods**: MIDI SysEx (when `midiDebugCable` available) or UART fallback
- **Build Control**: `ENABLE_TEXT_OUTPUT` flag in `drivers/uart/uart.h` (default: 0)
- **Timing System**: Cycle counter-based timestamps via ARM Performance Monitor Unit

## Critical Logging Constraints

### Initialization Hazard
- **`D_PRINTLN()` during firmware startup can cause initialization hangs**
- Debug output uses MIDI SysEx system which isn't ready during early boot
- MIDI dependency means logging must wait for full system initialization

### Thread Safety
- **NEVER use logging in audio processing threads** (`AudioEngine::routine()`)
- Audio thread must remain allocation-free and lock-free
- SD Card conflicts: Avoid logging during SD card operations (check `sdRoutineLock`)

## Safe Logging Practices

### Dangerous Pattern (Will Hang)
```cpp
// ❌ DANGEROUS: Can hang firmware during initialization
void setupPlayback() {
    D_PRINTLN("Setting up playback..."); // Will hang if called during boot
    // ...
}
```

### Safe Patterns
```cpp
// ✅ SAFE: Check system readiness or use after full initialization
void onUserAction() {
    if (currentSong && display) { // Ensure core systems ready
        D_PRINTLN("User action processed");
    }
}

// ✅ SAFE: Conditional logging for debugging sessions
#if ENABLE_TEXT_OUTPUT && defined(DEBUG_ARRANGEMENT_LOOP)
    D_PRINTLN("Loop trigger: pos=%d", currentPos);
#endif

// ✅ SAFE: Alternative feedback methods during initialization
display->displayPopup("LOOP"); // Visual feedback instead of debug logging
```

## Testing Strategy

### Hardware Testing
- Physical Deluge required for full validation
- Test pad/button combinations in different modes
- Monitor for audio dropouts and glitches in real-time processing

### Build Verification
```bash
./dbt build debug          # Debug build with assertions
./dbt build release        # Release build for performance testing
```

### Testing Workflow
1. **Compilation**: Use `./dbt build debug` to catch compilation issues
2. **UI Testing**: Exercise pad/button combinations in different UI modes
3. **Audio Testing**: Monitor for dropouts, glitches in real-time processing
4. **Memory Testing**: Verify no memory leaks with custom allocators
5. **Integration Testing**: Test cross-system interactions (UI → Audio → Storage)
