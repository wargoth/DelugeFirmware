# Deluge Firmware Software Initialization

Critical initialization sequence for bare-metal ARM firmware. Boot time ~3-4 seconds total.

## Boot Flow
```
Hardware Reset → C Runtime → main() → deluge_main() → Main Loop
```

## Initialization Phases

### Phase 1: Hardware Reset (Pre-main)
- ARM vectors, clocks, stack, static init (`resetprg.c`)
- C++ global constructors

### Phase 2: Early Hardware (`main.c`)
```c
// System timers FIRST - required for all timing
setupRunningClock(TIMER_SYSTEM_FAST, 256);
setupRunningClock(TIMER_SYSTEM_SUPERFAST, 1);

// UART for MIDI/PIC communication
uartInit(UART_ITEM_MIDI, 31250);
uartInit(UART_ITEM_PIC, UART_INITIAL_SPEED_PIC_PADS_HZ);

// Pin mux for hardware interfaces
setPinMux(6, 15, 5); // MIDI TX
setPinMux(6, 14, 5); // MIDI RX
```

### Phase 3: Deluge Application (`deluge_main`)
**Location**: `src/deluge/deluge.cpp:deluge_main()` (~2-3 seconds)

#### Critical Order:
```cpp
// 1. Display & PIC setup
bool have_oled = detectOLED();
PIC::setDebounce(20);
PIC::setRefreshTime(23);

// 2. Core systems (MUST BE EARLY)
functionsInit(); // Parameter system - required before all else
AudioEngine::init(); // Voice pools

// 3. Hardware interfaces
setPinAsOutput(CODEC.port, CODEC.pin);
setOutputState(CODEC.port, CODEC.pin, 1); // Enable codec
ssiInit(0, 1); // 48kHz stereo audio

// 4. Storage (DANGER: can trigger audio routines)
initSPIBSC(); // ⚠️ Audio engine must be ready first!
FlashStorage::readSettings();

// 5. USB detection
openUSBHost();
if (!anythingInitiallyAttachedAsUSBHost) {
    closeUSBHost();
    openUSBPeripheral();
}

// 6. Content loading
setupBlankSong();
addConditionalTask(setupStartupSong, 100, isCardReady, "load startup song");
```

### Phase 4: Main Loop
**Location**: `src/deluge/deluge.cpp:mainLoop()`

```cpp
void mainLoop() {
    while (1) {
        uiTimerManager.routine();
        if (hid::display::have_oled_screen) oledRoutine();
        PIC::flush();

        AudioEngine::routineWithClusterLoading(true);

        // Input processing with audio interleaval
        int32_t count = 0;
        while (readButtonsAndPads() && count < 16) {
            if (!(count & 3)) {
                AudioEngine::routineWithClusterLoading(true);
            }
            count++;
        }

        encoders::readEncoders();
        encoders::interpretEncoders();
        doAnyPendingUIRendering();

        // Slow maintenance routines
        audioFileManager.slowRoutine();
        AudioEngine::slowRoutine();
        audioRecorder.slowRoutine();
    }
}
```

## Critical Patterns

### Memory Allocation Timing
```cpp
// ✅ GOOD: After functionsInit() in deluge_main()
void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(size);

// ❌ BAD: In early hardware setup - will crash
```

### PIC Communication Sequence
```cpp
PIC::setDebounce(20);           // Configure timing first
PIC::setUARTSpeed();            // Set communication speed
PIC::flush();                   // Clear pending data
PIC::requestFirmwareVersion();  // Request ID
PIC::resendButtonStates();      // Sync state
```

## Common Pitfalls

1. **Initialization Order**: Follow timers → hardware → software → content
2. **Memory Too Early**: Allocate only after `GeneralMemoryAllocator` ready
3. **SPIBSC Timing**: Audio engine must be ready before `initSPIBSC()`
4. **Debug Logging**: `D_PRINTLN()` during boot **will hang** (MIDI not ready)

## Adding New Code

**main.c** - Basic hardware only:
- Timers, UART, pin mux, interrupts

**deluge_main()** - Everything else:
- Memory allocation, software systems, complex hardware, file system

### Template:
```cpp
// In deluge_main(), find correct phase:
// Phase 3.2 - Core software
MySubsystem::init();

// Phase 3.3 - Hardware interfaces
setupMyHardware();

// Phase 3.6 - Content loading
addConditionalTask(loadMyData, 100, isReady, "my data");
```
