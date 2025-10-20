# Bug Fix: Arrangement Loop Double-Speed Playback

## Issue
When recording audio in arrangement mode with an active loop, if recording continued for more than one loop cycle, the resulting audio clip would play back at double speed (or faster for longer recordings).

## Root Cause
The `SampleRecorder` was never told to stop capturing at the loop boundary. When the arrangement loop triggered and reset the playhead position, the recorder continued capturing audio linearly. After two loops, the recorder had captured 2x the material that should fit in one loop duration, causing the audio to be compressed/sped up during playback.

## Session View Behavior (Reference)
Analysis of session mode showed that audio clips do NOT restart sample playback on loop boundaries during recording. Instead:
- Audio clips continue playing linearly through multiple clip loops during recording
- `setupPlaybackBounds()` uses `originalLength` (not `loopLength`) when recording linearly
- Only after `finishLinearRecording()` is the final `loopLength` set

## Solution Location
The fix is implemented in `AudioClip::beginLinearRecording()` - called from the **main thread** when recording starts (NOT in the audio thread).

### Why This Location?
1. **Not in `possiblyBeginArrangementRecording()`** - This caused startup freeze because it's called during song initialization before ArrangementLoop is fully initialized
2. **Not in `handleArrangementLoopOverdubCreation()`** - This runs in the audio thread and is too late (recording already started)
3. **✅ In `beginLinearRecording()`** - Perfect timing: after recorder is created but before recording starts, on main thread

## Implementation

### Code Changes
```cpp
// In AudioClip::beginLinearRecording() - after recorder creation
recorder = AudioEngine::getNewRecorder(...);
recorder->autoDeleteWhenDone = true;
recorder->allowNormalization = shouldNormalize;

// NEW: Configure loop recording params if in arrangement loop
Song* song = modelStack->song;
if (song && song->shouldLoopArrangement() && currentPlaybackMode == &arrangement) {
    const ArrangementLoop& loop = song->getArrangementLoop();
    if (loop.exists() && loop.isActive()) {
        int64_t loopEndPos = loop.getEnd();
        recorder->setLoopRecordingParams(loopEndPos);
    }
}

return Clip::beginLinearRecording(modelStack, buttonPressLatency);
```

### Include Addition
Added `#include "playback/mode/playback_mode.h"` to access the global `currentPlaybackMode` variable.

## How It Works
1. **Recording Start**: When `beginLinearRecording()` is called, check if we're in arrangement mode with an active loop
2. **Configure Recorder**: Call `recorder->setLoopRecordingParams(loopEndPos)` to tell recorder where to stop
3. **Automatic Stop**: SampleRecorder automatically stops capturing when playback position reaches `loopEndPos`
4. **Correct Duration**: Recording captures exactly one loop's worth of audio, preventing speed-up on playback

## Thread Safety
- ✅ `beginLinearRecording()` runs on **main thread** (safe to access ArrangementLoop)
- ✅ `setLoopRecordingParams()` is **thread-safe** (designed to be called from main thread)
- ✅ No memory allocation in audio thread
- ✅ No ArrangementLoop access during initialization (prevents startup freeze)

## Testing Recommendations
1. Record audio in arrangement mode with loop active
2. Let recording run for 2+ loop cycles
3. Stop recording and verify playback speed is correct
4. Verify no startup freeze on song load
5. Test with different loop lengths and positions

## Related Files
- `src/deluge/model/clip/audio_clip.cpp` - Fix implementation
- `src/deluge/model/sample/sample_recorder.cpp` - Loop recording infrastructure
- `src/deluge/playback/playback_handler.cpp` - Loop detection and overdub handling
- `src/deluge/model/song/arrangement_loop.cpp` - Loop boundary management

## Comparison: Failed Approaches

### ❌ Approach 1: Fix in possiblyBeginArrangementRecording()
**Location**: `output.cpp` - Called when recording starts
**Problem**: Caused **startup freeze** because ArrangementLoop not initialized during song load
**Lesson**: Don't access complex state during initialization

### ❌ Approach 2: Fix in handleArrangementLoopOverdubCreation()
**Location**: `playback_handler.cpp` - Called on loop boundary (audio thread)
**Problem**: Too late - recording already started, and runs in audio thread
**Lesson**: Configure before starting, not during

### ✅ Approach 3: Fix in beginLinearRecording()
**Location**: `audio_clip.cpp` - Called when recorder created (main thread)
**Success**: Perfect timing, safe thread, no initialization issues
