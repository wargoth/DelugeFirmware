# Arrangement View Loop-Based Overdubbing Feature Specification

## Overview

This feature specification describes a new recording workflow for the Deluge's arrangement view that automatically creates in-place overdubs when recording with an active arrangement loop. The system leverages the existing in-place overdub infrastructure and arrangement loop boundaries to provide an intuitive, EDP-style loop-based recording experience.

## Feature Requirements

### Core Functionality

**When recording in arrangement view with an active loop:**

1. **Loop Trigger Overdub Creation**: When the playhead hits the loop end boundary during recording, automatically create a new in-place overdub for the recording track
2. **Mute Button Recording Termination**: If user presses the mute button, finish recording at the end of the current loop cycle
3. **RECORD Button Recording Termination**: If user presses the RECORD button to stop recording immediately
4. **Incomplete Overdub Handling**: If recording stops (via RECORD button) before completing at least one full loop cycle and there's already one complete overdub, discard the incomplete overdub

### Detailed Behavior Specification

#### 1. Loop-Triggered Overdub Creation

**Trigger Conditions:**
- Arrangement mode is active (`currentPlaybackMode == &arrangement`)
- An arrangement loop is active (`currentSong->shouldLoopArrangement()` returns true)
- Recording is active (`playbackHandler.recording != RecordingMode::OFF`)
- Playhead reaches loop end position (`arrangement.checkAndHandleArrangerLoop()` triggers loop-back)
- At least one audio track is recording

**Behavior:**
```cpp
// Conceptual flow in arrangement.cpp actionSwungTick()
if (recording && playheadReachesLoopEnd && hasRecordingAudioTracks) {
    for (auto& track : recordingAudioTracks) {
        track->createInPlaceOverdub();
    }
}
```

**Implementation Points:**
- Hook into existing loop-back logic in `Arrangement::checkAndHandleArrangerLoop()`
- Use existing in-place overdub system (`AudioClip::setupOverdubInPlace()`)
- Maintain arrangement loop timing precision with existing `ArrangementLoop` boundary calculations
- Ensure sample-accurate overdub boundaries using existing `SamplePlaybackGuide` infrastructure
- Apply proper latency compensation using existing `SampleRecorder` timing systems

#### 2. Mute Button Termination

**Trigger Conditions:**
- User presses mute button on any recording track during loop overdub session

**Behavior:**
- Set flag to terminate recording at next loop end boundary for the track
- **Visual Feedback**: Mute button LED starts blinking to indicate pending termination
- Complete current loop cycle before stopping
- Apply normal recording completion process
- Preserve all completed overdubs

**Cancellation Behavior:**
- User can cancel termination intent by pressing the mute button again
- **Visual Feedback**: Mute button LED stops blinking and returns to normal recording state
- Recording continues with loop overdubbing as before

**Implementation Points:**
- Extend existing mute button handlers in `ArrangerView::handleStatusPadAction()`
- Add loop-boundary-aware recording termination with cancellation logic
- Implement blinking LED state management for pending termination
- Track termination intent state per recording track
- Reuse existing `AudioClip::finishLinearRecording()` logic

#### 3. RECORD Button Termination

**Trigger Conditions:**
- User presses RECORD button during loop overdub session

**Behavior:**
- Stop recording immediately (do not wait for loop boundary)
- Apply incomplete overdub handling logic if conditions are met
- Preserve completed overdubs, potentially discard incomplete one

**Implementation Points:**
- Extend existing RECORD button handlers
- Add immediate recording termination logic
- Integrate with incomplete overdub discard system

#### 4. Incomplete Overdub Handling

**Conditions for Overdub Discard:**
- Recording stops before completing one full loop cycle (by pressing RECORD button)
- AND at least one complete overdub already exists for the track
- Triggered by user action (pressing RECORD button to stop recording, not automatic loop cycling)

**Behavior:**
```cpp
// Conceptual logic in recording termination
if (recordingStoppedEarly && hasExistingOverdubs && !completedAtLeastOneLoop) {
    discardIncompleteOverdub();
    revertToLastCompleteState();
}
```

**Implementation Points:**
- Track loop completion state per recording track
- Extend existing overdub abort logic (`SampleRecorder::abort()`)
- Maintain undo/redo compatibility

#### 5. Loop Boundary Management During Recording

**Boundary Change Restrictions:**
- **Block loop boundary modifications** when tracks are actively recording overdubs
- **Allow loop deactivation** - tracks continue recording as normal arrangement recording
- **Preserve recording state** - no data loss when loop is deactivated during recording

**User Experience:**
- Loop on/off toggle remains available during recording
- Seamless transition from loop overdub mode to normal recording mode

**Implementation Strategy:**
```cpp
// In ArrangerView loop boundary change handlers
if (hasActiveLoopOverdubRecordings() && !isLoopDeactivation) {
    return ActionResult::DEALT_WITH;
}
```

#### 6. Overdub Cycling Feature

**Behavior:**
- Users can cycle through overdub layers by pressing at the clip head and using the SELECT encoder
- **Implementation**: Reuse existing arrangement view clip cycling behavior exactly
- **User Interface**: Same as existing clip cycling - press and hold clip head, then turn SELECT encoder
- **Visual Feedback**: Display which overdub layer is currently selected
- **Scope**: Available on AudioClips that have multiple in-place overdub layers

**Technical Implementation:**
- **Leverage Existing System**: Reuse `ArrangerView::selectEncoderAction()` pattern
- **Model `Song::getNextSessionClipWithOutput()`**: Create equivalent function for overdub layers
- **Overdub Collection**: Track overdub layers within AudioClip for cycling
- **Integration Point**: Extend existing clip head press detection and SELECT encoder handling

```cpp
// Conceptual implementation - reuse existing cycling pattern
class AudioClip {
    std::vector<SampleHolder*> overdubLayers;  // Track all overdub layers
    int32_t currentOverdubLayer = 0;           // Which layer is currently active

    SampleHolder* getNextOverdubLayer(int32_t offset);  // Cycle through layers
    void switchToOverdubLayer(int32_t layerIndex);      // Change active layer
};

// In ArrangerView - extend existing selectEncoderAction pattern
void ArrangerView::selectEncoderAction(int8_t offset) {
    // ... existing clip cycling logic ...

    // NEW: If this is an AudioClip with multiple overdub layers
    if (audioClip && audioClip->hasMultipleOverdubLayers() && pressedHead) {
        SampleHolder* newLayer = audioClip->getNextOverdubLayer(offset);
        if (newLayer != audioClip->getCurrentOverdubLayer()) {
            audioClip->switchToOverdubLayer(newLayer);
            displayOverdubLayerInfo(newLayer);
        }
    }
}
```

**Research and Development:**
- **Study existing implementation**: `Song::getNextSessionClipWithOutput()` in `song.cpp:1024-1053`
- **Understand clip head detection**: `ArrangerView::editPadAction()` pressed head logic
- **Analyze SELECT encoder handling**: `ArrangerView::selectEncoderAction()` in `arranger_view.cpp:2845-2900`
- **UI mode integration**: `UI_MODE_HOLDING_ARRANGEMENT_ROW` state management

**Implementation Points:**
- Extend `AudioClip` to track and manage multiple overdub layers
- Add overdub layer cycling logic similar to existing session clip cycling
- Integrate with existing clip head press detection
- Reuse existing SELECT encoder event handling
- Add visual feedback for current overdub layer selection
- Maintain compatibility with existing arrangement view workflows

## Technical Implementation Strategy

### 1. Reuse Existing In-Place Overdub Infrastructure

**Core Components to Leverage:**
- `AudioClip::setupOverdubInPlace()` - Setup overdub recording on existing clip
- `AudioClip::beginLinearRecording()` with `AudioInputChannel::SPECIFIC_OUTPUT` - Internal mixing
- `AudioClip::finishLinearRecording()` - Complete overdub and integrate
- `SampleRecorder` infrastructure - Handle audio recording pipeline
- Existing latency compensation and timing systems

**Key Architectural Decision:**
Use **in-place overdubs** exclusively for this feature to:
- Minimize memory usage during potentially long recording sessions
- Provide immediate feedback without UI complexity of multiple clips
- Align with loop pedal mental model where layers accumulate on the same track
- Leverage more efficient recording pipeline for real-time performance

### 2. Integration Points

#### A. Arrangement Loop System Integration

**Hook into Existing Loop Logic:**
```cpp
// In Arrangement::checkAndHandleArrangerLoop()
int32_t Arrangement::checkAndHandleArrangerLoop(int32_t currentPos) {
    if (shouldLoop && currentPos >= loopEnd) {
        // Existing loop-back logic

        // NEW: Trigger overdub creation if recording
        if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
            handleLoopOverdubCreation();
        }

        return loopStart; // Continue with normal loop-back
    }
    return currentPos;
}
```

**Leverage Existing Infrastructure:**
- `currentSong->getArrangementLoop()` - Access loop boundaries
- `currentSong->shouldLoopArrangement()` - Check loop active state
- Existing loop timing precision and sample accuracy
- Integration with playback priority system

#### B. Recording System Integration

**Track-Level Overdub Management:**
- Track which audio tracks are in loop overdub mode
- Maintain overdub count per track for discard logic
- Coordinate multiple simultaneous track overdubs

### 3. State Management

#### A. Leverage Existing AudioClip State

**Derive State from Existing Properties:**
- `isUnfinishedAutoOverdub` - Track incomplete overdub state
- `isPendingOverdub` - Track clips waiting to begin recording
- `recorder != nullptr` - Track if currently recording
- `overdubNature` - Type of overdub being performed
- Loop position derived from `currentSong->getArrangementLoop()` boundaries

**New State Properties for Loop Overdubbing:**
- `pendingMuteTermination` - Track which tracks have pending mute termination intent
- `loopOverdubCycleCount` - Count completed overdub cycles per track for incomplete overdub logic

**Sample-Accurate Position Tracking:**
- Reuse existing `SampleHolder.startPos` and `SampleHolder.endPos` for boundary management
- Leverage `SampleRecorder.numSamplesCaptured` for precise recording position tracking
- Apply existing latency compensation calculations from `numSamplesToRunBeforeBeginningCapturing`
- Use existing boundary validation logic from `SampleHolder::setAudioFile()`

**Global Coordination:**
- Scan recording AudioClips for loop overdub sessions
- Use existing recording state management
- Leverage existing cleanup mechanisms

#### B. UI State Synchronization

**Visual Feedback:**
- Indicate pending termination state with blinking mute button LED
- Update LED state when termination intent is cancelled
- Maintain existing recording LED patterns during loop overdubbing
- Clear pending termination visual feedback when recording completes

### 4. Error Handling and Edge Cases

#### A. Memory Management

**Strategy:**
- Reuse in-place overdub memory efficiency
- Monitor available RAM during extended sessions
- Gracefully handle memory exhaustion
- Clean up incomplete overdubs on failure

#### B. Loop Boundary Precision

**Leverage Existing Sample-Accurate Boundary System:**
- **Reuse SamplePlaybackGuide hierarchy** - Use existing boundary calculation infrastructure
- **Maintain byte-level alignment** - All overdub boundaries aligned to exact sample boundaries (bytesPerSample)
- **Preserve cluster-based precision** - Leverage existing cluster reading system for boundary enforcement
- **Apply overshoot compensation** - Use existing boundary crossing compensation when loop end is reached

**Key Implementation Points:**
```cpp
// Reuse existing boundary alignment from SampleLowLevelReader
int32_t bytesPerSample = sample->numChannels * sample->byteDepth;  // Typically 6 bytes (24-bit stereo)
int32_t alignedBytePosition = (rawBytePosition / bytesPerSample) * bytesPerSample;

// Apply exact overshoot compensation when looping
uint32_t startPlaybackAtByte = guide->getBytePosToStartPlayback(justLooped);
startPlaybackAtByte += byteOvershoot * guide->playDirection;  // Exact sample compensation
```

**Critical Precision Guarantees:**
- **±1 sample accuracy** - No sub-sample timing errors accumulate over overdub cycles
- **Musical timing integration** - Loop boundaries synchronize with PlaybackHandler tick system
- **Latency compensation consistency** - Reuse existing `kAudioRecordLagCompensation` and button press latency handling

#### C. Multi-Track Coordination

**Synchronization:**
- Coordinate overdub creation across tracks
- Handle partial success scenarios
- Maintain timing relationships between tracks
- Preserve existing multi-track recording behavior

**Loop Boundary Protection During Recording:**
- **Block loop boundary changes** when any track is actively recording overdubs
- **Allow loop deactivation** - turning off the loop allows tracks to continue recording normally (non-looping)
- **Implementation**: Check for active overdub recordings before allowing loop boundary modifications

```cpp
// Conceptual boundary change validation
bool canModifyLoopBoundaries() {
    return !hasActiveLoopOverdubRecordings();
}

bool canDeactivateLoop() {
    // Always allow loop deactivation - recordings continue as normal arrangement recording
    return true;
}
```

## Implementation Phases

### Phase 1: Core Loop-Triggered Overdub Creation

**Scope:**
- Basic loop boundary detection for recording tracks
- Single-track in-place overdub creation
- Integration with existing arrangement loop system

**Key Components:**
- `Arrangement::handleLoopOverdubCreation()`
- Extension of `Arrangement::checkAndHandleArrangerLoop()`
- Track-level overdub session management

**Success Criteria:**
- Recording track automatically creates overdub at loop end
- Overdub uses existing in-place infrastructure
- Loop timing remains sample-accurate

### Phase 2: Verify Multi-Track Support and State Management

**Scope:**
- Multiple simultaneous track overdub sessions
- Per-track state tracking and management
- Coordination of overdub creation timing

**Key Components:**
- `TrackLoopOverdubState` management system
- Multi-track overdub coordination logic
- Enhanced state tracking in arrangement recording

**Success Criteria:**
- Multiple tracks can simultaneously do loop overdubs
- State is properly maintained per track
- No timing conflicts between tracks

### Phase 3: User Control and Termination

**Scope:**
- Mute button termination at loop boundaries
- Incomplete overdub discard logic
- UI integration and feedback

**Key Components:**
- Enhanced mute button handling
- Loop-boundary-aware termination logic
- Overdub discard and cleanup system

**Success Criteria:**
- Mute button terminates recording at loop end
- Incomplete overdubs are properly discarded
- UI provides appropriate feedback

### Phase 4: Overdub Cycling Feature

**Scope:**
- Overdub layer cycling via clip head + SELECT encoder
- Reuse existing arrangement view clip cycling behavior
- Visual feedback for overdub layer selection

**Key Components:**
- Overdub layer management within AudioClip
- Extension of existing `ArrangerView::selectEncoderAction()` logic
- Overdub layer cycling function similar to `Song::getNextSessionClipWithOutput()`
- Integration with existing clip head press detection

**Success Criteria:**
- Users can cycle through overdub layers using familiar clip cycling interface
- Visual feedback clearly indicates which overdub layer is active
- Seamless integration with existing arrangement view workflows
- No conflicts with existing clip cycling behavior

### Phase 5: Polish and Edge Cases

**Scope:**
- Error handling and recovery
- Performance optimization
- Extended session support
- User experience refinements

**Key Components:**
- Comprehensive error handling
- Memory usage optimization
- Extended overdub session support
- UI/UX improvements

**Success Criteria:**
- Robust handling of all edge cases
- Optimal performance during long sessions
- Intuitive user experience
- Full integration with existing workflows

## Integration with Existing Systems

### 1. Overdub Infrastructure Reuse

**Leverage Existing Components:**
- `Song::createPendingNextOverdubBelowClip()` pattern - **NOT USED** (we use in-place)
- `AudioClip::setupOverdubInPlace()` - **PRIMARY COMPONENT** for setup
- `AudioClip::beginLinearRecording()` with internal mixing - **CORE RECORDING**
- `SampleRecorder` pipeline - **AUDIO PROCESSING**
- `AudioClip::finishLinearRecording()` - **COMPLETION LOGIC**

**Key Insight:**
The existing in-place overdub system in grid layout provides the exact foundation needed. The arrangement view implementation will be a context-specific application of this existing infrastructure.

### 2. Arrangement Loop System Reuse

**Leverage Existing Components:**
- `ArrangementLoop` class in `Song` - **BOUNDARY DEFINITIONS**
- `Arrangement::checkAndHandleArrangerLoop()` - **TIMING INTEGRATION**
- `PlaybackHandler` loop position tracking - **POSITION MONITORING**
- Sample-accurate loop timing - **PRECISION MAINTENANCE**

**Key Insight:**
The arrangement loop system already provides sample-accurate timing and boundary detection. The overdub feature adds behavior to existing timing events rather than creating new timing systems.

**Sample-Accurate Integration Points:**
- **Boundary Enforcement**: Leverage existing `SampleLowLevelReader::setupReassessmentLocation()` for precise loop end detection
- **Cluster Management**: Use existing `changeClusterIfNecessary()` for seamless boundary crossing
- **Overshoot Handling**: Apply existing overshoot compensation when overdub boundaries don't align perfectly with loop boundaries
- **Musical Timing**: Maintain integration with `PlaybackHandler` tick system for sample-accurate musical timing

### 3. Recording System Extension

**Build Upon Existing:**
- `RecordingMode::ARRANGEMENT` - **CONTEXT DETECTION**
- Per-track recording state - **TRACK MANAGEMENT**
- Audio input/output routing - **SIGNAL FLOW**
- Latency compensation - **TIMING ACCURACY**

## User Experience Flow

### 1. Typical Usage Scenario

1. **Setup**: User sets arrangement loop boundaries
2. **Initial Recording**: User starts recording on one or more audio tracks
3. **Loop Cycling**: When playhead hits loop end, new overdub layers are automatically created
4. **Layer Building**: Each loop cycle adds a new overdub layer using in-place mixing
5. **Termination**:
   - User presses mute button to finish recording at next loop boundary (mute LED blinks to indicate intent, can be cancelled by pressing mute again)
   - OR user presses RECORD button to stop immediately (may discard incomplete overdub if conditions met)
6. **Result**: Track contains all overdub layers mixed into a single layered recording

### 2. Advanced Usage Scenarios

**Multi-Track Overdub Session:**
- Multiple tracks recording simultaneously
- Each track builds independent overdub layers
- Synchronized loop boundary behavior across tracks

**Incomplete Overdub Management:**
- User stops recording mid-loop (via RECORD button) after several complete overdubs
- System discards incomplete partial overdub
- Preserves all complete overdub layers

**Loop Boundary Protection:**
- Loop boundary changes blocked during active recording
- Loop deactivation allowed - recordings continue as normal arrangement recording
- Clear user feedback about restricted operations during recording

**Overdub Layer Cycling:**
- User presses and holds at clip head, then turns SELECT encoder
- Cycles through available overdub layers exactly like existing clip cycling
- Visual feedback shows which overdub layer is currently active
- Seamless integration with existing arrangement view workflow

## Testing Strategy

### 1. Unit Testing

**Core Components:**
- Loop boundary detection accuracy
- In-place overdub creation timing
- State management correctness
- Memory management during extended sessions

### 2. Integration Testing

**System Integration:**
- Arrangement loop system integration
- Multi-track recording coordination
- UI state synchronization
- Error handling and recovery

### 3. User Scenario Testing

**Workflow Validation:**
- Basic single-track overdub cycles
- Multi-track simultaneous overdubbing
- Mute button termination behavior (including blinking LED and cancellation)
- Incomplete overdub handling
- Pending termination cancellation scenarios
- **Overdub cycling behavior** (clip head + SELECT encoder interaction)
- **Overdub layer switching** (seamless transition between layers)
- **Visual feedback** for overdub layer selection

## Performance Considerations

### 1. Memory Usage

**Optimization Strategy:**
- In-place overdubs minimize memory growth
- Monitor RAM during extended sessions
- Efficient cleanup of incomplete overdubs
- Reuse existing memory management patterns

### 2. Real-Time Performance

**Maintain Existing Performance:**
- Leverage existing audio pipeline efficiency
- Minimize additional processing overhead
- Preserve sample-accurate timing through existing boundary management
- Reuse optimized recording infrastructure

**Boundary Processing Efficiency:**
- **No Additional Boundary Calculations**: Reuse existing `ArrangementLoop` boundary detection
- **Minimal Overshoot Processing**: Leverage existing compensation algorithms from `SampleLowLevelReader`
- **Cached Boundary State**: Use existing cluster-based caching for boundary information
- **SIMD-Aligned Operations**: Maintain existing byte-alignment for optimal memory access patterns

### 3. UI Responsiveness

**Responsive Feedback:**
- Use existing LED and display patterns
- Minimal additional UI processing
- Efficient state change notifications
- Preserve existing arrangement view performance

## Future Enhancements

### 1. Advanced Loop Control

**Potential Extensions:**
- Variable loop length during overdub sessions
- Loop subdivision for complex rhythmic patterns
- Cross-track overdub synchronization options

### 2. Overdub Management

**Enhanced Control:**
- Individual overdub layer volume control
- Overdub layer muting/soloing
- Overdub layer reordering or removal

### 3. Integration with Other Features

**System Integration:**
- Integration with stem export for overdub layers
- MIDI synchronization with overdub cycling
- Integration with sampling and resampling workflows

## Conclusion

This specification leverages the Deluge's existing in-place overdub infrastructure and arrangement loop system to provide an intuitive, loop pedal-style recording workflow within arrangement view. By reusing proven components and maintaining architectural consistency, the implementation can provide powerful new functionality while preserving the Deluge's performance characteristics and user experience quality.

The focus on in-place overdubs ensures memory efficiency during extended recording sessions, while the integration with existing arrangement loop timing maintains the sample-accurate precision that users expect. The result is a natural extension of the Deluge's capabilities that feels integrated rather than grafted on.
