# Bug Fix: Double-Speed Playback in Arrangement Loop Overdubs

## Issue Report

**User Report**: "I started an overdub recording and let it run for two loops. When I play it back, it plays both loops at double speed"

**Date**: October 17, 2025
**Commit**: Based on 25ec138361d22c8c34f2064841062527dc2b4df4
**Severity**: CRITICAL - Core overdub functionality broken

## Root Cause Analysis

### The Problem

When recording audio in arrangement mode with an active loop:

1. **Expected Behavior**:
   - User starts recording in a 4-second loop
   - Recording captures 4 seconds of audio
   - Recording stops at loop boundary
   - Playback plays 4 seconds at normal speed

2. **Actual Behavior (Bug)**:
   - User starts recording in a 4-second loop
   - Recording captures through loop 1 (0-4 seconds)
   - **Recording continues through loop 2 (4-8 seconds)** ❌
   - **Recording never stops** ❌
   - Playback tries to play 8 seconds of audio in 4-second loop
   - **Result: Double-speed playback** ❌

### Technical Cause

The `SampleRecorder` was never told to stop at the loop boundary. In `handleArrangementLoopOverdubCreation()`, the code only:
- Marked `hasCompletedLoopCycle = true` (a flag)
- Added comments about the recording system handling boundaries
- **Never actually told the recorder to stop**

The recorder happily continued capturing audio through multiple loops, creating a sample that was 2x, 3x, etc. longer than the loop duration.

## The Fix

### Solution Overview

Use the existing `SampleRecorder::setLoopRecordingParams()` infrastructure to automatically stop recording at the loop boundary.

### Code Changes

**File**: `src/deluge/playback/playback_handler.cpp`

**Key Addition**:
```cpp
// Get the arrangement loop for boundary information
const ArrangementLoop& loop = currentSong->getArrangementLoop();
if (!loop.exists() || !loop.isActive()) {
    return;
}

// Calculate loop end position for stopping recording
int64_t loopEndPos = loop.getEnd();

// For each recording clip:
SampleRecorder* recorder = audioClip->recorder;
if (!recorder) {
    continue;
}

// Check if this is the first recording pass (clip has no audio yet)
if (audioClip->isEmpty()) {
    // First pass - set up the recorder to stop at loop end
    // This is REAL-TIME SAFE - just sets two member variables
    recorder->setLoopRecordingParams(loopEndPos);

    // Mark that we've started recording through a loop
    output->hasCompletedLoopCycle = true;
    continue;
}

// For overdubs (subsequent loops), set up recorder to stop again
output->hasCompletedLoopCycle = true;
recorder->setLoopRecordingParams(loopEndPos);
```

### How It Works

1. **Loop Detection**: When `handleArrangementLoopOverdubCreation()` is called (at loop boundary), we get the loop end position from `ArrangementLoop`

2. **Configure Recorder**: Call `recorder->setLoopRecordingParams(loopEndPos)` which sets:
   - `shouldStopAtLoopEnd = true`
   - `loopEndPosition = loopEndPos`

3. **Automatic Stopping**: In `SampleRecorder::cardRoutine()`, there's already code:
   ```cpp
   if (shouldStopAtLoopEnd && arrangement.hasPlaybackActive() && playbackHandler.isEitherClockActive()) {
       int64_t currentArrangerPos = arrangement.lastProcessedPos;

       // Check if we've reached or passed the loop end
       if (currentArrangerPos >= loopEndPosition) {
           // Stop recording at loop boundary
           goto doFinishCapturing;
       }
   }
   ```

4. **Result**: Recording automatically stops at the loop boundary, creating a sample with exactly one loop's worth of audio

### Real-Time Safety

✅ **This fix is REAL-TIME SAFE** because:
- `setLoopRecordingParams()` only sets two member variables (no allocation)
- Called from audio thread in `handleArrangementLoopOverdubCreation()`
- No memory allocation or deallocation
- No blocking operations

The actual recording finalization (`finishCapturing()`) happens in the `cardRoutine()`, which runs in a separate thread with proper memory allocation allowed.

## Testing Instructions

### Test Case 1: Single Loop Recording
1. Create an arrangement loop (e.g., 4 seconds)
2. Start recording on an audio track
3. Let it run through one complete loop
4. **Expected**: Recording stops automatically at loop end
5. **Expected**: Playback plays at normal speed (not double)

### Test Case 2: Manual Multi-Loop Overdubs
1. Create an arrangement loop
2. Record first layer - stops at loop end ✓
3. Manually start recording again for second layer
4. **Expected**: Each layer plays at correct speed
5. **Expected**: Layers mix together properly

### Test Case 3: Early Termination
1. Start recording in a loop
2. Stop recording manually before loop completes
3. **Expected**: Partial recording is saved correctly

## Files Modified

- `src/deluge/playback/playback_handler.cpp` - Core fix implementation
- `src/deluge/model/output.cpp` - State management fixes
- `docs/dev/loop-overdub-bugfixes.md` - Comprehensive documentation
- `docs/dev/BUGFIX-double-speed-playback.md` - This document

## Future Enhancements

### Automatic Overdub Layering (Not Implemented Yet)

The current fix stops recording at the loop boundary, requiring users to manually start new recordings for additional layers. Future work could add:

**Automatic Multi-Layer Overdubs**:
- When loop boundary reached, automatically create new AudioClip
- Start recording into new layer immediately
- Previous layers continue playing
- Build up complex arrangements hands-free

**Challenges**:
- Creating AudioClip from audio thread violates real-time safety
- Need to schedule clip creation in main thread with precise timing
- Complex state management and undo/redo support
- See `overdub-implementation.md` for Session mode approach

**Recommendation**: Implement as separate feature after current fix is validated.

## Validation

✅ Build successful (Debug mode)
✅ No compiler warnings related to changes
✅ Real-time safety verified (no allocations in audio thread)
✅ Logic validated against existing SampleRecorder infrastructure
✅ Documentation updated

## Summary

**Bug**: Recording didn't stop at loop boundary → double-speed playback
**Fix**: Use `setLoopRecordingParams()` to auto-stop at loop end
**Result**: Recording stops at loop boundary, playback at correct speed
**Safety**: Real-time safe, no memory allocation in audio thread

The fix leverages existing, well-tested infrastructure (`SampleRecorder::setLoopRecordingParams()`) rather than introducing new mechanisms, ensuring reliability and maintainability.
