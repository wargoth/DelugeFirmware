# Deluge Overdub Implementation Documentation

## Overview

**Overdubbing** in the Deluge firmware refers to the process of layering additional audio recordings on top of existing audio tracks. This is a fundamental looping feature that allows musicians to build complex arrangements by recording new material while playing back existing loops.

## Core Concepts

### What is Overdubbing?

Overdubbing is a recording technique where musicians add new sounds or instruments to an existing recording at different times. In the context of loop-based devices like the Deluge, this means:

- Recording new audio material while existing loops are playing
- Building up complex layered compositions incrementally
- Creating harmonies, counter-melodies, or rhythm parts on top of base loops
- Non-destructive layering that preserves original recordings

### Two Types of Overdubbing

The Deluge supports two distinct overdub modes:

1. **Clone-based Overdubs**: Traditional Deluge behavior where a new clip is created as a copy of the existing clip
   - Creates a separate AudioClip instance
   - Maintains independent control over each layer
   - Used in rows layout or when `shouldCloneForOverdubs()` returns true
   - Provides full undo/redo capability for each layer

2. **In-place Overdubs**: Modern behavior where audio is mixed directly into the existing clip
   - Records directly into the existing AudioClip
   - More memory efficient
   - Used in grid layout when clone behavior is disabled
   - Simpler workflow for basic overdubbing

### Overdub Types

```cpp
enum class OverDubType {
    Normal,           // Standard overdub behavior
    ContinuousLayering // Special mode for continuous recording sessions
};
```

- **Normal**: Standard single overdub creation
- **ContinuousLayering**: Enables continuous overdub recording with output cloning

## Key Data Structures

### AudioClip Properties

```cpp
class AudioClip : public Clip {
    bool overdubsShouldCloneOutput;  // Controls whether outputs should be cloned for overdub
    bool isUnfinishedAutoOverdub;    // Tracks incomplete overdub state
    bool isPendingOverdub;           // Marks clips waiting to begin recording
    OverDubType overdubNature;       // Type of overdub (Normal or ContinuousLayering)
};
```

### SampleHolder Properties

```cpp
class SampleHolder : public AudioFileHolder {
    uint32_t startPos;  // Sample start position (in samples)
    uint32_t endPos;    // Sample end position (in samples)
    // Both are set automatically during recording completion
};
```

### SampleRecorder Properties

```cpp
class SampleRecorder {
    Sample* sample;                              // The audio sample being recorded
    bool recordingExtraMargins;                  // Whether to record pre/post margins
    uint32_t numSamplesCaptured;                // Number of samples recorded so far
    int32_t numSamplesToRunBeforeBeginningCapturing; // Latency compensation
    RecorderStatus status;                       // Current recording state
};
```

## Implementation Flow

### 1. Overdub Creation (`Song::createPendingNextOverdubBelowClip`)

**Location**: `src/deluge/model/song/song.cpp:5445`

This is the main entry point for creating overdubs in song view.

```cpp
Clip* Song::createPendingNextOverdubBelowClip(Clip* clip, int32_t clipIndex, OverDubType newOverdubNature) {
    Clip* newClip = clip; // Default to existing clip

    // No overdubs during soloing - too complex for UI management
    if (anyClipsSoloing) {
        return nullptr;
    }

    // Choose clone-based vs in-place overdub based on layout and settings
    if (sessionLayout == SessionLayoutType::SessionLayoutTypeRows || clip->shouldCloneForOverdubs()) {
        // Clone-based: Create new clip instance
        char modelStackMemory[MODEL_STACK_MAX_SIZE];
        ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, this);
        ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

        newClip = clip->cloneAsNewOverdub(modelStackWithTimelineCounter, newOverdubNature);

        if (newClip && newClip != clip) {
            newClip->overdubNature = newOverdubNature;
            sessionClips.insertClipAtIndex(newClip, clipIndex);

            // Update UI scroll position to accommodate new clip
            if (clipIndex != songViewYScroll) {
                songViewYScroll++;
            }

            // Request UI re-render
            sessionView.requestRendering(getRootUI());
        }
    } else {
        // In-place: Setup existing clip for overdub recording
        clip->setupOverdubInPlace(newOverdubNature);
    }

    return newClip;
}
```

**Key Features:**
- Handles both clone-based and in-place overdubs
- Prevents overdubs during soloing (UI complexity)
- Manages UI scroll position for new clips
- Integrates with session layout preferences

### 2. Audio Clip Creation (`AudioClip::cloneAsNewOverdub`)

**Location**: `src/deluge/model/clip/audio_clip.cpp:251`

Creates a new AudioClip instance for clone-based overdubs.

```cpp
Clip* AudioClip::cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStackOldClip, OverDubType newOverdubNature) {
    // 1. Allocate memory for new AudioClip
    void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioClip));
    if (!clipMemory) {
        display->displayError(Error::INSUFFICIENT_RAM);
        return nullptr;
    }

    // 2. Create new clip instance using placement new
    AudioClip* newClip = new (clipMemory) AudioClip();

    // 3. Setup new clip for overdub recording
    newClip->setupForRecordingAsAutoOverdub(this, modelStackOldClip->song, newOverdubNature);

    // 4. Create model stack for new clip
    char modelStackMemoryNewClip[MODEL_STACK_MAX_SIZE];
    ModelStackWithTimelineCounter* modelStackNewClip =
        setupModelStackWithTimelineCounter(modelStackMemoryNewClip, modelStackOldClip->song, newClip);

    // 5. Set output and handle errors
    Error error = newClip->setOutput(modelStackNewClip, output, this);

    if (error != Error::NONE) {
        // Clean up on failure
        newClip->~AudioClip();
        delugeDealloc(clipMemory);
        display->displayError(Error::INSUFFICIENT_RAM);
        return nullptr;
    }

    return newClip;
}
```

**Key Features:**
- Uses custom memory allocator for performance
- Proper error handling with cleanup
- Creates model stack for new clip management
- Links new clip to same output as original

### 3. Overdub Setup (`Clip::setupForRecordingAsAutoOverdub`)

**Location**: `src/deluge/model/clip/clip.cpp:108`

Configures a clip for overdub recording by copying properties and setting recording state.

```cpp
void Clip::setupForRecordingAsAutoOverdub(Clip* existingClip, Song* song, OverDubType newOverdubNature) {
    // Copy all basic properties from source clip
    copyBasicsFrom(existingClip);

    // Calculate new length based on overdub type
    uint32_t newLength = existingClip->loopLength;

    // For normal overdubs (not continuous layering), optimize for screen length
    if (newOverdubNature != OverDubType::ContinuousLayering) {
        uint32_t currentScreenLength = currentSong->xZoom[NAVIGATION_CLIP] << kDisplayWidthMagnitude;

        // Use screen length if loop length is a multiple of screen length
        if ((newLength % currentScreenLength) == 0) {
            newLength = currentScreenLength;
        }
    }

    // Set clip properties for overdub recording
    loopLength = originalLength = newLength;
    soloingInSessionMode = existingClip->soloingInSessionMode;
    armState = ArmState::ON_NORMAL;      // Ready to record
    activeIfNoSolo = false;              // Not active until recording starts
    wasActiveBefore = false;
    isPendingOverdub = true;             // Marked as pending overdub
    isUnfinishedAutoOverdub = true;      // Marked as incomplete
}
```

**Key Features:**
- Copies all relevant properties from source clip
- Optimizes loop length for UI display
- Sets appropriate recording state flags
- Handles continuous layering mode differently

### 4. Recording Initiation (`AudioClip::beginLinearRecording`)

**Location**: `src/deluge/model/clip/audio_clip.cpp:152`

Starts the actual audio recording process with appropriate input configuration.

```cpp
Error AudioClip::beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency) {
    AudioInputChannel inputChannel;
    Output* outputRecordingFrom;
    int32_t numChannels;
    bool shouldNormalize = false;

    if (isEmpty()) {
        // First recording - use external input source
        inputChannel = ((AudioOutput*)output)->inputChannel;
        outputRecordingFrom = ((AudioOutput*)output)->getOutputRecordingFrom();
        numChannels = (inputChannel >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION ||
                      inputChannel == AudioInputChannel::STEREO) ? 2 : 1;
        shouldNormalize = (inputChannel < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION);
    } else {
        // Overdub recording - record from the output (internal mixing)
        inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
        outputRecordingFrom = output;
        numChannels = 2;  // Always stereo for overdubs
    }

    // Determine if we need recording margins for external inputs
    bool shouldRecordMarginsNow = FlashStorage::audioClipRecordMargins &&
                                 inputChannel < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION;

    // Create the actual audio recorder
    recorder = AudioEngine::getNewRecorder(numChannels, AudioRecordingFolder::CLIPS, inputChannel, true,
                                          shouldRecordMarginsNow, buttonPressLatency, false, outputRecordingFrom);
    if (!recorder) {
        return Error::INSUFFICIENT_RAM;
    }

    // Configure recorder settings
    recorder->autoDeleteWhenDone = true;
    recorder->allowNormalization = shouldNormalize;

    // Delegate to base class for common setup
    return Clip::beginLinearRecording(modelStack, buttonPressLatency);
}
```

**Key Features:**
- Distinguishes between first recording and overdub
- Uses SPECIFIC_OUTPUT for overdub recording (internal mixing)
- Handles both mono and stereo input sources
- Applies proper latency compensation
- Configures recording margins for external inputs

### 5. Start/End Position Handling

The start and end positions are automatically calculated and set during the recording process through a multi-stage approach:

#### A. SampleRecorder Setup (`SampleRecorder::setup`)

**Location**: `src/deluge/model/sample/sample_recorder.cpp:105`

```cpp
Error SampleRecorder::setup(int32_t newNumChannels, AudioInputChannel newMode, bool newKeepingReasons,
                            bool shouldRecordExtraMargins, AudioRecordingFolder newFolderID, int32_t buttonPressLatency,
                            Output* outputRecordingFrom_) {
    // Initialize sample with proper format
    sample->audioDataStartPosBytes = shouldRecordExtraMargins ? 112 : 44; // WAV header offset
    sample->byteDepth = 3;           // 24-bit audio
    sample->numChannels = newNumChannels;
    sample->lengthInSamples = 0x8FFFFFFFFFFFFFFF;     // Initially max value
    sample->audioDataLengthBytes = 0x8FFFFFFFFFFFFFFF; // Initially max value
    sample->sampleRate = kSampleRate; // 44.1kHz

    // Latency compensation setup
    numSamplesToRunBeforeBeginningCapturing = numSamplesExtraToCaptureAtEndSyncingWise =
        (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) ? kAudioRecordLagCompensation : 0;

    // Button press latency compensation for external sources only
    if (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
        numSamplesToRunBeforeBeginningCapturing -= buttonPressLatency;
    }

    // Handle recording margins for external inputs
    if (shouldRecordExtraMargins) {
        sample->fileLoopStartSamples =
            SSI_RX_BUFFER_NUM_SAMPLES - (SSI_TX_BUFFER_NUM_SAMPLES << 1) + numSamplesToRunBeforeBeginningCapturing;
        numSamplesToRunBeforeBeginningCapturing = 0;
    }

    return Error::NONE;
}
```

#### B. Recording Completion (`SampleRecorder::endSyncedRecording`)

**Location**: `src/deluge/model/sample/sample_recorder.cpp:1102`

```cpp
void SampleRecorder::endSyncedRecording(int32_t buttonLatencyForTempolessRecording) {
    if (numSamplesCaptured) {
        // Calculate actual recording length with precise latency compensation
        int32_t numMoreSamplesTilEndLoopPoint =
            numSamplesExtraToCaptureAtEndSyncingWise - buttonLatencyForTempolessRecording;
        int32_t numMoreSamplesToCapture = numMoreSamplesTilEndLoopPoint;

        // Add post-recording margins if enabled
        if (recordingExtraMargins) {
            numMoreSamplesToCapture += kAudioClipMarginSizePostEnd;
        }

        // Calculate precise loop end point
        uint32_t loopEndPointSamples = numSamplesCaptured + numMoreSamplesTilEndLoopPoint;

        // Set final sample length with exact boundaries
        totalSampleLengthNowKnown(numSamplesCaptured + numMoreSamplesToCapture, loopEndPointSamples);

        // Check if recording should stop immediately
        if (numMoreSamplesToCapture <= 0) {
            if (numMoreSamplesToCapture < 0) {
                capturedTooMuch = true; // Handle over-capture
            }
            finishCapturing();
        } else {
            status = RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP;
        }
    } else {
        // No samples captured (threshold recording case)
        abort();
    }
}
```

#### C. Sample Length Finalization (`SampleRecorder::totalSampleLengthNowKnown`)

**Location**: `src/deluge/model/sample/sample_recorder.cpp:1142`

```cpp
void SampleRecorder::totalSampleLengthNowKnown(uint32_t totalLengthSamples, uint32_t loopEndPointSamples) {
    // Set final sample properties
    sample->lengthInSamples = totalLengthSamples;
    sample->audioDataLengthBytes = totalLengthSamples * sample->byteDepth * sample->numChannels;

    // Set loop end point for file metadata
    if (stemExport.writeLoopEndPos()) {
        sample->fileLoopEndSamples = stemExport.loopEndPointInSamplesForAudioFile;
    } else {
        sample->fileLoopEndSamples = loopEndPointSamples;
    }

    // Update cluster data if not yet written to storage
    if (firstUnwrittenClusterIndex == 0) {
        SampleCluster* firstSampleCluster = sample->clusters.getElement(0);
        Cluster* cluster = firstSampleCluster->cluster;

        audioDataLengthBytesAsWrittenToFile = sample->audioDataLengthBytes;
        loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples;
        updateDataLengthInFirstCluster(cluster);
    }
}
```

#### D. SampleHolder Position Setup (`SampleHolder::setAudioFile`)

**Location**: `src/deluge/model/sample/sample_holder.cpp:115`

```cpp
void SampleHolder::setAudioFile(AudioFile* newSample, bool reversed, bool manuallySelected,
                                int32_t clusterLoadInstruction) {
    AudioFileHolder::setAudioFile(newSample, reversed, manuallySelected, clusterLoadInstruction);

    if (audioFile) {
        uint32_t lengthInSamples = ((Sample*)audioFile)->lengthInSamples;

        if (manuallySelected) {
            // User-selected file: use full sample length
            startPos = 0;
            endPos = lengthInSamples;
        } else {
            // Recording result: ensure bounds are within sample limits
            startPos = std::min<uint64_t>(startPos, lengthInSamples);

            // Handle uninitialized or invalid end positions
            if (endPos == 0 || endPos == 9999999) {
                endPos = lengthInSamples;
            }

            // Ensure valid position relationship
            if (endPos <= startPos) {
                startPos = 0;
            }
        }

        // Setup sample processing and claim cluster reasons
        sampleBeenSet(reversed, manuallySelected);
        claimClusterReasons(reversed, clusterLoadInstruction);
    }
}
```

### 6. Recording Finalization (`AudioClip::finishLinearRecording`)

**Location**: `src/deluge/model/clip/audio_clip.cpp:187`

Final step that completes the overdub recording and integrates it into the clip.

```cpp
void AudioClip::finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingOverdub,
                                     int32_t buttonLatencyForTempolessRecord) {
    if (!recorder) {
        return; // Shouldn't happen, but safety check
    }

    // Check for recording errors
    if (recorder->status == RecorderStatus::ABORTED || recorder->reachedMaxFileSize || !recorder->numSamplesCaptured) {
        abortRecording();
        return;
    }

    // Create action for undo support
    Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
    if (!isUnfinishedAutoOverdub && action) {
        action->recordAudioClipSampleChange(this);
    }

    // Release recorder pointer
    recorder->pointerHeldElsewhere = false;

    // 1. End recording with precise timing
    recorder->endSyncedRecording(buttonLatencyForTempolessRecord);

    // Request UI update
    if (getRootUI()) {
        getRootUI()->clipNeedsReRendering(this);
    }

    // 2. Clear any existing content
    if (!isEmpty()) {
        clear(nullptr, modelStack, true, true);
    }

    // 3. Switch from monitor mode to playback mode
    auto ao = (AudioOutput*)output;
    if (ao->mode == AudioOutputMode::sampler) {
        ao->mode = AudioOutputMode::player;
    }

    // 4. Set up sample with proper start/end bounds
    originalLength = loopLength;
    sampleHolder.filePath.set(&recorder->sample->filePath);
    sampleHolder.setAudioFile(recorder->sample, sampleControls.isCurrentlyReversed(), true,
                              CLUSTER_DONT_LOAD); // This sets startPos=0, endPos=sample length

    // 5. Force UI re-render
    renderData.xScroll = -1;

    // 6. Apply recording margins if enabled
    if (recorder->recordingExtraMargins) {
        attack = kAudioClipDefaultAttackIfPreMargin;
    }

    // 7. Mark overdub as complete
    isUnfinishedAutoOverdub = false;

    // 8. Clean up recorder
    recorder = nullptr;

    // 9. Set clip name from file path
    name.set(sampleHolder.filePath.get());
}
```

**Key Features:**
- Handles recording errors gracefully
- Creates actions for undo support
- Switches from monitoring to playback mode
- Automatically sets optimal start/end positions
- Integrates with UI rendering system
- Applies recording margins when appropriate

## Timing and Synchronization

### Latency Compensation

The overdub system implements sophisticated latency compensation to ensure perfect timing alignment:

```cpp
// Button press latency compensation (external sources only)
numSamplesToRunBeforeBeginningCapturing -= buttonPressLatency;

// Audio system latency compensation
numSamplesToRunBeforeBeginningCapturing =
    (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) ? kAudioRecordLagCompensation : 0;

// End-of-recording latency compensation
int32_t numMoreSamplesTilEndLoopPoint =
    numSamplesExtraToCaptureAtEndSyncingWise - buttonLatencyForTempolessRecording;
```

### Sample-Accurate Positioning

The system ensures sample-accurate start and end positions through:

1. **Pre-recording Setup**: Calculates exact start position with latency compensation
2. **During Recording**: Tracks exact sample count with real-time monitoring
3. **Post-recording**: Applies final latency adjustments for precise end position
4. **Position Validation**: Ensures start/end positions are within valid sample bounds

### Musical Timing Integration

Overdubs integrate seamlessly with the Deluge's musical timing system:

- **Tick Synchronization**: Aligned with internal musical tick counter
- **Bar Boundary Alignment**: Respects musical bar boundaries when appropriate
- **Tempo Sync**: Supports both free-running and tempo-synchronized recording
- **Loop Length Optimization**: Automatically optimizes lengths for UI display

## Output Management

### Clone Output Behavior

```cpp
bool AudioClip::cloneOutput(ModelStackWithTimelineCounter* modelStack) {
    // Only clone if enabled
    if (!overdubsShouldCloneOutput) {
        return false;
    }

    // Create new AudioOutput
    AudioOutput* newOutput = modelStack->song->createNewAudioOutput();
    if (!newOutput) {
        return false;
    }

    // Clone properties from original
    newOutput->cloneFrom((AudioOutput*)output);
    newOutput->wasCreatedForAutoOverdub = true;

    // Switch to new output
    changeOutput(modelStack, newOutput);

    return true;
}
```

### Input Source Selection

The system intelligently selects input sources based on recording context:

| Recording Type | Input Source | Channels | Purpose |
|---------------|-------------|----------|---------|
| First Recording | External Input | 1 or 2 | Initial audio capture |
| Overdub Recording | SPECIFIC_OUTPUT | 2 | Internal mixing of existing content |
| Resampling | MIX or OUTPUT | 2 | Full mix capture |

## Error Handling

The overdub system includes comprehensive error handling:

### Memory Management Errors
```cpp
void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioClip));
if (!clipMemory) {
    display->displayError(Error::INSUFFICIENT_RAM);
    return nullptr;
}
```

### Recording Errors
```cpp
if (recorder->status == RecorderStatus::ABORTED || recorder->reachedMaxFileSize || !recorder->numSamplesCaptured) {
    abortRecording(); // Clean up and notify user
    return;
}
```

### Cleanup on Failure
```cpp
Error error = newClip->setOutput(modelStackNewClip, output, this);
if (error != Error::NONE) {
    newClip->~AudioClip();        // Proper destructor call
    delugeDealloc(clipMemory);    // Free allocated memory
    goto ramError;                // Unified error handling
}
```

## Key Functions Reference

| Function | Location | Purpose |
|----------|----------|---------|
| `Song::createPendingNextOverdubBelowClip` | `song.cpp:5445` | Main overdub creation entry point |
| `AudioClip::cloneAsNewOverdub` | `audio_clip.cpp:251` | Creates new clip for overdub |
| `Clip::setupForRecordingAsAutoOverdub` | `clip.cpp:108` | Configures clip for overdub recording |
| `AudioClip::beginLinearRecording` | `audio_clip.cpp:152` | Starts the recording process |
| `AudioClip::finishLinearRecording` | `audio_clip.cpp:187` | Completes recording and sets positions |
| `SampleRecorder::setup` | `sample_recorder.cpp:105` | Initializes audio recorder |
| `SampleRecorder::endSyncedRecording` | `sample_recorder.cpp:1102` | Handles recording termination |
| `SampleRecorder::totalSampleLengthNowKnown` | `sample_recorder.cpp:1142` | Sets final sample length |
| `SampleHolder::setAudioFile` | `sample_holder.cpp:115` | Sets final start/end positions |
| `AudioClip::cloneOutput` | `audio_clip.cpp:280` | Handles output cloning for overdubs |

## Configuration Options

### Recording Settings

- **`FlashStorage::audioClipRecordMargins`**: Enables pre/post recording margins
- **`kAudioRecordLagCompensation`**: Audio system latency compensation value
- **`kAudioClipDefaultAttackIfPreMargin`**: Default attack for margin recordings
- **`kAudioClipMarginSizePostEnd`**: Post-recording margin size

### Session Layout Impact

- **Rows Layout**: Always uses clone-based overdubs
- **Grid Layout**: Can use either clone-based or in-place based on settings
- **`overdubsShouldCloneOutput`**: Per-clip setting controlling output cloning

## Best Practices

### For Developers

1. **Always Check Memory Allocation**: Overdub creation can fail due to memory constraints
2. **Handle Latency Compensation**: Different input sources require different compensation
3. **Validate Position Bounds**: Always ensure start/end positions are within sample limits
4. **Clean Up on Errors**: Proper cleanup prevents memory leaks and UI inconsistencies
5. **Update UI State**: Overdub operations require UI updates for proper user feedback

### For Users

1. **Use Appropriate Layout**: Grid layout for simple overdubs, rows for complex layering
2. **Monitor Memory Usage**: Too many overdubs can exhaust available RAM
3. **Check Input Levels**: Proper input levels ensure good overdub quality
4. **Understand Timing**: Button press timing affects overdub synchronization

## Troubleshooting

### Common Issues

1. **"INSUFFICIENT_RAM" Error**: Too many active overdubs, reduce active clips
2. **Timing Misalignment**: Check button press latency compensation settings
3. **Silent Overdubs**: Verify input source and levels
4. **UI Not Updating**: Check if UI refresh calls are being made after overdub operations

### Debug Information

Key debug points for overdub issues:

```cpp
D_PRINTLN("Overdub creation: clipIndex=%d, nature=%d", clipIndex, (int)newOverdubNature);
D_PRINTLN("Recording setup: inputChannel=%d, numChannels=%d", (int)inputChannel, numChannels);
D_PRINTLN("Sample length: %u samples, startPos=%u, endPos=%u", lengthInSamples, startPos, endPos);
```

## Sample-Accurate Loop Boundary Management

### Overview

The Deluge maintains sample-accurate boundaries during looping through a sophisticated multi-layered system that ensures precise timing at the audio sample level (44.1 kHz resolution). This is critical for musical applications where even small timing deviations can be audible.

### Core Boundary Management System

#### 1. Playback Guide Hierarchy

The system uses a hierarchy of playback guides to manage different types of boundaries:

```cpp
// Base class for all playback boundary management
class SamplePlaybackGuide {
    int32_t startPlaybackAtByte;    // Start boundary in bytes
    int32_t endPlaybackAtByte;      // End boundary in bytes
    int32_t playDirection;          // 1 for forward, -1 for reverse
};

// Voice-specific boundaries (includes loop points)
class VoiceSamplePlaybackGuide : public SamplePlaybackGuide {
    int32_t loopStartPlaybackAtByte;  // Loop start boundary in bytes
    int32_t loopEndPlaybackAtByte;    // Loop end boundary in bytes
    bool noteOffReceived;             // Controls loop behavior
};
```

#### 2. Boundary Calculation Process

**Location**: `src/deluge/model/voice/voice_sample_playback_guide.cpp:30`

```cpp
void VoiceSamplePlaybackGuide::setupPlaybackBounds(bool reversed) {
    // Call base class to set up start/end boundaries
    SamplePlaybackGuide::setupPlaybackBounds(reversed);

    int32_t loopStartPlaybackAtSample = 0;
    int32_t loopEndPlaybackAtSample = 0;

    // Loop points are only obeyed if not in STRETCH mode
    if (!sequenceSyncLengthTicks) {
        // Get loop boundaries from sample holder, handling direction
        loopStartPlaybackAtSample = reversed ?
            ((SampleHolderForVoice*)audioFileHolder)->loopEndPos :
            ((SampleHolderForVoice*)audioFileHolder)->loopStartPos;
        loopEndPlaybackAtSample = reversed ?
            ((SampleHolderForVoice*)audioFileHolder)->loopStartPos :
            ((SampleHolderForVoice*)audioFileHolder)->loopEndPos;

        // Handle reversed playback boundary adjustments
        if (reversed) {
            if (loopStartPlaybackAtSample) loopStartPlaybackAtSample--;
            if (loopEndPlaybackAtSample) loopEndPlaybackAtSample--;
        }
    }

    // Convert sample positions to precise byte positions
    Sample* sample = (Sample*)audioFileHolder->audioFile;
    int32_t bytesPerSample = sample->numChannels * sample->byteDepth;

    if (loopStartPlaybackAtSample) {
        loopStartPlaybackAtByte = sample->audioDataStartPosBytes +
                                  loopStartPlaybackAtSample * bytesPerSample;
    } else {
        loopStartPlaybackAtByte = startPlaybackAtByte;
    }

    if (loopEndPlaybackAtSample) {
        loopEndPlaybackAtByte = sample->audioDataStartPosBytes +
                                loopEndPlaybackAtSample * bytesPerSample;
    }
}
```

#### 3. Loop Boundary Decision Logic

**Location**: `src/deluge/model/voice/voice_sample_playback_guide.cpp:87`

```cpp
int32_t VoiceSamplePlaybackGuide::getBytePosToEndOrLoopPlayback() {
    // This function determines the effective end boundary for playback
    if (shouldObeyLoopEndPointNow()) {
        return loopEndPlaybackAtByte;    // Use loop end for sustained notes
    } else {
        return SamplePlaybackGuide::getBytePosToEndOrLoopPlayback();  // Use sample end
    }
}

bool VoiceSamplePlaybackGuide::shouldObeyLoopEndPointNow() {
    return (loopEndPlaybackAtByte && !noteOffReceived);
}
```

### Real-Time Boundary Enforcement

#### 1. Cluster-Based Reading System

The Deluge reads audio data in clusters (typically 16KB chunks) and maintains sample-accurate boundaries within each cluster:

**Location**: `src/deluge/model/sample/sample_low_level_reader.cpp:397`

```cpp
bool SampleLowLevelReader::changeClusterIfNecessary(SamplePlaybackGuide* guide, Sample* sample,
                                                    bool loopingAtLowLevel, int32_t priorityRating) {
    while (true) {
        // Calculate how far we've overshot the boundary
        int32_t byteOvershoot = (int32_t)((uint32_t)currentPlayPos - (uint32_t)reassessmentLocation)
                                * guide->playDirection;

        if (byteOvershoot < 0) {
            break;  // Still within bounds
        }

        if (reassessmentAction == REASSESSMENT_ACTION_NEXT_CLUSTER) {
            // Move to next cluster for continuation
            bool success = moveOnToNextCluster(guide, sample, priorityRating);
            if (!success) return false;

        } else { // REASSESSMENT_ACTION_STOP_OR_LOOP
            unassignAllReasons(false);
            if (loopingAtLowLevel) {
                // SAMPLE-ACCURATE LOOP: Restart from beginning with exact overshoot compensation
                bool success = setupClusersForInitialPlay(guide, sample, byteOvershoot, true, priorityRating);
                if (!success) return false;
            } else {
                // Stop playback at exact boundary
                currentPlayPos = nullptr;
                return false;
            }
        }
    }
    return true;
}
```

#### 2. Reassessment Location Setup

**Location**: `src/deluge/model/sample/sample_low_level_reader.cpp:130`

```cpp
void SampleLowLevelReader::setupReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample) {
    int32_t bytesPerSample = (sample->byteDepth * sample->numChannels);
    int32_t currentClusterIndex = clusters[0]->clusterIndex;

    int32_t endPlaybackAtByte;
    int32_t finalClusterIndex = guide->getFinalClusterIndex(sample, shouldObeyMarkers(), &endPlaybackAtByte);

    // Is this the final cluster before boundary?
    if (currentClusterIndex == finalClusterIndex) {
        // Calculate exact byte position to stop at within cluster
        int32_t bytePosWithinClusterToStopAt = endPlaybackAtByte & (Cluster::size - 1);

        // Handle forward/reverse playback alignment
        if (guide->playDirection == 1) {
            if (bytePosWithinClusterToStopAt == 0) {
                bytePosWithinClusterToStopAt = Cluster::size;
            }
        } else {
            if (bytePosWithinClusterToStopAt > Cluster::size - bytesPerSample) {
                bytePosWithinClusterToStopAt -= Cluster::size;
            }
        }

        // Set precise reassessment boundary
        reassessmentLocation = &clusters[0]->data[bytePosWithinClusterToStopAt];
        reassessmentAction = REASSESSMENT_ACTION_STOP_OR_LOOP;

    } else {
        // Set up for next cluster transition
        reassessmentAction = REASSESSMENT_ACTION_NEXT_CLUSTER;
        // ... calculate cluster transition boundary with sample alignment
    }
}
```

### Sample-Level Precision Mechanisms

#### 1. Byte-Level Alignment

All boundaries are calculated and maintained at the byte level, ensuring sample-perfect alignment:

```cpp
// Every boundary calculation considers the exact sample format
int32_t bytesPerSample = sample->numChannels * sample->byteDepth;  // Typically 6 bytes (24-bit stereo)

// All positions are aligned to sample boundaries
int32_t alignedBytePosition = (rawBytePosition / bytesPerSample) * bytesPerSample;
```

#### 2. Overshoot Compensation

When a loop boundary is crossed, the system calculates the exact overshoot and compensates:

```cpp
// In setupClusersForInitialPlay when looping:
uint32_t startPlaybackAtByte = guide->getBytePosToStartPlayback(justLooped);
startPlaybackAtByte += byteOvershoot * guide->playDirection;  // Exact compensation
```

#### 3. Interpolation Buffer Management

For interpolated playback (resampling), boundary precision is maintained through careful buffer management:

**Location**: `src/deluge/model/sample/sample_low_level_reader.cpp:500`

```cpp
bool SampleLowLevelReader::fillInterpolationBufferForward(SamplePlaybackGuide* guide, Sample* sample,
                                                          int32_t interpolationBufferSize, bool loopingAtLowLevel,
                                                          int32_t startI, int32_t priorityRating) {
    // Fill interpolation buffer while respecting exact boundaries
    for (int32_t i = numSpacesToFill - 1; i >= 0; i--) {
        bool stillGoing = changeClusterIfNecessary(guide, sample, loopingAtLowLevel, priorityRating);
        if (!stillGoing) {
            // Hit boundary - fill with zeros to maintain buffer integrity
            interpolator_.buffer_l[i] = 0;
            interpolator_.buffer_r[i] = 0;
            continue;
        }

        // Read sample data respecting exact boundary positions
        interpolator_.buffer_l[i] = *(int16_t*)(currentPlayPos + 2);
        if (sample->numChannels == 2) {
            interpolator_.buffer_r[i] = *(int16_t*)(currentPlayPos + 2 + sample->byteDepth);
        }

        // Advance exactly one sample
        currentPlayPos += sample->numChannels * sample->byteDepth * guide->playDirection;
    }
    return true;
}
```

### Cache-Based Boundary Management

For performance-critical scenarios, the system uses cached sample data with preserved boundary accuracy:

**Location**: `src/deluge/model/voice/voice_sample.cpp:88`

```cpp
void VoiceSample::setupCacheLoopPoints(SamplePlaybackGuide* guide, Sample* sample, LoopType loopingType) {
    uint8_t bytesPerSample = sample->numChannels * sample->byteDepth;
    uint64_t combinedIncrement = ((uint64_t)(uint32_t)cache->phaseIncrement *
                                 (uint32_t)cache->timeStretchRatio) >> 24;

    if (loopingType != LoopType::NONE) {
        // Calculate exact loop boundaries in cache coordinates

        // Loop start point conversion
        int32_t loopStartPointBytesRaw = guide->getLoopStartPlaybackAtByte();
        uint32_t loopStartPointBytes = (guide->playDirection == 1)
            ? loopStartPointBytesRaw - sample->audioDataStartPosBytes
            : sample->audioDataStartPosBytes + sample->audioDataLengthBytes - loopStartPointBytesRaw - 1;
        int32_t loopStartPointSamples = loopStartPointBytes / bytesPerSample - cache->skipSamplesAtStart;

        // Loop end point conversion with sample-accurate rounding
        int32_t loopEndPointBytesRaw = guide->getLoopEndPlaybackAtByte();
        int32_t loopEndPointBytes = (guide->playDirection == 1)
            ? loopEndPointBytesRaw - sample->audioDataStartPosBytes
            : sample->audioDataStartPosBytes + sample->audioDataLengthBytes - loopEndPointBytesRaw - 1;
        int32_t loopEndPointSamples = loopEndPointBytes / bytesPerSample - cache->skipSamplesAtStart;

        // Convert to cache sample positions with proper rounding
        uint64_t loopEndPointSamplesBig = (uint64_t)loopEndPointSamples << 24;
        uint32_t loopEndPointCombinedIncrements =
            (loopEndPointSamplesBig + (combinedIncrement >> 1)) / combinedIncrement;  // Rounds to nearest
        cacheLoopEndPointBytes = loopEndPointCombinedIncrements * kCacheByteDepth * sample->numChannels;

        // Calculate loop length in cache coordinates
        uint32_t loopLengthSamples = loopEndPointSamples - loopStartPointSamples;
        uint64_t loopLengthSamplesBig = (uint64_t)loopLengthSamples << 24;
        uint32_t loopLengthCombinedIncrements =
            (loopLengthSamplesBig + (combinedIncrement >> 1)) / combinedIncrement;
        cacheLoopLengthBytes = loopLengthCombinedIncrements * kCacheByteDepth * sample->numChannels;
    }
}
```

### TimeStretcher Boundary Handling

When using time-stretching for tempo sync, boundaries are maintained through the time-stretcher interface:

**Location**: `src/deluge/model/voice/voice_sample.cpp:1622`

```cpp
bool VoiceSample::sampleZoneChanged(SamplePlaybackGuide* voiceSource, Sample* sample, bool reversed,
                                   MarkerType markerType, LoopType loopingType, int32_t priorityRating,
                                   bool forAudioClip) {
    if (timeStretcher) {
        // Check for loop boundary overshoot in time-stretched playback
        if (((VoiceSamplePlaybackGuide*)voiceSource)->shouldObeyLoopEndPointNow()) {
            int32_t bytePos = timeStretcher->getSamplePos(voiceSource->playDirection)
                              * (sample->byteDepth * sample->numChannels)
                              + sample->audioDataStartPosBytes;

            int32_t overshootBytes = (bytePos - ((VoiceSamplePlaybackGuide*)voiceSource)->loopEndPlaybackAtByte)
                                     * voiceSource->playDirection;

            if (overshootBytes >= 0) {
                // SAMPLE-ACCURATE LOOP in time-stretched mode
                int32_t overshootSamples = overshootBytes / (sample->byteDepth * sample->numChannels);
                int32_t loopStartSample = (((VoiceSamplePlaybackGuide*)voiceSource)->loopStartPlaybackAtByte
                                          - sample->audioDataStartPosBytes) / (sample->byteDepth * sample->numChannels);

                // Calculate exact loop restart position with overshoot compensation
                int32_t newSamplePos = loopStartSample + overshootSamples;
                timeStretcher->setupForLoopStart((uint32_t)newSamplePos, sample->numChannels, sample->byteDepth);
            }
        }
    }

    return true;
}
```

### Boundary Precision Guarantees

The system provides several levels of precision guarantees:

#### 1. **Sample-Level Accuracy (±1 sample)**
- All loop boundaries are aligned to exact sample boundaries
- No sub-sample timing errors accumulate over time
- Exact compensation for buffer overruns/underruns

#### 2. **Byte-Level Alignment**
- All memory accesses respect sample format boundaries
- No partial sample reads that could cause artifacts
- Proper alignment for SIMD operations

#### 3. **Musical Timing Integration**
- Loop boundaries synchronize with musical timing system
- Maintains accuracy even with tempo changes
- Supports both free-running and tempo-locked playback

#### 4. **Multi-Rate Handling**
- Consistent boundary handling across different sample rates
- Proper boundary scaling for resampled content
- Maintains precision through complex signal processing chains

This multi-layered approach ensures that loop boundaries remain sample-accurate throughout all playback scenarios, from simple loops to complex time-stretched, cached, and interpolated playback modes. The system's design prioritizes timing precision while maintaining real-time performance requirements.

## Conclusion

The Deluge overdub implementation is a sophisticated system that provides:

- **Flexible Recording Options**: Both clone-based and in-place overdubbing
- **Precise Timing**: Sample-accurate positioning with comprehensive latency compensation
- **Robust Error Handling**: Graceful failure recovery and memory management
- **Musical Integration**: Seamless integration with the Deluge's timing and UI systems
- **Performance Optimization**: Efficient memory usage and real-time audio processing

This implementation enables musicians to create complex layered compositions with professional timing accuracy while maintaining the responsive real-time performance that the Deluge is known for.
