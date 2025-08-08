# Deluge Firmware Architecture Details

## System Overview
Deluge Community Firmware - bare-metal C/C++ embedded application on Renesas RZ/A1L ARM Cortex-A9 (400MHz, 3MB SRAM, 64MB SDRAM). No operating system, real-time audio processing.

## Software Initialization & Startup

The firmware follows a carefully orchestrated boot sequence from hardware reset through application ready state (~3-4 seconds total):

### Key Initialization Phases
1. **Hardware Reset → C Runtime**: ARM vectors, clocks, stack, static init
2. **Early Hardware Setup** (`main.c`): System timers, UART, pin mux, interrupts
3. **Application Initialization** (`deluge_main()`): PIC comms, display detection, audio engine, USB, file systems
4. **Main Loop**: UI processing, audio rendering, input handling

### Critical Dependencies
- System timers → Hardware interrupts → Software subsystems → Content loading
- `functionsInit()` → Parameter system → Audio engine → SPIBSC (flash) setup
- Memory allocator ready → All dynamic allocation (Songs, UI objects, etc.)

### Common Pitfalls
- `D_PRINTLN()` during early boot **will hang firmware** (MIDI system not ready)
- Memory allocation before `GeneralMemoryAllocator` setup crashes
- SPIBSC setup triggers audio routines - audio engine must be ready first
- PIC communication failures manifest as unresponsive buttons/LEDs

📖 **Complete Documentation**: See `docs/dev/software_initialization.md` for detailed phase-by-phase breakdown.

## UI System Architecture

### Coordinate Systems (CRITICAL)
- **Loop Row (y=0)**: Special UI element, not in `outputsOnScreen` array
- **Track Rows (y=1-7)**: Use `outputsOnScreen[yDisplay]` directly (NOT `outputsOnScreen[yDisplay-1]`)
- **Array Population**: `repopulateOutputsOnScreen()` stores first track at `outputsOnScreen[1]`

### UI State Machine
- **`currentUIMode`**: Central state variable managing exclusive/non-exclusive UI modes
- **Exclusive modes**: `UI_MODE_HOLDING_ARRANGEMENT_ROW`, `UI_MODE_CLIP_PRESSED_IN_SONG_VIEW`
- **Non-exclusive modes**: `UI_MODE_AUDITIONING`, `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`
- **State transitions**: `enterUIMode(UIMode)` and `exitUIMode()` functions
- **Context preservation**: Views store state (scroll positions, zoom levels) across transitions

### UI Action Flow
1. Hardware events → `buttonAction()`/`padAction()` → `ActionResult`
2. UI state managed via `currentUIMode` enum with sophisticated state machine
3. Views: `ArrangerView`, `SessionView`, `InstrumentClipView`, `AutomationView`, `KeyboardScreen`
4. Mode transitions trigger view-specific setup/teardown and LED state updates

## Audio Engine Integration

### Voice Management Patterns
- **Voice Pool**: `AudioEngine::VoicePool` with priority-based allocation system
- **Voice Allocation**: `assignVoiceToNote()` with sophisticated priority calculations
- **Voice Stealing**: `stealOneActiveVoice()` using least-recently-triggered algorithm
- **Voice Culling**: `terminateOneActiveVoice()` and `forceReleaseOneVoice()` for resource management
- **Priority System**: Based on note velocity, age, and current amplitude for intelligent resource allocation
- **Voice Lifecycle**: New → Active → Releasing → Unassigned with state transitions

### Thread Safety Rules
- Audio thread: `AudioEngine::routine()` - NEVER allocate memory or touch UI
- Main thread: All UI interactions, memory allocation
- SD Card thread: File I/O operations (yielding functions)

### Cluster Loading & Caching System
- **SD Card Streaming**: `ClusterPriorityQueue` for asynchronous audio data loading
- **Cluster Types**: `SAMPLE` (raw audio), `PERC_CACHE_FORWARDS/REVERSED` (time-stretch data)
- **Multi-level Caching**: Sample cache (`SampleCache`), percussion cache, and wavetable cache
- **Memory Pressure**: Sophisticated stealing algorithms with priority queues
- **Predictive Loading**: Time-stretcher reserves upcoming clusters via `updateClustersForPercLookahead()`
- **Cluster Boundaries**: 32KB clusters (filesystem dependent) with cross-cluster sample continuity

## Action/Consequence Pattern (Critical Architecture)

### Undo/Redo System
- All user actions create `Action` objects managed by `ActionLogger`
- **Action Types**: `ActionType::NOTE_EDIT`, `ActionType::CLIP_HORIZONTAL_SHIFT`, `ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE`
- **Consequence Chain**: Each `Action` contains linked list of `Consequence` objects for granular undo
- **Smart Batching**: Multiple similar actions get batched (e.g., continuous parameter tweaks)
- **State Snapshots**: Actions capture full UI state (scroll positions, zoom levels, mode states)
- **Reversion Logic**: `Action::revert(TimeType time)` calls `Consequence::revert()` in sequence
- **Memory Management**: Custom allocators for Actions/Consequences with careful cleanup

### Action Creation Pattern
```cpp
Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
if (action) {
    void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteEdit));
    ConsequenceNoteEdit* newConsequence = new (consMemory) ConsequenceNoteEdit(/* params */);
    action->addConsequence(newConsequence);
}
```

## Hardware Integration Points

### External Dependencies
- MIDI I/O via custom MIDI engine (`src/deluge/io/midi/`)
- SD Card: FatFS filesystem (`src/fatfs/`)
- Audio: SSI interface to codec (`src/RZA1/drivers/ssi/`)
- Display: OLED or 7-segment (`src/deluge/hid/display/`)

### Real-time Constraints
- Audio buffer: 64 samples @ 44.1kHz (≈1.5ms deadline)
- UI responsiveness: Button debouncing, LED updates
- Storage: SD card clustering for large audio files
