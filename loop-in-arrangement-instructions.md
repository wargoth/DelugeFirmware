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
    * **Audition Pad:** The audition pad for the loop row is always **yellow** and does nothing when pressed (non-functional).
2.  **Loop Handle Visuals:**
    * Render the Loop Handle within this row as a distinct "clip-like" element.
    * Implement **rainbow color rendering** for the Loop Handle to clearly distinguish it from other clip types in the arranger.
    * The visual length of the Loop Handle should accurately reflect its set duration in bars/beats.
3.  **Loop Handle Interaction & Logic:**
    * **Simple Press-Based Interaction:** The Loop Handle uses a simple press-based interaction model:
        - **First Press:** Pressing any position in the empty loop row sets the loop start point
        - **Second Press:** Pressing a different position sets the loop end point, completing the loop creation
        - **Loop Removal:** Pressing anywhere in the loop row when a loop already exists removes the current loop
        - **No Dragging:** The system does not support dragging operations - all interactions are single press actions
    * **Two-Press Loop Creation:**
        - Press 1: Sets loop start position (shows visual feedback, e.g., single pad highlight)
        - Press 2: Sets loop end position and creates the complete loop handle
        - If Press 2 is to the left of Press 1, automatically swap the positions
    * **Single Instance Only:** Only one loop handle can exist at a time. Any press when a loop exists removes it.
    * **No Boundary Constraints:** Loop handles ignore existing clip boundaries when being created.
    * **Default Length:** Not applicable since length is determined by the two press positions.
    * **Loop Activation:** When the Loop Handle is created via the two-press method, its defined start and end points are stored, but the loop only affects playback when activated via the mute/launch button.
    * **Playback Control:** Ensure that when playback is initiated and a Loop Handle exists AND is active (green mute/launch button), the arranger view loops continuously within the boundaries defined by the Loop Handle.
    * **Activation Toggle:** The mute/launch button toggles between active (green) and inactive (unlit) states, controlling whether the loop affects playback.

---

### **Task 2: Integrate Quantized Resampling with Active Loop**

**Objective:** Modify the `SHIFT` + `RECORD` resampling function to automatically stop at the end of the active loop.

**Sub-tasks:**

1.  **Identify Resampling Trigger:** Hook into the existing `SHIFT` + `RECORD` command trigger for resampling.
2.  **Check for Active Loop:** Before starting the resampling process, check if a Loop Handle exists AND is active (green mute/launch button) in the Arranger View.
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
    * Ensure this shortcut is only active/responsive when a Loop Handle currently exists AND is active (green mute/launch button) in the Arranger View.
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

**General Implementation Notes:**

* **Performance:** Ensure that the new UI elements and background loop processing do not negatively impact the Deluge's real-time performance.
* **User Experience:** Prioritize intuitive interaction and clear visual feedback for all new features.
* **Edge Cases:** Consider edge cases such as very short loops, loops at the end of the arrangement, or concurrent user actions.
* **Firmware Integration:** Ensure seamless integration with the existing Deluge firmware architecture and codebase.

---

### **Implementation Reference: Clip-Like Behavior for Loop Handles**

**Based on existing clip interaction patterns in `src/deluge/gui/views/arranger_view.cpp`:**

**Key Variables to Implement:**
- `arrangerLoopStart` - Start position of the loop
- `arrangerLoopEnd` - End position of the loop
- `arrangerLoopExists` - Whether a loop handle exists (visible on grid)
- `arrangerLoopActive` - Whether loop is currently active (affects playback)
- `arrangerLoopFirstPressPos` - Position of first press when creating loop
- `arrangerLoopCreationInProgress` - Whether user is in middle of two-press creation

**Interaction Logic:**

1. **Two-Press Creation Logic:**
   ```cpp
   // On press in loop row
   if (!arrangerLoopExists && !arrangerLoopCreationInProgress) {
       // First press - start loop creation
       arrangerLoopFirstPressPos = pressPosition;
       arrangerLoopCreationInProgress = true;
       // Show visual feedback for first press
   }
   else if (!arrangerLoopExists && arrangerLoopCreationInProgress) {
       // Second press - complete loop creation
       arrangerLoopStart = min(arrangerLoopFirstPressPos, pressPosition);
       arrangerLoopEnd = max(arrangerLoopFirstPressPos, pressPosition);
       arrangerLoopExists = true;
       arrangerLoopActive = false; // Created but not active by default
       arrangerLoopCreationInProgress = false;
   }
   else if (arrangerLoopExists) {
       // Loop exists - remove it
       arrangerLoopExists = false;
       arrangerLoopActive = false;
       arrangerLoopCreationInProgress = false;
   }
   ```

2. **Mute/Launch Button Logic:**
   ```cpp
   // On mute/launch button press for loop row
   if (arrangerLoopExists) {
       arrangerLoopActive = !arrangerLoopActive;
       // Update button LED: green if active, unlit if inactive
   }
   ```

3. **Visual Feedback States:**
   - No loop: Empty loop row, unlit mute/launch button, yellow audition pad
   - First press made: Single pad highlighted, unlit mute/launch button, yellow audition pad
   - Loop exists but inactive: Full rainbow handle, unlit mute/launch button, yellow audition pad
   - Loop exists and active: Full rainbow handle, green mute/launch button, yellow audition pad

4. **UI Mode Handling:**
   - Add `UI_MODE_LOOP_CREATION_FIRST_PRESS` state
   - Handle loop creation states in pad action logic
   - Handle mute/launch button presses specifically for loop row
   - Audition pad press for loop row should be ignored/non-functional

**Rendering Reference:**
- Loop handle should render in a dedicated top row (y = -1 conceptually, above normal clip rows)
- Use rainbow colors to distinguish from regular clips
- Visual length should accurately reflect loop duration in bars/beats

**Key Differences from Clips:**
- No dragging or resizing operations - only simple press interactions
- Two-press creation method instead of click-and-drag
- Any press on existing loop removes it (much simpler than clip deletion)
- Only one instance allowed globally
- Positioned in dedicated loop row, not in regular track rows
- Different color scheme (rainbow) for visual distinction
- No collision detection needed since only one can exist
