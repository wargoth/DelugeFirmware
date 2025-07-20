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

### Pending Implementation

**Task 1: Complete Loop Row UI**
- ⏳ Loop handle visual rendering with rainbow colors
- ✅ Basic playback integration (start from loop beginning)
- ⏳ Full looping behavior during playback

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
