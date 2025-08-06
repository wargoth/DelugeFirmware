# Deluge Firmware Looping Implementation Guide

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Core Classes](#core-classes)
4. [Implementation Details](#implementation-details)
5. [State Management](#state-management)
6. [Track Type Specifics](#track-type-specifics)
7. [Row Looping](#row-looping)
8. [Recording Modes](#recording-modes)
9. [Community Features](#community-features)
10. [Performance Considerations](#performance-considerations)
11. [Code Examples](#code-examples)

## Overview

The Deluge firmware implements a sophisticated multi-level looping system that operates across several hierarchical layers:

- **Session-Level**: Global timing and clip coordination
- **Clip-Level**: Individual clip timing and boundaries
- **Row-Level**: Independent note row looping within clips
- **Grid-Level**: Enhanced community features for real-time looping

This system supports complex scenarios including independent row timing, real-time overdubbing, and seamless clip transitions while maintaining strict real-time performance requirements.

## Architecture

### Hierarchical Looping Structure

```
Session (Global Timing)
├── Clip (Individual Loop Boundaries)
│   ├── AudioClip (Sample-based looping)
│   └── InstrumentClip (Note-based looping)
│       └── NoteRow (Independent row timing)
└── Grid View (Enhanced UI workflows)
```

### Key Design Principles

1. **Deterministic Timing**: All loop calculations are predictable and bounded
2. **Real-Time Safety**: No memory allocation or blocking operations in audio thread
3. **Hierarchical Independence**: Each level can override timing from levels above
4. **State Consistency**: Loop states remain coherent across mode changes

## Core Classes

### 1. Clip (Base Class)

**Location**: `src/deluge/model/clip/clip.h`, `src/deluge/model/clip/clip.cpp`

The foundational looping implementation that all clip types inherit from.

#### Key Properties
```cpp
class Clip {
    uint32_t loopLength;        // Total loop duration in ticks
    uint32_t currentPos;        // Current playback position
    bool activelyPlaying;       // Whether clip is currently active
    TimelineCounter interface;  // Timing calculations
};
```

#### Core Methods
- `processCurrentPos()`: Advances playback position and handles wrap-around
- `posReachedEnd()`: Detects loop boundary crossings
- `willClipContinuePlayingAtEnd()`: Determines clip continuation behavior
- `incrementPos()`: Updates position with bounds checking

### 2. SessionView

**Location**: `src/deluge/gui/views/session_view.cpp`

Manages the user interface for looping operations, especially in grid view mode.

#### Grid View Integration
- Handles pad interactions for clip launching
- Manages Create+Record workflow for instant clip creation
- Provides visual feedback for loop states
- Implements LOOP/LAYERING LOOP command handling

### 3. NoteRow

**Location**: `src/deluge/model/note/note_row.h`

Enables independent timing for individual instrument rows within clips.

#### Independent Looping Features
```cpp
class NoteRow {
    uint32_t loopLengthIfIndependent;  // Custom loop length
    bool hasIndependentPlayPos;        // Independent timing flag
    uint32_t lastProcessedPos;         // Position tracking
};
```

### 4. Session (Playback Mode)

**Location**: `src/deluge/playback/mode/session.h`

Coordinates global timing and handles clip synchronization in session mode.

#### Responsibilities
- Global playback state management
- Clip launch scheduling and quantization
- Section-based looping in arranger view
- Cross-clip timing synchronization

## Implementation Details

### Position Tracking System

The core of all looping operations is the position tracking system:

```cpp
void Clip::processCurrentPos(ModelStack* modelStack, uint32_t ticksToProcess) {
    uint32_t oldPos = currentPos;
    currentPos += ticksToProcess;

    // Handle loop boundary crossing
    if (currentPos >= loopLength) {
        handleLoopWrapAround(modelStack, oldPos);
        currentPos = currentPos % loopLength;
    }

    // Process events in the traversed region
    processEventsInRange(modelStack, oldPos, currentPos);
}
```

### Loop Boundary Detection

```cpp
bool Clip::posReachedEnd(ModelStack* modelStack, uint32_t pos) {
    if (pos >= loopLength) {
        onLoopEnd(modelStack);
        return true;
    }
    return false;
}
```

### Wrap-Around Handling

Critical for maintaining seamless playback across loop boundaries:

1. **Pre-wrap processing**: Handle events before boundary
2. **Boundary event**: Trigger loop-specific behaviors
3. **Post-wrap processing**: Continue from loop start
4. **State preservation**: Maintain continuity for ongoing notes/automation

#### Arrangement Loop Wrap-Around Implementation

The arrangement loop system handles positions beyond the loop end through sophisticated offset calculation:

```cpp
int32_t Arrangement::checkForLoopAndGetNewPosition(int32_t currentPos) {
    if (loop_.shouldLoopAtPosition(currentPos)) {
        // Calculate how far beyond the loop end we are
        int32_t loopLength = loop_.getEnd() - loop_.getStart();
        int32_t beyondEnd = currentPos - loop_.getEnd();

        // Handle wrap-around: calculate new offset after start
        int32_t offsetWithinLoop = beyondEnd % loopLength;

        return loop_.getStart() + offsetWithinLoop;
    }
    return currentPos; // No loop, position unchanged
}
```

**Key Behaviors:**

- **Beyond-End Detection**: Uses `>=` comparison in `shouldLoopAtPosition()` to catch any position at or beyond loop end
- **Offset Calculation**: Computes `beyondEnd % loopLength` to find the correct offset within the loop
- **Timing Preservation**: Maintains exact timing relationships even for positions far beyond loop boundaries
- **Boundary Events**: The arrangement system's event scheduling ensures all intermediate events are triggered

**Example Scenarios:**

| Scenario | Loop Range | Current Pos | Calculated Result | Explanation |
|----------|------------|-------------|-------------------|-------------|
| Normal Loop | 100-200 | 200 | 100 | Exactly at end → loop start |
| Small Overshoot | 100-200 | 205 | 105 | 5 ticks beyond → start + 5 |
| Large Overshoot | 100-200 | 350 | 150 | 150 beyond end → 150 % 100 = 50, so start + 50 |
| Multi-Loop | 100-200 | 500 | 100 | 400 beyond = 4 complete loops → back to start |

**Comparison with Session View:**
The session view uses similar logic for clip looping:
```cpp
int32_t whichRepeat = (uint32_t)livePos / (uint32_t)clip->loopLength;
livePos -= whichRepeat * clip->loopLength;
```

Both approaches achieve the same result through modulo arithmetic, ensuring consistent behavior across arrangement and session modes.

## State Management

### Global States

| State | Location | Purpose |
|-------|----------|---------|
| `playbackMode` | PlaybackHandler | Session vs Arrangement mode |
| `isPlaying` | Session | Global playback state |
| `currentSong` | Song | Active song context |
| `recordingMode` | AudioEngine | Current recording state |

### Clip-Level States

| State | Purpose | Edge Cases |
|-------|---------|------------|
| `activelyPlaying` | Clip activity | Launch scheduling, mute handling |
| `launchStyle` | Timing behavior | Immediate vs quantized launch |
| `clipMode` | Loop behavior | INFINITE, FILL, ONCE modes |
| `currentPos` | Playback position | Wrap-around, seek operations |

### Row-Level States

Independent note rows maintain their own state:

| State | Purpose | Conflicts |
|-------|---------|-----------|
| `hasIndependentPlayPos` | Independent timing | Clip-level sync issues |
| `loopLengthIfIndependent` | Custom length | Memory allocation |
| `lastProcessedPos` | Position tracking | Timing drift |

## Track Type Specifics

### Audio Clips

Audio clips have unique looping characteristics due to their sample-based nature:

#### Key Features
- Fixed-length audio samples with optional loop points
- Real-time overdubbing and layering capabilities
- Three monitoring modes: Player, Sampler, Looper

#### Edge Cases

**1. Sample Length vs Loop Length Mismatch**
```cpp
void AudioClip::handleLengthMismatch() {
    if (audioSampleLength < clipLoopLength) {
        // Pad with silence
        fillSilenceGap(audioSampleLength, clipLoopLength);
    } else if (audioSampleLength > clipLoopLength) {
        // Truncate or create sample loop points
        setSampleLoopPoints(0, clipLoopLength);
    }
}
```

**2. Real-Time Overdubbing**
```cpp
void AudioClip::processOverdubRecording(int32_t* inputBuffer, int32_t numSamples) {
    int32_t loopPos = getCurrentLoopPosition();

    for (int32_t i = 0; i < numSamples; i++) {
        // Mix new input with existing content
        audioData[loopPos] = mixAudioSamples(audioData[loopPos], inputBuffer[i]);
        loopPos = (loopPos + 1) % loopLength;
    }
}
```

**3. Loop Point Conflicts**
When sample-level loop points don't align with clip boundaries, priority is given to clip-level timing with sample loops disabled.

### Instrument Clips (Synth/MIDI/CV)

Note-based clips have different looping considerations:

#### Key Features
- Event-based sequencing with velocity, probability, iteration
- MPE expression data handling across boundaries
- Independent row looping support

#### Edge Cases

**1. Long Notes Across Boundaries**
```cpp
void InstrumentClip::handleNoteAcrossLoopBoundary(Note* note) {
    if (note->getLength() + note->pos > loopLength) {
        if (settings.catchNotes) {
            // Create continuation note at loop start
            Note* continuation = createContinuationNote(note);
            insertNoteAtPosition(continuation, 0);
        } else {
            // Truncate at boundary
            note->setLength(loopLength - note->pos);
        }
    }
}
```

**2. MPE Data Continuity**
Pitch bend, pressure, and timbre data must be preserved across loop boundaries:

```cpp
void InstrumentClip::preserveMPEAcrossLoop(ModelStack* modelStack) {
    // Capture current MPE state before loop
    MPEState currentState = captureMPEState(modelStack);

    // Apply state at loop start
    applyMPEStateAtPosition(modelStack, currentState, 0);
}
```

**3. Probability and Iteration Conflicts**
Complex interactions between note probability, iteration counts, and fill modes require careful state management.

### Kit Clips

Drum kits have the most complex looping scenarios:

#### Key Features
- Per-row independent sounds and timing
- Voice allocation across multiple drum sounds
- Cross-row dependencies (sidechaining, etc.)

#### Edge Cases

**1. Independent Row Timing**
```cpp
void KitClip::processIndependentRows(ModelStack* modelStack, uint32_t ticksToProcess) {
    for (int32_t rowIndex = 0; rowIndex < numNoteRows; rowIndex++) {
        NoteRow* row = getNoteRow(rowIndex);
        if (row->hasIndependentPlayPos) {
            // Process with row-specific timing
            row->processWithIndependentTiming(modelStack, ticksToProcess);
        } else {
            // Use clip-level timing
            row->processWithClipTiming(modelStack, ticksToProcess);
        }
    }
}
```

**2. Voice Allocation Conflicts**
When multiple drum sounds trigger simultaneously at loop boundaries, voice stealing algorithms must prioritize based on importance and timing.

**3. Sidechain Dependencies**
Compression sidechaining between kit rows can create timing dependencies that affect loop behavior.

## Row Looping

### Independent Row Implementation

Note rows can have their own loop lengths independent of the parent clip:

```cpp
class NoteRow {
private:
    uint32_t loopLengthIfIndependent;
    bool hasIndependentPlayPos;

public:
    void enableIndependentLooping(uint32_t length) {
        loopLengthIfIndependent = length;
        hasIndependentPlayPos = true;
    }

    uint32_t getEffectiveLoopLength(ModelStack* modelStack) {
        return hasIndependentPlayPos ?
               loopLengthIfIndependent :
               modelStack->getClip()->loopLength;
    }
};
```

### Wrap Editing Support

Wrap editing allows extending individual rows beyond the main clip:

```cpp
void NoteRow::extendForWrapEditing(uint32_t newLength) {
    if (newLength > parentClip->loopLength) {
        enableIndependentLooping(newLength);
        expandNoteDataStructures(newLength);
    }
}
```

### Synchronization Challenges

Independent row timing creates synchronization challenges:

1. **Phase Drift**: Rows may drift out of sync over time
2. **Memory Usage**: Each row requires separate position tracking
3. **UI Complexity**: Visual representation of multiple loop lengths

## Recording Modes

### Normal Recording

Standard recording behavior with loop awareness:

```cpp
void Clip::handleNormalRecording(ModelStack* modelStack) {
    if (recordingScheduledToStart) {
        startRecording(modelStack);
    }

    if (isRecording()) {
        recordInput(modelStack);

        // Check for loop boundary
        if (shouldStopAtLoopEnd()) {
            stopRecording(modelStack);
        }
    }
}
```

### Overdub Recording (Audio)

Real-time mixing with existing loop content:

```cpp
void AudioClip::processOverdubMode(int32_t* inputBuffer, int32_t numSamples) {
    int32_t writePos = getCurrentWritePosition();

    for (int32_t i = 0; i < numSamples; i++) {
        // Non-destructive mixing
        float existing = audioData[writePos];
        float input = convertToFloat(inputBuffer[i]);
        float mixed = mixAudioSignals(existing, input, overdubAmount);

        audioData[writePos] = convertToInt32(mixed);
        writePos = (writePos + 1) % loopLength;
    }
}
```

### Layering Recording

Multiple layer support for complex overdubbing:

```cpp
class AudioClip {
    struct Layer {
        int32_t* audioData;
        float volume;
        bool muted;
    };

    std::vector<Layer> layers;

public:
    void addNewLayer() {
        Layer newLayer;
        newLayer.audioData = allocateAudioBuffer(loopLength);
        newLayer.volume = 1.0f;
        newLayer.muted = false;
        layers.push_back(newLayer);
    }
};
```

### Punch Recording

Recording only within specified regions:

```cpp
void Clip::processPunchRecording(ModelStack* modelStack) {
    uint32_t currentPos = getCurrentPosition();

    if (currentPos >= punchInPos && currentPos < punchOutPos) {
        recordInput(modelStack);
    } else if (wasRecording && currentPos >= punchOutPos) {
        stopRecording(modelStack);
    }
}
```

## Community Features

### Grid View Enhanced Looping

The community firmware adds sophisticated grid view features:

#### Create + Record Workflow

```cpp
bool SessionView::handleCreateAndRecord(uint8_t yDisplay, int32_t xDisplay) {
    if (settings.gridCreateRecordEnabled && isRecording()) {
        Clip* newClip = createClipInTrack(yDisplay, xDisplay);
        if (newClip) {
            armClipForRecording(newClip);
            newClip->scheduleRecordingStart(getNextBarPosition());
            return true;
        }
    }
    return false;
}
```

#### LOOP/LAYERING LOOP Commands

Global MIDI commands for enhanced looping:

- **LOOP**: EDP-style loop extension while recording
- **LAYERING LOOP**: Pedal-style overdubbing within current length
- **Grid Loop Pads**: Visual interface via red/magenta audition pads

```cpp
void SessionView::handleLoopCommand(LoopCommand command) {
    Clip* activeClip = getCurrentActiveClip();
    if (!activeClip) return;

    switch (command) {
        case LoopCommand::LOOP:
            activeClip->extendLoopWhileRecording();
            break;
        case LoopCommand::LAYERING_LOOP:
            activeClip->startLayeringMode();
            break;
    }
}
```

#### Enhanced Audio Clip Modes

Three monitoring modes for different workflows:

1. **Player Mode**: Static playback, monitoring disabled
2. **Sampler Mode**: Monitoring during recording only
3. **Looper Mode**: Continuous monitoring with real-time overdub

### Advanced Launch Modes

Community firmware adds sophisticated clip launch behaviors:

#### Fill Clips
Automatically timed to end at loop boundaries:

```cpp
void FillClip::calculateLaunchTiming(uint32_t currentPos, uint32_t loopLength) {
    uint32_t remainingTime = loopLength - (currentPos % loopLength);

    if (clipLength <= remainingTime) {
        // Launch now to finish at loop end
        scheduleLaunch(currentPos);
    } else {
        // Launch at calculated earlier position
        uint32_t launchPos = currentPos + remainingTime - clipLength;
        scheduleLaunch(launchPos);
    }
}
```

#### Once Clips
Play exactly once then stop:

```cpp
void OnceClip::processPlayback(ModelStack* modelStack) {
    if (!hasPlayedOnce) {
        // Normal playback
        Clip::processPlayback(modelStack);

        if (currentPos >= loopLength) {
            hasPlayedOnce = true;
            stopPlaying(modelStack);
        }
    }
}
```

## Performance Considerations

### Memory Management

Efficient memory usage is critical for real-time performance:

```cpp
// Pre-allocated buffers for loop processing
class LoopBufferManager {
    static constexpr int32_t MAX_LOOP_BUFFER_SIZE = 48000 * 60; // 1 minute at 48kHz
    int32_t* audioBuffer;
    uint32_t bufferSize;

public:
    void allocateBuffer(uint32_t requiredSize) {
        if (requiredSize <= MAX_LOOP_BUFFER_SIZE) {
            bufferSize = requiredSize;
        } else {
            // Handle oversized loops differently
            handleOversizedLoop(requiredSize);
        }
    }
};
```

### Real-Time Constraints

All loop operations must complete within audio callback deadlines:

```cpp
// Optimized loop position calculation
inline uint32_t fastLoopPosition(uint32_t pos, uint32_t loopLength) {
    // Use bit operations for power-of-2 loop lengths
    if ((loopLength & (loopLength - 1)) == 0) {
        return pos & (loopLength - 1);
    }
    return pos % loopLength;
}
```

### CPU Optimization

Minimize processing overhead in critical paths:

1. **Branch Prediction**: Structure conditionals for common cases
2. **Cache Locality**: Keep related data structures close in memory
3. **SIMD Usage**: Vectorize audio processing where possible
4. **Lock-Free Algorithms**: Avoid synchronization in audio thread

### Memory Pool Management

```cpp
class AudioMemoryPool {
    struct Block {
        void* memory;
        size_t size;
        bool inUse;
    };

    std::vector<Block> blocks;

public:
    void* allocateForLoop(size_t size) {
        // Find suitable pre-allocated block
        for (auto& block : blocks) {
            if (!block.inUse && block.size >= size) {
                block.inUse = true;
                return block.memory;
            }
        }
        return nullptr; // Allocation failure
    }
};
```

## Code Examples

### Complete Loop Processing Example

```cpp
void AudioClip::processLoopedPlayback(int32_t* outputBuffer, int32_t numSamples) {
    int32_t samplesProcessed = 0;
    uint32_t currentSamplePos = audioFileCurrentPos;

    while (samplesProcessed < numSamples) {
        // Calculate samples until next boundary
        uint32_t samplesToNextBoundary = std::min(
            (uint32_t)(numSamples - samplesProcessed),
            audioFileLength - currentSamplePos
        );

        // Process samples before boundary
        copySamples(
            outputBuffer + samplesProcessed,
            audioData + currentSamplePos,
            samplesToNextBoundary
        );

        samplesProcessed += samplesToNextBoundary;
        currentSamplePos += samplesToNextBoundary;

        // Handle wrap-around
        if (currentSamplePos >= audioFileLength) {
            currentSamplePos = 0;
            onLoopWrapAround();
        }
    }

    audioFileCurrentPos = currentSamplePos;
}
```

### Multi-Layer Overdub Implementation

```cpp
void AudioClip::processMultiLayerOverdub(int32_t* inputBuffer, int32_t numSamples) {
    if (!layers.empty()) {
        int32_t writePos = getCurrentWritePosition();
        Layer& activeLayer = layers.back();

        for (int32_t i = 0; i < numSamples; i++) {
            // Record to active layer
            activeLayer.audioData[writePos] = inputBuffer[i];

            // Mix all layers for output
            int32_t mixedOutput = 0;
            for (const auto& layer : layers) {
                if (!layer.muted) {
                    mixedOutput += (int32_t)(layer.audioData[writePos] * layer.volume);
                }
            }

            outputBuffer[i] = mixedOutput;
            writePos = (writePos + 1) % loopLength;
        }
    }
}
```

### Independent Row Timing

```cpp
void NoteRow::processIndependentTiming(ModelStack* modelStack, uint32_t ticksToProcess) {
    if (!hasIndependentPlayPos) {
        return; // Use clip timing
    }

    uint32_t effectiveLoopLength = loopLengthIfIndependent;
    uint32_t oldPos = independentPos;

    independentPos += ticksToProcess;

    // Handle independent wrap-around
    while (independentPos >= effectiveLoopLength) {
        // Process notes at wrap boundary
        processNotesInRange(modelStack, oldPos, effectiveLoopLength);

        // Wrap to beginning
        independentPos -= effectiveLoopLength;
        oldPos = 0;

        // Trigger row-specific loop events
        onIndependentLoopWrap(modelStack);
    }

    // Process remaining notes
    if (oldPos < independentPos) {
        processNotesInRange(modelStack, oldPos, independentPos);
    }
}
```

---

## Conclusion

The Deluge's looping implementation demonstrates sophisticated engineering that balances flexibility with real-time performance requirements. The multi-level architecture allows for complex musical scenarios while maintaining deterministic timing and efficient resource usage.

Key strengths of the implementation:

1. **Hierarchical Design**: Clean separation of concerns across loop levels
2. **Real-Time Safety**: No dynamic allocation in audio processing paths
3. **Extensibility**: Community features integrate seamlessly with core architecture
4. **Performance**: Optimized algorithms for critical timing operations

This guide provides the foundation for understanding, maintaining, and extending the looping functionality in the Deluge firmware.

---

*This guide documents the looping implementation as of Deluge Community Firmware v1.3. For the latest updates, refer to the community documentation at https://delugecommunity.com/*
