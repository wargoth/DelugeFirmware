# Loop Overdub Bug Fix Attempts

## Problem Statement
When recording audio in arrangement mode with an active loop, if recording continues for more than one loop cycle, the resulting audio clip plays back at double speed (or faster for longer recordings).

## Root Cause Analysis
The `SampleRecorder` was never told to stop capturing at the loop boundary. When the arrangement loop triggered and reset the playhead position, the recorder continued capturing audio linearly. After two loops, the recorder had captured 2x the material that should fit in one loop duration, causing the audio to be compressed/sped up during playback.

## Session View Behavior (Reference)
Analysis showed that audio clips do NOT restart sample playback on loop boundaries during recording. Instead:
- Audio clips continue playing linearly through multiple clip loops during recording
- `setupPlaybackBounds()` uses `originalLength` (not `loopLength`) when recording linearly
- Only after `finishLinearRecording()` is the final `loopLength` set

## Attempts Made

### Attempt 1: Fix in `possiblyBeginArrangementRecording()` (output.cpp)
**Approach**: Add loop params setup after `beginLinearRecording()` is called
```cpp
// In Output::possiblyBeginArrangementRecording()
newClip->beginLinearRecording(modelStackWithTimelineCounter, 0);

if (song->shouldLoopArrangement() && type == OutputType::AUDIO) {
    const ArrangementLoop& loop = song->getArrangementLoop();
    if (loop.exists() && loop.isActive()) {
        AudioClip* audioClip = static_cast<AudioClip*>(newClip);
        if (audioClip && audioClip->recorder) {
            int64_t loopEndPos = loop.getEnd();
            audioClip->recorder->setLoopRecordingParams(loopEndPos);
        }
    }
}
```
**Result**: ❌ Caused startup freeze
**Reason**: `possiblyBeginArrangementRecording()` can be called during song initialization before ArrangementLoop is fully initialized

### Attempt 2: Add safety check with `playbackHandler.isEitherClockActive()`
**Approach**: Guard ArrangementLoop access with playback state check
```cpp
if (playbackHandler.isEitherClockActive() && song->shouldLoopArrangement() && type == OutputType::AUDIO) {
    // ... set loop params
}
```
**Result**: ❌ Still caused startup freeze OR didn't set params
**Reason**: Either still accessed during init, or `isEitherClockActive()` was false when recording actually started

### Attempt 3: Fix in `AudioClip::beginLinearRecording()` (audio_clip.cpp)
**Approach**: Set loop params when recorder is created, with safety checks
```cpp
recorder->autoDeleteWhenDone = true;
recorder->allowNormalization = shouldNormalize;

Song* song = modelStack->song;
if (song && playbackHandler.isEitherClockActive() && currentPlaybackMode == &arrangement
    && song->shouldLoopArrangement()) {
    const ArrangementLoop& loop = song->getArrangementLoop();
    if (loop.exists() && loop.isActive()) {
        int64_t loopEndPos = loop.getEnd();
        recorder->setLoopRecordingParams(loopEndPos);
    }
}
```
**Result**: ❌ Caused startup freeze
**Reason**: `beginLinearRecording()` is called for ALL recording (session and arrangement) and can be called during initialization

### Attempt 4: Check `playbackHandler.playbackState` instead
**Approach**: Use simpler boolean check before accessing loop
```cpp
if (type == OutputType::AUDIO && playbackHandler.playbackState) {
    if (song->shouldLoopArrangement()) {
        // ... set loop params
    }
}
```
**Result**: ❌ Still caused startup freeze
**Reason**: `playbackState` might be set during song load/init

### Attempt 5: Move to `Arrangement::resetPlayPos()` (arrangement.cpp)
**Approach**: Set loop params right after `possiblyBeginArrangementRecording()` succeeds
```cpp
Error error = output->possiblyBeginArrangementRecording(currentSong, newPos);
if (error != Error::NONE) {
    display->displayError(error);
}
else if (output->type == OutputType::AUDIO && currentSong->shouldLoopArrangement()) {
    const ArrangementLoop& loop = currentSong->getArrangementLoop();
    if (loop.exists() && loop.isActive()) {
        Clip* activeClip = output->getActiveClip();
        if (activeClip && activeClip->type == ClipType::AUDIO) {
            AudioClip* audioClip = static_cast<AudioClip*>(activeClip);
            if (audioClip->recorder) {
                int64_t loopEndPos = loop.getEnd();
                audioClip->recorder->setLoopRecordingParams(loopEndPos);
            }
        }
    }
}
```
**Result**: ❌ Still caused startup freeze
**Reason**: Unknown - possibly still accessed during some initialization path

### Attempt 6: Understanding SampleRecorder Loop Detection
**Discovery**: The `setLoopRecordingParams()` check in SampleRecorder has a flaw:
```cpp
// In SampleRecorder::processBuffers()
if (shouldStopAtLoopEnd && arrangement.hasPlaybackActive() && playbackHandler.isEitherClockActive()) {
    int64_t currentArrangerPos = arrangement.lastProcessedPos;
    if (currentArrangerPos >= loopEndPosition) {
        goto doFinishCapturing;
    }
}
```

**Problem Identified**:
- When the loop wraps around, `arrangement.lastProcessedPos` gets reset to loop start position
- After wrapping, `lastProcessedPos < loopEndPosition`, so the check never triggers again
- The recorder continues capturing through multiple loops

**Conclusion**: `setLoopRecordingParams()` infrastructure alone is insufficient for arrangement loops

### Attempt 7: Stop Recording in `handleArrangementLoopOverdubCreation()` (Current)
**Approach**: Detect first loop completion and immediately finish recording
```cpp
// In PlaybackHandler::handleArrangementLoopOverdubCreation()
// Called when loop boundary is detected

if (!output->hasCompletedLoopCycle) {
    // First time hitting loop boundary
    output->hasCompletedLoopCycle = true;

    char modelStackMemory[MODEL_STACK_MAX_SIZE];
    ModelStackWithTimelineCounter* modelStack =
        setupModelStackWithTimelineCounter(modelStackMemory, currentSong, audioClip);

    audioClip->finishLinearRecording(modelStack, nullptr, 0);
    output->recordingInArrangement = false;
    continue;
}
```

**Additional Fix**: Reset flag when recording starts
```cpp
// In Output::possiblyBeginArrangementRecording()
recordingInArrangement = true;
hasCompletedLoopCycle = false;  // Reset for new recording session
```

**Status**: ✅ No startup freeze reported
**Status**: ❌ Double-speed issue still occurs

### Attempt 8: Truncate numSamplesCaptured Before finishLinearRecording()
**Approach**: Truncate `recorder->numSamplesCaptured` BEFORE calling `finishLinearRecording()`
**Hypothesis**: The SampleRecorder continues capturing audio through multiple loops. When `finishLinearRecording()` is called, it calls `endSyncedRecording()`, which sets `sample->lengthInSamples = numSamplesCaptured`. If the recorder has captured 2 loops worth of audio, the sample length is set to 2x what it should be.

**Implementation**:
```cpp
// BEFORE calling finishLinearRecording(), truncate the captured sample count
if (audioClip->recorder) {
    const ArrangementLoop& loop = currentSong->getArrangementLoop();
    if (loop.exists() && loop.isActive()) {
        int32_t loopLengthInTicks = loop.getEnd() - loop.getStart();
        uint32_t loopLengthInSamples =
            (uint32_t)((uint64_t)loopLengthInTicks * kSampleRate / (currentSong->timePerTimerTickBig >> 32));

        // Truncate to loop length
        if (loopLengthInSamples > 0 && audioClip->recorder->numSamplesCaptured > loopLengthInSamples) {
            audioClip->recorder->numSamplesCaptured = loopLengthInSamples;
        }
    }
}

// NOW finish recording with the corrected sample count
audioClip->finishLinearRecording(modelStack, nullptr, 0);
```

**Applied to Three Locations in handleArrangementLoopOverdubCreation()**:
1. First loop completion path (line ~3410)
2. Pending termination path (line ~3450)
3. Overdub completion path (line ~3475)

**Result**: ❌ **DOES NOT FIX THE ISSUE**

**Testing Result**: Same behavior - double-speed playback still occurs

**Why This Doesn't Work**: 
Looking at `SampleRecorder::endSyncedRecording()`, it calculates total length as:
```cpp
totalSampleLengthNowKnown(numSamplesCaptured + numMoreSamplesToCapture, loopEndPointSamples);
```

Even if we truncate `numSamplesCaptured`, the function adds `numMoreSamplesToCapture` (derived from `numSamplesExtraToCaptureAtEndSyncingWise`), so the final sample length is still wrong.

**Key Insight**: Truncating `numSamplesCaptured` alone is insufficient because `endSyncedRecording()` performs additional calculations that add more samples to the total length.

### Attempt 9: Understanding the Real Problem
**Discovery**: The issue is likely NOT in recording, but in clip length calculation.

When `finishLinearRecording()` is called:
1. `originalLength = loopLength` (line 231 in audio_clip.cpp)
2. But the clip's `loopLength` was set based on timeline ticks, not sample duration
3. If recording spanned 2 loops, `loopLength` might represent 2x the intended loop duration

**Key Code in `AudioClip::setupPlaybackBounds()`** (line 520):
```cpp
int32_t length = getCurrentlyRecordingLinearly() ? originalLength : loopLength;
```

The clip uses `originalLength` for playback bounds calculation during recording.

**Hypothesis**: The clip's `loopLength` (in timeline ticks) is being set to the TOTAL recording duration (multiple loops), not just one loop's duration. This then gets copied to `originalLength`, which affects playback speed calculation.

**Need to Investigate**:
1. Where is the clip's `loopLength` initially set when recording starts in arrangement?
2. Does the clip length grow as the arrangement loops?
3. Is the sample-to-tick ratio calculation in `finishLinearRecordingInternal()` using the wrong clip length?

**CRITICAL FINDING** (line 450 in output.cpp):
```cpp
newClip->loopLength = kMaxSequenceLength;
```

**THIS IS THE ROOT CAUSE!**

When a clip is created for arrangement recording, its `loopLength` is set to `kMaxSequenceLength` (maximum value).
Then in `finishLinearRecording()`:
```cpp
originalLength = loopLength;  // Copies kMaxSequenceLength!
```

The clip's `loopLength` MUST be set to the actual arrangement loop length BEFORE `finishLinearRecording()` is called.

### Attempt 10: Set Clip loopLength Before finishLinearRecording()
**Approach**: In `handleArrangementLoopOverdubCreation()`, set the clip's `loopLength` to match the arrangement loop duration BEFORE calling `finishLinearRecording()`.

**Implementation**:
```cpp
// Calculate actual loop length in ticks
const ArrangementLoop& loop = currentSong->getArrangementLoop();
int32_t loopLengthInTicks = loop.getEnd() - loop.getStart();

// Set the clip's loopLength to match the arrangement loop
audioClip->loopLength = loopLengthInTicks;

// NOW finish recording - originalLength will be set correctly
audioClip->finishLinearRecording(modelStack, nullptr, 0);
```

**Applied to Three Locations in handleArrangementLoopOverdubCreation()**:
1. First loop completion path
2. Pending termination path
3. Overdub completion path

**Rationale**:
1. The clip's `loopLength` was set to `kMaxSequenceLength` when created (line 450 in output.cpp)
2. `finishLinearRecording()` does `originalLength = loopLength`
3. The huge `originalLength` causes incorrect tick-to-sample ratio calculation
4. Setting `loopLength` to the actual loop duration BEFORE finishing fixes the ratio

**Status**: ⏳ **NEEDS TESTING** - Build succeeds, must test on hardware to confirm fix

**Testing Result**: ❌ **NO CHANGE IN BEHAVIOR** - Double-speed playback still occurs

**Why This Doesn't Work**: Setting `audioClip->loopLength` before `finishLinearRecording()` has no effect. This suggests:
1. The clip's `loopLength` is not actually used in the playback speed calculation we thought
2. OR there's another place where the length gets overwritten after we set it
3. OR the problem is fundamentally elsewhere in the playback system

## Current State - NOT FIXED ❌

### Critical Finding:
**NONE of the documented attempts (1-10) have changed the behavior.** The double-speed playback issue persists exactly as before, regardless of:
- Where we try to stop/finish recording
- Whether we truncate `numSamplesCaptured`
- Whether we set the clip's `loopLength` before finishing

### What This Tells Us:
The problem is NOT in any of the places we've been modifying:
1. NOT in `handleArrangementLoopOverdubCreation()`
2. NOT in the recorder's sample count
3. NOT in the clip's `loopLength` setting during recording finish
4. The actual root cause is somewhere else entirely

### Hypotheses to Investigate:
1. **Playback interpretation issue**: The recorded audio file is correct, but playback interprets it wrong
2. **Timeline calculation issue**: Something in how `SamplePlaybackGuide::setupPlaybackBounds()` calculates the sync length
3. **Length change propagation**: The clip length gets changed/corrected somewhere after recording, overwriting our fixes
4. **Different code path**: Recording might be finishing through a different code path we haven't identified

### Next Steps:
Need to add logging to understand:
- What is the actual clip `loopLength` when playback starts?
- What is `originalLength` when playback starts?
- What values does `setupPlaybackBounds()` actually use?
- Is `finishLinearRecording()` even being called, or is recording ending another way?

### Conclusion After 10 Failed Attempts:
All modifications to the recording/finishing code path have had **ZERO effect** on behavior. This definitively proves:

1. **We are modifying code that either isn't executed OR doesn't affect the outcome**
2. **The real issue is likely in playback, not recording** - The audio file might be recorded correctly with the right data, but the playback system interprets the timing/speed incorrectly
3. **Need different debugging approach** - Must trace through actual playback to see where the speed/timing gets calculated

The double-speed issue is NOT caused by:
- How/when recording finishes
- The recorder's sample count
- The clip's loopLength at finish time
- Any code in `handleArrangementLoopOverdubCreation()`

It MUST be caused by something in:
- How the playback guide calculates sample positions
- How the clip's length is interpreted during playback
- Some other playback-related calculation we haven't identified yet

## Testing Checklist
- [x] No startup freeze on song load
- [ ] Recording in arrangement loop captures correct duration (one loop)
- [x] Playback speed is correct (not double-speed) - **CONFIRMED STILL BROKEN (all 10 attempts)**
- [ ] Multiple sequential recordings work correctly
- [ ] Overdub functionality works as expected
- [ ] Session mode recording unaffected

**Status**: All attempts (1-10) have failed to change the behavior. The double-speed playback issue persists. We have been modifying the wrong parts of the code. Need to investigate the actual playback system and how it interprets the recorded clip.
