Here's a set of instructions for Copilot (or a developer) to implement the "Dedicated Loop Row & Workflow Enhancements in Arranger View" proposal for the Deluge firmware:

## Copilot Implementation Instructions: Dedicated Loop Row & Workflow Enhancements

This task involves adding a dedicated loop control row to the Arranger View, integrating its functionality with existing playback, resampling, and song structure features.

---

### **Task 1: Implement Dedicated Loop Row in Arranger View**

**Objective:** Create a new, persistent UI element in the Arranger View that allows users to define a single active loop.

**Sub-tasks:**

1.  **UI Element Creation:**
    * Add a new row at the very top of the Arranger View grid.
    * Ensure this row remains visible and accessible even when the user scrolls horizontally or vertically within the Arranger View.
    * This row will house a "Loop Handle" which visually represents the active loop.
    * **Mute/Launch Button:** The mute/launch button for the loop row toggles loop activation:
        - **Green:** Loop is active and will affect playback
        - **Unlit/Off:** Loop is inactive (loop handle may exist but doesn't affect playback)
        - Only functional when a loop exists (`arrangerLoopExists` is true)
    * **Audition Pad:** The audition pad for the loop row displays loop status when pressed:
        - Shows "ON" when a loop exists and is active
        - Shows "OFF" when a loop exists but is inactive
        - Shows "LOOP" when no loop exists
        - Pad remains visually yellow but serves as an informational display
2.  **Loop Handle Visuals:**
    * Render the Loop Handle within this row as a distinct "clip-like" element.
    * Implement **rainbow color rendering** for the Loop Handle to clearly distinguish it from other clip types in the arranger.
    * The visual length of the Loop Handle should accurately reflect its set duration in bars/beats.
3.  **Loop Handle Interaction & Logic:**
    * **Hold-and-Press Creation:** Users create loops by pressing and holding the start position, then pressing the end position while still holding start. Releasing without pressing end creates a single-cell loop.
    * **Auto-Activation:** When a loop is created, it automatically becomes active (no need to manually activate via mute/launch button).
    * **No Auto-Deletion:** Loops are not automatically removed when pressing outside their boundaries - they persist until explicitly managed.
    * **Playback Control:** Ensure that when playback is initiated and a Loop Handle exists AND is active (green mute/launch button), the arranger view loops continuously within the boundaries defined by the Loop Handle.
    * **Status Display:** Pressing the audition pad displays the current loop status on the screen for user feedback.

---

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

**Loop Playback Start Position (Task 1 - Partial)**
- ✅ Smart loop engagement without jarring jumps
- ✅ When playback starts with active loop → Does NOT jump if playhead is ahead of loop end
- ✅ When creating loop behind playhead during stopped playback → No jarring jump (playhead is ahead of loop end)
- ✅ When creating loop behind playhead during active playback → No immediate backwards jump (prevented by runtime loop check)
- ✅ When creating loop ahead/containing playhead → Jumps to loop start (playhead is before loop end)
- ✅ Normal looping behavior continues when playhead naturally reaches loop end during playback
- ✅ No automatic loop activation/deactivation complexity
- ✅ Integration with existing playback priority system
- ✅ Implementation locations:
  - `PlaybackHandler::setupPlaybackUsingInternalClock()` in `/src/deluge/playback/playback_handler.cpp` (smart jump logic based on position)
  - `Arrangement::checkAndHandleArrangerLoop()` in `/src/deluge/playback/mode/arrangement.cpp` (runtime loop boundary checking)

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

**Task 2: Quantized Resampling Integration**
- ⏳ SHIFT + RECORD hook detection
- ⏳ Automatic stop at loop end
- ⏳ User feedback for auto-stop

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
* **Coordinate Conversion Functions:** Use the correct coordinate conversion functions - `getSquareFromPos()` converts time positions to display squares, while `getPosFromSquare()` converts display squares to time positions. Using them incorrectly will result in broken rendering and positioning. **CRITICAL SAFETY APPLIED**: Added bounds validation for coordinate conversion results to prevent invalid calculations that could cause hangs.
* **Smooth Loop Engagement:** Implemented simple and robust solution to prevent jarring jumps when creating loops. **MUSICAL BEHAVIOR IMPROVED**: Modified `PlaybackHandler::setupPlaybackUsingInternalClock()` with the rule "do not jump if playhead is ahead of loop end". When the current playback position is ahead of (past) the loop end, playback starts from the current position instead of jumping to the loop start. **SIMPLIFIED APPROACH**: Replaced complex state tracking with a simple distance-based approach in `PlaybackHandler::actionSwungTick()`. When a loop boundary is reached, the system only jumps backwards if the playhead is not too far past the loop end (within one loop length). This prevents jarring backwards jumps when loops are created behind the playhead while preserving normal looping behavior for natural forward progression. The approach eliminates the need for complex state flags and timing-sensitive logic, making the system more reliable and predictable.
* **Loop Rendering Visibility Fix:** Fixed critical rendering issue where loop handles would break when zoomed and scrolled outside visible area. **COORDINATE CONVERSION FIX**: Implemented proper visibility checking in `ArrangerView::renderLoopRow()` to detect when loops are completely outside the visible area and skip rendering. Previously, the clamping logic incorrectly handled negative coordinates and coordinates beyond render width, causing visual artifacts and potential crashes. **BOUNDS CHECKING IMPROVED**: Added early return when `loopEndSquare < 0` or `loopStartSquare >= renderWidth` to prevent invalid rendering operations. The fix ensures loops render correctly regardless of zoom level or scroll position while maintaining performance by avoiding unnecessary computations for off-screen content. **IMPLEMENTATION LOCATION**: `/src/deluge/gui/views/arranger_view.cpp` in `renderLoopRow()` function with proper coordinate bounds validation using `getSquareFromPos()` with zoom and scroll parameters.

** FIXMEs **
- loop must be saved and restored with song
- ✅ loop rendering broken when zoomed and scrolled outside of its visible area - **FIXED**
- ✅ loop jumps back if enabled and playhead is after it - **FIXED**
- ✅ loop jumps back if created before the play head - **FIXED** (REFACTORED with simplified approach)
- no need to display "loop" if play started in song mode.
- play doesn't start when I open a clip in arrangement mode.
- make loop markers animation

**REFACTORING NOTES:**
- **Simplified State Management**: Removed complex `arrangerLoopJustActivated` flag and associated timing-sensitive logic
- **Single Loop Handler**: Consolidated loop handling into single location in `PlaybackHandler::actionSwungTick()`
- **Distance-Based Logic**: Replaced state tracking with simple distance calculation to determine when backwards jumping should occur
- **Reduced Code Complexity**: Eliminated unused `Arrangement::checkAndHandleArrangerLoop()` function and related state variables
- **More Reliable**: New approach is less dependent on precise timing and state synchronization
q
