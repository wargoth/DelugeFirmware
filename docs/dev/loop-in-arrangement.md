# Arrangement Loop Documentation

## Current Status: COMPREHENSIVE REFACTORING COMPLETED ✅

**Major architectural refactoring implemented successfully** - The arrangement loop system has been completely redesigned with proper separation of concerns and moved to the Song class for correct data ownership:

### Refactoring Overview:

**Architectural Ownership Change**
- **Before**: Loop data stored in Arrangement class (playback mode)
- **After**: Loop data moved to Song class (data model) with clean separation of concerns

**Key Architectural Changes:**
1. **ArrangementLoop in Song Class** (`src/deluge/model/song/song.h/cpp`):
   - Loop data moved from Arrangement to Song for proper data ownership
   - Song directly manages its own persistent state including loops
   - Clean accessor methods: `getArrangementLoop()`, `shouldLoopArrangement()`, `checkForArrangementLoopAndGetNewPosition()`

2. **Simplified Arrangement Class** (`src/deluge/playback/mode/arrangement.h/cpp`):
   - Removed ArrangementLoop member and associated loop management
   - Arrangement now references Song's loop data instead of owning it
   - Clean separation between data (Song) and behavior (Arrangement)

3. **Direct Song Serialization** (`src/deluge/model/song/song.cpp`):
   - Song directly saves/loads its own loop data without coupling to Arrangement
   - Eliminated temporary "pending" variables during loading
   - Clean data flow: XML → Song loop data → immediate application

4. **Updated Integration Points**:
   - PlaybackHandler uses `currentSong->shouldLoopArrangement()` and `currentSong->checkForArrangementLoopAndGetNewPosition()`
   - ArrangerView uses `currentSong->getArrangementLoop()` for all loop operations
   - AudioRecorder uses `currentSong->getArrangementLoop()` for loop-constrained recording

5. **Removed Obsolete Methods** ⚠️:
   - **`Song::applyPendingArrangementLoopData()`** - No longer needed since loop data is applied directly during loading
   - All calls to this method have been removed from initialization sequences
   - Clean, simplified initialization flow without unnecessary deferred operations

### Root Cause Analysis:
The original issues stemmed from **architectural violations**:
1. **Data/Behavior Mixing**: Loop data (persistent state) was stored in Arrangement (behavior/playback mode)
2. **Coupling**: Song had to reach into Arrangement to access loop data for saving
3. **Temporary Variables**: Song needed "pending" variables as workaround for the coupling
4. **Inconsistency**: All other song data lived in Song, but loop lived in Arrangement
5. **Deferred Application**: Complex initialization sequences with pending data application

### Architecture Benefits:
- ✅ **Proper Separation**: Song owns data, Arrangement provides behavior
- ✅ **Loose Coupling**: Arrangement references Song's data instead of owning it
- ✅ **Direct Serialization**: Song saves/loads its own data without external dependencies
- ✅ **Consistency**: All persistent song data lives in single location
- ✅ **Clean Interfaces**: Clear ownership and well-defined accessor methods
- ✅ **Extensibility**: Easy to add new loop features without affecting multiple systems
- ✅ **Simplified Initialization**: No deferred operations or pending data application needed

---

## New Architecture Overview

The loop system follows proper separation of concerns with Song class owning all persistent data.

### Core Components:

**Song Class** (`src/deluge/model/song/song.h/cpp`)
```cpp
class Song {
private:
    ArrangementLoop arrangementLoop_;  // Owned by Song for proper data ownership

public:
    // Clean interface for loop access
    ArrangementLoop& getArrangementLoop() { return arrangementLoop_; }
    const ArrangementLoop& getArrangementLoop() const { return arrangementLoop_; }
    bool shouldLoopArrangement() const { return arrangementLoop_.isActive(); }
    int32_t checkForArrangementLoopAndGetNewPosition(int32_t currentPos);
};
```

**Integration Points:**

1. **Song Class** (`src/deluge/model/song/song.h/cpp`)
   - Owns ArrangementLoop as private member
   - Provides public accessor methods for loop operations
   - Directly saves/loads loop data in `writeToFile()` and `readFromFile()`
   - Implements `checkForArrangementLoopAndGetNewPosition()` for playback integration

2. **Arrangement Class** (`src/deluge/playback/mode/arrangement.h/cpp`)
   - References Song's loop data via `currentSong->getArrangementLoop()`
   - No loop ownership or management responsibility
   - Clean separation between data and playback behavior

3. **PlaybackHandler** (`src/deluge/playback/playback_handler.cpp`)
   - Uses `currentSong->shouldLoopArrangement()` for loop checks
   - Uses `currentSong->checkForArrangementLoopAndGetNewPosition()` for position updates

4. **ArrangerView** (`src/deluge/gui/views/arranger_view.h/cpp`)
   - Uses `currentSong->getArrangementLoop()` for all UI operations
   - Clean interface with Song's loop data instead of Arrangement

### Data Flow:

**Before Refactoring (Problematic):**
```
Song.writeToFile() → arrangement.getLoop() → Save loop data (coupling)
Song.readFromFile() → pendingArrangerLoopData (temporary storage)
Song.applyPendingArrangementLoopData() → arrangement.getLoop() (complex flow)
```

**After Refactoring (Clean):**
```
Song.writeToFile() → arrangementLoop_ → Save loop data (direct)
Song.readFromFile() → arrangementLoop_ → Apply loop data immediately (direct)
PlaybackHandler → currentSong->shouldLoopArrangement() (clean reference)
```

## Testing

The existing comprehensive test suite at `tests/unit/arrangement_loop_tests.cpp` continues to validate loop behavior with the new architecture.

## Implementation Files

### Core Loop Logic:
- **`src/deluge/model/arrangement_loop.h/cpp`** - ArrangementLoop class (unchanged)
- **`src/deluge/model/song/song.h/cpp`** - Song class with integrated loop management
- **`src/deluge/playback/mode/arrangement.h/cpp`** - Simplified Arrangement class
- **`src/deluge/playback/playback_handler.cpp`** - Updated to use Song's loop interface
- **`src/deluge/gui/views/arranger_view.h/cpp`** - Updated to use Song's loop interface

### Updated Integration Points:
- **`src/deluge/gui/ui/audio_recorder.cpp`** - Loop-constrained recording
- All files now consistently use `currentSong->getArrangementLoop()` pattern

## Migration Notes

### For Developers:
- **Old pattern**: `arrangement.getLoop()` → **New pattern**: `currentSong->getArrangementLoop()`
- **Old pattern**: `arrangement.shouldLoopArrangement()` → **New pattern**: `currentSong->shouldLoopArrangement()`
- **Loop data access**: Always through Song class, never through Arrangement
- **State management**: Song owns and manages all loop state

### Architectural Benefits:
- **Clear Ownership**: Song class owns all persistent data including loops
- **Loose Coupling**: Arrangement references Song data instead of owning it
- **Direct I/O**: Song directly saves/loads its own data without external dependencies
- **Consistency**: All song-level data in single location following established patterns

## Development Guidelines

### When Working with Loop System:
1. **Use Song Interface**: Always access loop state through `currentSong->getArrangementLoop()` methods
2. **Respect Ownership**: Song owns data, other classes reference it
3. **Follow Patterns**: Use established Song accessor patterns for consistency
4. **Test Thoroughly**: Run existing tests to validate architectural changes

### Common Operations:
```cpp
// Check if loop is active
if (currentSong && currentSong->shouldLoopArrangement()) { /* ... */ }

// Create a new loop
currentSong->getArrangementLoop().create(startPos, endPos);

// Clear existing loop
currentSong->getArrangementLoop().clear();

// Check for loop in playback
int32_t newPos = currentSong->checkForArrangementLoopAndGetNewPosition(currentPos);
```

This refactoring provides a solid foundation for future loop-related features while maintaining clean architecture, proper separation of concerns, and comprehensive test coverage.

## Development Guidelines

### When Working with Loop System:
1. **Use ArrangementLoop Interface**: Always access loop state through `arrangement.getLoop()` methods
2. **Avoid Direct State Access**: Don't bypass encapsulation by accessing private members
3. **Test Thoroughly**: Run existing tests and add new tests for any modifications
4. **Follow Patterns**: Use established patterns for state management and integration

### Common Operations:
```cpp
// Check if loop is active
if (arrangement.shouldLoopArrangement()) { /* ... */ }

// Create a new loop
arrangement.getLoop().create(startPos, endPos);

// Clear existing loop
arrangement.getLoop().clear();

// Check for loop in playback
int32_t newPos = arrangement.checkForLoopAndGetNewPosition(currentPos);
```

This refactoring provides a solid foundation for future loop-related features while maintaining clean architecture and comprehensive test coverage.

### **Task 2: Integrate Quantized Resampling with Active Loop**

**Objective:** Modify the `SHIFT` + `RECORD` resampling function to automatically stop at the end of the active loop.

**Sub-tasks:**

1.  **Identify Resampling Trigger:** Hook into the existing `SHIFT` + `RECORD` command trigger for resampling.
2.  **Check for Active Loop:** Before starting the resampling process, check if a Loop Handle is active and defining loop boundaries in the Arranger View.
3.  **Automatic Stop Logic:** If an active loop is detected:
    * When resampling begins, monitor the playback position relative to the active loop's end point.
    * Automatically terminate the resampling recording precisely when the playback cursor reaches the end of the active loop's boundary.
4.  **Error Handling/Feedback:** Provide appropriate visual or auditory feedback to the user when automatic stopping occurs.

---

### **Task 3: Implement Instant Loop-to-Song Section Conversion**

**Objective:** Create a new shortcut (`RECORD` + `SONG`) to convert the active loop's content into a new song section.

**Sub-tasks:**

1.  **New Shortcut Implementation:**
    * Define and implement the new shortcut combination: `RECORD` + `SONG`.
    * Ensure this shortcut is only active/responsive when a Loop Handle is currently defining an active loop in the Arranger View.
2.  **Content Selection:**
    * Upon activation of the `RECORD` + `SONG` shortcut, identify all clips and automation data within the currently active loop's boundaries.
3.  **New Song Section Creation:**
    * Create a new "song section" in the Deluge's song structure.
    * Copy all identified clips and automation data from the active loop into this newly created song section, preserving their relative positions and properties.
4.  **Placement in Arrangement:** Decide on a logical placement for this new song section in the arrangement (e.g., at the end of the current song, or prompt user for insertion point). For initial implementation, placing it at the end of the current song is acceptable.
5.  **User Feedback:** Provide clear visual or auditory feedback that a new song section has been successfully created.

---

### **Task 4: General Enhancements for Audio Recordings/Overdubs**

**Objective:** Ensure the new looping capabilities support improved quantized audio recordings and overdubs.

**Sub-tasks:**

1.  **Verify Compatibility:** Confirm that the Loop Row and its looping behavior correctly constrain audio recording and overdubbing processes, leveraging the exact loop boundaries for start and stop.
2.  **Documentation Update:** Update internal documentation regarding the enhanced capabilities for quantized sampling of external MIDI instruments and overdubbing within precise loop regions.

---

## Implementation Status

### Summary of Key Achievements

**Critical Safety Fixes**
- ✅ **Constructor Initialization**: Proper initialization of critical variables to prevent crashes during startup
  - `lastSwungTickActioned = 0` - Prevents uninitialized tick counter access
  - `swungTicksTilNextEvent = 2147483647` - Prevents immediate tick processing during startup
  - `nextTimerTickScheduled = 0` - Ensures safe timer state initialization
- ✅ **Nullptr Protection**: Added comprehensive null pointer checks to prevent crashes during initialization
  - `display` pointer checks before calling `displayPopup()`
  - `currentSong` null checks before accessing properties like `lastClipInstanceEnteredStartPos`
  - Safety guards in arrangement loop logic to only process when fully initialized
- ✅ **Loop Jump Event Scheduling Fix**: Fixed critical hang issue with balanced safety measures
  - **Issue**: Recent refactoring changed `swungTicksTilNextEvent = 1` to `swungTicksTilNextEvent = 0` after loop jumps, causing hangs during initialization, then overly restrictive safety checks prevented normal loop functionality
  - **Root Cause**: Multiple factors: (1) Setting `swungTicksTilNextEvent = 0` can cause infinite loops during initialization, (2) Loop logic needed proper safety checks without being too restrictive during normal playback
  - **Solution**: (1) Restored `swungTicksTilNextEvent = 1` after `resetPlayPos()` for safe event processing, (2) Implemented balanced safety checks: `currentPlaybackMode == &arrangement`, `currentSong`, `playbackState & PLAYBACK_SWITCHED_ON`, `!currentlyActioningSwungTickOrResettingPlayPos` - sufficient to prevent initialization issues while allowing normal loop functionality
  - **Location**: `PlaybackHandler::actionSwungTick()` in loop-back logic around line 970-1000
- ✅ **Session Mode Isolation Fix**: Fixed critical issue where arrangement loops affected session mode playback
  - **Issue**: Session mode clips would incorrectly start from arrangement loop positions instead of their natural positions
  - **Root Cause**: Loop startup logic in `setupPlaybackUsingInternalClock()` was applied to all playback modes without checking current mode
  - **Solution**: Added `currentPlaybackMode == &arrangement` condition to restrict loop logic to arrangement mode only
  - **Location**: `PlaybackHandler::setupPlaybackUsingInternalClock()` around lines 410-420

**Loop Playback Start Position (Task 1 - Partial)**
- ✅ Smart loop engagement without jarring jumps
- ✅ When playback starts with active loop → Does NOT jump if playhead is ahead of loop end
- ✅ When creating loop behind playhead during stopped playback → No jarring jump (playhead is ahead of loop end)
- ✅ When creating loop behind playhead during active playback → No immediate backwards jump (prevented by runtime loop check)
- ✅ When creating loop ahead/containing playhead → Jumps to loop start (playhead is before loop end)
- ✅ Normal looping behavior continues when playhead naturally reaches loop end during playback
- ✅ No automatic loop activation/deactivation complexity
- ✅ Integration with existing playback priority system
- ✅ **Note Row Resume Fix**: Fixed first notes not playing after loop reset when loop ends at clip boundary
- ✅ Implementation locations:
  - `PlaybackHandler::setupPlaybackUsingInternalClock()` in `/src/deluge/playback/playback_handler.cpp` (smart jump logic based on position)
  - `Arrangement::checkAndHandleArrangerLoop()` in `/src/deluge/playback/mode/arrangement.cpp` (runtime loop boundary checking)
  - `NoteRow::resumePlayback()` in `/src/deluge/model/note/note_row.cpp` (note triggering at exact positions)

### Completed Features

**Loop Row Status Display Functionality (Task 1 - Partial)**
- ✅ Audition pad for loop row (y=0) displays loop status when pressed
- ✅ Shows "ON" when loop exists and is active
- ✅ Shows "OFF" when loop exists but is inactive
- ✅ Shows "LOOP" when no loop exists
- ✅ Implementation location: `ArrangerView::handleAuditionPadAction()` in `/src/deluge/gui/views/arranger_view.cpp`

**Loop Row Mute/Launch Button (Task 1 - Partial)**
- ✅ Status pad (mute/launch) for loop row (y=0) toggles loop activation
- ✅ Only functional when `arrangerLoopExists` is true
- ✅ Toggles `arrangerLoopActive` state between true/false
- ✅ Displays "ON" when loop becomes active, "OFF" when deactivated
- ✅ Triggers UI re-rendering for visual feedback
- ✅ **FIXED**: Mute button now shows green (active) state immediately when loop is initialized
- ✅ Implementation location: `ArrangerView::handleStatusPadAction()` in `/src/deluge/gui/views/arranger_view.cpp`

**Loop Row Hold-and-Press Creation (Task 1 - Partial)**
- ✅ Hold-and-press workflow for loop creation implemented
- ✅ Press and hold start position, then press end position to create loop
- ✅ Release without pressing end creates single-cell loop
- ✅ Auto-activation when loop is created
- ✅ No auto-deletion when pressing outside loop boundaries
- ✅ Visual feedback during loop creation (white square at start position)
- ✅ Compilation and variable naming issues resolved
- ✅ Implementation location: `ArrangerView::handleLoopRowPadAction()` in `/src/deluge/gui/views/arranger_view.cpp`

**Loop Playback Start Position (Task 1 - Partial)**
- ✅ When loop is active and play button is pressed, playback starts from loop beginning
- ✅ Integration with existing playback priority system
- ✅ Only affects new playback sessions, not restarting existing playback
- ✅ Implementation location: `PlaybackHandler::setupPlaybackUsingInternalClock()` in `/src/deluge/playback/playback_handler.cpp`

**Loop Handle Visual Rendering (Task 1 - Partial)**
- ✅ Rainbow color rendering implemented for loop handle visualization
- ✅ HSV to RGB conversion optimized for embedded environment (no std::fmod dependency)
- ✅ Loop row rendering with proper bounds checking and position calculations
- ✅ Visual feedback during loop creation (white square at start position)
- ✅ Implementation location: `ArrangerView::renderLoopRow()` and `ArrangerView::getRainbowColor()` in `/src/deluge/gui/views/arranger_view.cpp`

### Critical Implementation Findings

**Session Mode Loop Isolation Fix**
During implementation, a critical issue was discovered and resolved:

**The Problem:**
- Arrangement loop logic in `setupPlaybackUsingInternalClock()` was being applied to ALL playback modes
- This caused session mode playback to be affected by active arrangement loops
- Session clips would incorrectly start from arrangement loop positions instead of their natural positions

**The Solution:**
- Added `currentPlaybackMode == &arrangement` condition to loop logic in `setupPlaybackUsingInternalClock()`
- Session mode playback is now completely isolated from arrangement loop state
- Arrangement loops only affect playback when in arrangement mode

**Root Cause:**
The loop startup position logic was checking `currentSong->shouldLoopArrangement()` without verifying the current playback mode. This violated the separation of concerns between session and arrangement modes.

**Implementation Location:**
`PlaybackHandler::setupPlaybackUsingInternalClock()` in `/src/deluge/playback/playback_handler.cpp` around lines 410-420.

**Note:** The runtime loop logic in `actionSwungTick()` was already correctly scoped to arrangement mode only.

**Row Indexing Architecture Discovery**
During implementation, a critical misunderstanding of the arrangement view coordinate system was discovered and resolved:

**The Problem:**
- Initial implementation assumed `outputsOnScreen` array used 0-based indexing for outputs
- This caused crashes, non-clickable tracks, and black display areas
- Firmware would hang due to incorrect array access patterns

**The Solution - Correct Indexing Pattern:**
- **Loop Row (y=0):** Special handling, not stored in `outputsOnScreen` array
- **Track Rows (y=1 to 7):** Direct mapping to `outputsOnScreen[y]` (not `outputsOnScreen[y-1]`)
- **Array Population:** `repopulateOutputsOnScreen()` places first track at `outputsOnScreen[1]`, second at `outputsOnScreen[2]`, etc.
- **Access Pattern:** All functions must use `outputsOnScreen[yDisplay]` where `yDisplay` is the display row (1-7)

**Root Cause:**
The `outputsOnScreen` array is sized for the full display height (8 rows) but reserves index 0 for potential future use, making it effectively 1-based for track access.

**Functions Corrected:**
- `renderRow()`: Fixed from `outputsOnScreen[yDisplay - 1]` to `outputsOnScreen[yDisplay]`
- `handleEditPadAction()`: Fixed bounds checking and array access patterns
- `handleStatusPadAction()`: Fixed bounds checking and array access patterns
- `handleAuditionPadAction()`: Fixed bounds checking and array access patterns
- `editPadAction()`: Added proper bounds checking with correct indexing
- `drawMuteSquare()`: Fixed bounds checking and array access patterns
- `drawAuditionSquare()`: Fixed bounds checking and array access patterns

**Embedded Environment Considerations:**
- Removed dependencies on `std::fmod`, `std::abs`, `std::max`, `std::min`
- Implemented embedded-friendly alternatives for mathematical operations
- Used manual bounds checking and simpler modulo operations for color calculations

### Pending Implementation

**Task 1: Complete Loop Row UI**
- ✅ Loop handle visual rendering with rainbow colors
- ✅ Basic playback integration (start from loop beginning)
- ✅ Row indexing architecture properly understood and implemented
- ✅ Embedded environment compatibility (no std library dependencies)
- ✅ Full looping behavior during playback - **FIXED: Playhead now reaches end of loop cell correctly**
- ✅ Loop boundary at arrangement end - **FIXED: Loops now work correctly when they end at arrangement boundary**

**Task 2: Quantized Resampling Integration**
- ⏳ SHIFT + RECORD hook detection
- ⏳ Automatic stop at loop end
w
**Task 3: Loop-to-Song Section Conversion**
- ⏳ RECORD + SONG shortcut implementation
- ⏳ Content identification within loop boundaries
- ⏳ Song section creation logic

**Task 4: Audio Recording/Overdub Enhancement**
- ⏳ Loop boundary constraint verification
- ⏳ Documentation updates

---

**General Implementation Notes:**

* **Performance:** Ensure that the new UI elements and background loop processing do not negatively impact the Deluge's real-time performance.
* **User Experience:** Prioritize intuitive interaction and clear visual feedback for all new features.
* **Edge Cases:** Consider edge cases such as very short loops, loops at the end of the arrangement, or concurrent user actions.
* **Firmware Integration:** Ensure seamless integration with the existing Deluge firmware architecture and codebase.

**Critical Development Lessons Learned:**

* **Array Indexing:** The Deluge arrangement view uses a hybrid indexing system where `outputsOnScreen[0]` is reserved and tracks are stored starting at `outputsOnScreen[1]`. Always use `outputsOnScreen[yDisplay]` where `yDisplay` ranges from 1-7 for tracks.
* **Embedded Environment:** Avoid standard library dependencies (`std::fmod`, `std::abs`, etc.) - implement embedded-friendly alternatives.
* **Bounds Checking:** Always implement comprehensive bounds checking for array access to prevent crashes and hangs.
* **UI Coordinate Systems:** Distinguish between display coordinates (0-7 where 0=loop row) and array indices (1-7 for tracks).
* **Testing Strategy:** Changes to core UI functions require thorough testing of interaction patterns, not just compilation verification.
* **Loop Boundary Fix:** When creating loops, the end position must include the full extent of the selected square. Use `getPosFromSquare(square + 1)` to get the end position of a square, not just `getPosFromSquare(square)` which gives the start position. This ensures the playhead reaches the visual end of the selected loop cell before looping back. **CRITICAL FIX APPLIED**: Fixed single-cell loop creation in `handleLoopRowPadAction()` where `arrangerLoopEnd` was incorrectly set to the same position as `arrangerLoopStart`. Now properly calculates the actual end position using `getPosFromSquare(startSquare + 1)` for both single-cell and multi-cell loops. **VISUAL RENDERING FIX**: Fixed loop display rendering to show correct number of cells by using `getSquareFromPos(arrangerLoopEnd - 1)` for visual rendering while keeping the correct end position for playback logic.
* **Display Pointer Safety:** Always check if the `display` pointer is valid before calling `display->displayPopup()`. Use `if (display) { display->displayPopup("text"); }` to prevent crashes when the display subsystem is not initialized. The SEGGER RTT printf crash indicates null pointer dereferencing in display calls. **CRITICAL SAFETY APPLIED**: Added display pointer validation to all loop status display functions.
* **Function Parameter Validation:** Add null pointer checks for critical objects (`currentSong`, `display`, `ui`) at the start of loop functions to prevent crashes during initialization or invalid states. Return `ActionResult::DEALT_WITH` early if any required objects are null. **CRITICAL SAFETY APPLIED**: Added comprehensive null pointer checks to `handleLoopRowPadAction()`, `handleStatusPadAction()`, and `handleAuditionPadAction()` functions to prevent initialization hangs.
* **State-Based Loop System:** Implemented robust loop management using `arrangerLoopPlayheadInside` boolean flag to track playhead position relative to loop boundaries. The system only performs backwards jumps when the playhead has been inside the loop and reaches the end, preventing jarring jumps when creating loops behind the playhead. **INFINITE LOOP PREVENTION**: Added `static int64_t lastLoopTick` safety mechanism with per-tick limiting to prevent multiple loop operations within the same tick. **AUTOMATIC STATE MANAGEMENT**: Loop state is automatically initialized and cleared during activation, deactivation, and arrangement clearing operations. **IMPLEMENTATION LOCATION**: `/src/deluge/playback/playback_handler.cpp` in `actionSwungTick()` function around lines 957-990, with state management in `/src/deluge/gui/views/arranger_view.cpp` functions.
* **Smooth Loop Engagement:** Implemented state-based loop system to prevent jarring jumps when creating loops. **STATE-BASED ARCHITECTURE**: Introduced `arrangerLoopPlayheadInside` boolean flag in `ArrangerView` to track whether the playhead is currently within loop boundaries. The system only performs backwards jumps when the playhead has been inside the loop region and then reaches the end boundary. **INFINITE LOOP PREVENTION**: Added `static int64_t lastLoopTick` safety mechanism to prevent multiple loop jumps within the same tick, using `lastSwungTickActioned != lastLoopTick` condition. **AUTOMATIC STATE MANAGEMENT**: The `arrangerLoopPlayheadInside` flag is automatically managed during loop activation, deactivation, and clearing operations to ensure consistent behavior across all user interactions. **IMPLEMENTATION LOCATIONS**: State tracking in `PlaybackHandler::actionSwungTick()` around line 957-990, state initialization in `ArrangerView::handleStatusPadAction()` and `ArrangerView::clearArrangement()`. This approach provides reliable loop behavior without complex distance calculations while preventing backwards jumps when loops are created behind the playhead.
* **Loop End-of-Arrangement Fix:** ✅ **FIXED** - Fixed critical issue where loops wouldn't work when they ended exactly at the arrangement boundary. **ROOT CAUSE**: Three-part problem: (1) The arrangement playback logic in `Arrangement::doTickForward()` was stopping playback when reaching the end of all clip instances, preventing the loop logic from executing; (2) The loop boundary detection logic used `currentPos < arrangerLoopEnd` which excluded the exact end position, preventing the `arrangerLoopPlayheadInside` flag from being set when the playhead reached the loop boundary; (3) After loop-back, `swungTicksTilNextEvent` remained at a large value, causing the arrangement logic to stop playback on the next tick. **SOLUTION**: (1) Modified arrangement logic to calculate exact timing for loop boundaries instead of stopping playback; (2) Changed boundary detection to use `currentPos <= arrangerLoopEnd` to include the exact end position; (3) Added `swungTicksTilNextEvent = 1` after loop-back to force immediate event scheduling. **IMPLEMENTATION LOCATIONS**: `/src/deluge/playback/mode/arrangement.cpp` in `doTickForward()` (timing calculation) and `/src/deluge/playback/playback_handler.cpp` in `actionSwungTick()` (boundary detection and event scheduling)
* **Note Row Resume Playback Fix:** ✅ **FIXED** - Fixed critical bug where first notes in instrument clips wouldn't play after arrangement loop reset when loop end coincides with clip end and there's no subsequent clip instance. **ROOT CAUSE**: The `NoteRow::resumePlayback()` function was designed to catch "late" notes (already in progress) but missed notes starting exactly at the resume position. The `maybeStartLateNote()` function uses `notes.search(currentPos, LESS)` which finds notes starting before the current position, but when the loop resets to position 0, notes starting exactly at position 0 aren't found because 0 is not less than 0. **TECHNICAL DETAILS**: During normal playback, `processCurrentPos()` handles note triggering when `newTicksTil <= 0`, but `resumePlayback()` only called `maybeStartLateNote()` which looks for notes where `currentPos` is inside an existing note (between note start and end). **SOLUTION**: Added explicit check for notes starting exactly at current position using `notes.search(currentPos, GREATER_OR_EQUAL)` before checking for late notes. This follows the same triggering pattern as `processCurrentPos()` and ensures consistent behavior between normal playback and loop resume scenarios. **BEHAVIORAL IMPACT**: No breaking changes - only adds missing functionality for exact-position note triggering during resume. All existing late-note catching behavior remains unchanged. **IMPLEMENTATION LOCATION**: `/src/deluge/model/note/note_row.cpp` in `resumePlayback()` function around lines 3152-3164.
* **Note Row Resume Playback Fix:** ✅ **FIXED** - Fixed critical issue where first notes in instrument clips wouldn't play after arrangement loop reset when loop end coincides with clip end and there's no subsequent clip instance. **ROOT CAUSE**: The `NoteRow::resumePlayback()` function only checked for "late" notes (already in progress) via `maybeStartLateNote()`, but missed notes that start exactly at position 0 after loop reset. The search logic used `LESS` comparison which finds notes starting before the current position, but notes starting exactly at position 0 weren't triggered. **SOLUTION**: Added explicit check for notes starting exactly at the current position before checking for late notes. Uses `notes.search(effectiveActualCurrentPos, GREATER_OR_EQUAL)` to find notes at exact position and triggers them with `playNote()` following the same pattern as `processCurrentPos()`. **IMPLEMENTATION LOCATION**: `/src/deluge/model/note/note_row.cpp` in `resumePlayback()` function around lines 3152-3164. This fix ensures consistent note triggering behavior between normal playback and resume-after-loop scenarios.
* **Song Swap Initialization Fix:** ✅ **FIXED** - Fixed critical issue where loops would play from the beginning first, then continue in the loop, causing jarring playback behavior during session-to-arrangement mode transitions. **ROOT CAUSE**: The `doSongSwap()` method was calling `resetPlayPos()` with the raw current position first, then manually adjusting to the loop start position afterward. This caused the arrangement to briefly initialize playback from the original position before jumping to the loop start. **TECHNICAL DETAILS**: The sequence was: (1) `resetPlayPos(currentPos)` - initializes arrangement playback at current position, (2) Manual loop check and position adjustment - causes immediate jump to loop start. This created a brief moment where playback started at the wrong position. **SOLUTION**: Calculate the correct loop-aware starting position before calling `resetPlayPos()`. The method now: (1) Determines if there's an active loop and if the current position should start from loop beginning, (2) Calls `resetPlayPos()` with the already-correct position, (3) No manual adjustment needed since position is already correct. **BEHAVIORAL IMPACT**: Eliminates jarring "start-then-jump" behavior during mode transitions. Loop playback now starts smoothly from the correct position immediately. **IMPLEMENTATION LOCATION**: `/src/deluge/playback/playback_handler.cpp` in `doSongSwap()` method around lines 600-650. Also updated `switchToArrangement()` method for consistency.

** FIXMEs **
- loop must be saved and restored with song
- ✅ loop rendering broken when zoomed and scrolled outside of its visible area - **FIXED**
- ✅ loop jumps back if enabled and playhead is after it - **FIXED** (STATE-BASED APPROACH)
- ✅ loop jumps back if created before the play head - **FIXED** (STATE-BASED APPROACH with infinite loop prevention)
- ✅ first notes don't play after loop reset when loop ends at clip boundary - **FIXED** (NOTE ROW RESUME PLAYBACK FIX)
- ✅ loop plays from beginning first, then continues in loop - **FIXED** (SONG SWAP INITIALIZATION FIX)
- ✅ session mode affected by arrangement loops - **FIXED** (SESSION MODE ISOLATION FIX)
- no need to display "loop" if play started in song mode.
- play doesn't start when I open a clip in arrangement mode.
- make loop markers animation
- ✅ loop doesn't loop if it ends at the end of the arrangement - **FIXED** (THREE-PART FIX)

**REFACTORING NOTES:**
- **State-Based Loop Management**: Implemented `arrangerLoopPlayheadInside` boolean flag to track playhead position relative to loop boundaries
- **Infinite Loop Prevention**: Added `static int64_t lastLoopTick` safety mechanism with per-tick limiting to prevent infinite loops during initialization or edge cases
- **Single Loop Handler**: Consolidated loop handling into single location in `PlaybackHandler::actionSwungTick()` with robust state management
- **Automatic State Tracking**: Loop state is automatically managed during activation, deactivation, and clearing operations for consistent behavior
- **Reduced Code Complexity**: Eliminated complex distance calculations in favor of simple boolean state tracking
- **Enhanced Safety**: Added comprehensive state reset logic in `clearArrangement()` and loop activation functions
- **Reliable Performance**: State-based approach is less dependent on precise timing calculations and more predictable across different playback scenarios
- **More Reliable**: New approach is less dependent on precise timing and state synchronization
