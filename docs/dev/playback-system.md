# Playback System Architecture

**Central timing coordinator** - Manages musical tempo, swing, and synchronization across the entire Deluge.

## Key Concepts

**PlaybackHandler** - Core timing system coordinating all tempo-sensitive operations
- ⏱️ **Dual Timing**: Timer ticks (hardware) + Swung ticks (musical)
- 🎵 **Swing Engine**: Sophisticated groove timing with per-track swing
- 🔗 **MIDI Sync**: Master/slave clock synchronization
- 🎛️ **Mode Coordination**: Session, arranger, and audio clip playback

**Location**: `src/deluge/playback/playback_handler.cpp`

## Dual Timing System

The Deluge uses two independent timing systems working together:

### Timer Ticks (Hardware Timing)
```cpp
void PlaybackHandler::actionTimerTick() {
    // High-resolution hardware timer (96 PPQN)
    if (playState & PLAYSTATE_FLAG_STOPPED) return;

    timeNextTimerTickBig += timerInterval;
    scheduleSwungTick();  // Calculate musical timing from hardware tick

    // Process tempo changes
    if (tempoMagnitudeMatchingFlag) {
        uint32_t timePerBigTimer = tempoMagnitude * tempoMagnitudeMatchingMult;
        timerInterval = timePerBigTimer / 3;
    }
}
```

### Swung Ticks (Musical Timing)
```cpp
void PlaybackHandler::actionSwungTick() {
    // Musical events (notes, automation) happen on swung ticks
    if (currentSong) {
        currentSong->doTickForward(modelStack);
    }

    // Handle cross-screen communication
    if (currentUIMode == UI_MODE_INSTRUMENT_CLIP_PRESSED_IN_SONG_VIEW) {
        session.doTickForward();
    }

    // Update musical position
    swungTicksTilNextEvent = getCurrentInternalTickLength(playbackState);
}
```

**Key Insight**: Hardware ticks run at fixed intervals, but musical ticks can be delayed/advanced for swing timing.

## Swing Engine

### Per-Track Swing Implementation
```cpp
int32_t PlaybackHandler::getActualSwingFromBPMAndSwingAmount(uint32_t swingAmount) {
    // Calculate swing delay based on tempo and swing amount
    uint64_t swingInterval = (uint64_t)swingAmount * currentTempo;
    return swingInterval >> 8;  // Scale to audio samples
}

void PlaybackHandler::scheduleSwungTick() {
    int32_t swingAdjustment = 0;

    // Apply swing on off-beats (2nd, 4th 16th notes in 4/4)
    if (playbackState.bpmTicksToNextTimerTick == 48) {  // Off-beat position
        swingAdjustment = getActualSwingFromBPMAndSwingAmount(globalSwingAmount);
    }

    scheduledSwungTickTime = audioSampleTimer + baseTickLength + swingAdjustment;
    swungTickScheduled = true;
}
```

### Musical Subdivision Support
- **16th note swing**: Standard groove timing
- **Triplet swing**: 3-against-2 feel
- **Custom ratios**: User-defined swing percentages
- **Per-track swing**: Individual swing amounts per instrument

## Playback State Management

### Core State Variables
```cpp
struct PlaybackState {
    uint8_t playState;                    // PLAYING, STOPPED, RECORDING flags
    uint32_t bpmTicksToNextTimerTick;     // Position within beat (0-95)
    uint32_t timePerTimerTick;            // Duration of one timer tick
    bool isRecording;
    bool metronomeOn;
};
```

### State Transitions
```cpp
void PlaybackHandler::playButtonPressed() {
    if (playState & PLAYSTATE_FLAG_STOPPED) {
        // Start playback
        playState = PLAYSTATE_FLAG_PLAYING;
        resetTimeBaseToZero();

        // Initialize timing
        scheduleSwungTick();
        setLedStates();

        // Notify all systems
        currentSong->resumePlayback(modelStack);
    } else {
        // Stop playback
        endPlayback();
    }
}
```

## Mode Coordination

### Session View Integration
```cpp
void PlaybackHandler::doSessionViewTick() {
    // Session clips play independently but sync to global timing
    for (auto& clipInstance : session.clipInstances) {
        if (clipInstance.clip && clipInstance.launchStyle == LAUNCH_STYLE_SYNC) {
            clipInstance.clip->doTickForward(modelStack);
        }
    }

    // Handle clip launching/stopping
    session.considerLaunchEvent(scheduledSwungTickTime);
}
```

### Arranger View Integration
```cpp
void PlaybackHandler::doArrangerTick() {
    // Arranger follows linear timeline
    if (currentSong->arrangementYScroll != -1) {
        currentSong->doArrangerTick(modelStack);

        // Handle arrangement looping
        if (arrangement.hasFinished() && arrangement.loopEnabled) {
            arrangement.restart();
        }
    }
}
```

### Audio Clip Coordination
```cpp
// Audio clips sync to musical timing but maintain precise sample positions
void AudioClip::doTickForward(ModelStackWithTimelineCounter* modelStack) {
    // Update playback position based on tempo
    int32_t samplesPerTick = getSamplesPerTick(currentTempo);
    voiceSample->playbackPosition += samplesPerTick;

    // Handle looping and sync
    if (voiceSample->playbackPosition >= sampleHolder->audioFile->length) {
        if (repeatMode != REPEAT_ONCE) {
            voiceSample->playbackPosition %= sampleHolder->audioFile->length;
        }
    }
}
```

## MIDI Synchronization

### Master Clock Mode
```cpp
void PlaybackHandler::sendMIDIClock() {
    // Send 24 MIDI clocks per quarter note
    if (midiClockOutStatus == MIDI_CLOCK_OUT_ON) {
        midiEngine.sendClock();
        midiClocksOutputted++;

        // Schedule next clock
        int32_t intervalBetweenMIDIClocks = timerInterval / 4;  // 24 PPQN
        timeNextMIDIClockOutTick += intervalBetweenMIDIClocks;
    }
}
```

### Slave Clock Mode
```cpp
void PlaybackHandler::receiveMIDIClock() {
    // Sync internal tempo to incoming MIDI clock
    uint32_t timeSinceLastClock = audioSampleTimer - timeLastMIDIClockReceived;

    // Calculate incoming tempo (with smoothing)
    float incomingTempo = 2500000.0f / timeSinceLastClock;  // 60000000 ÷ 24 clocks ÷ sample_rate
    currentTempo = (currentTempo * 0.9f) + (incomingTempo * 0.1f);  // Smooth tempo changes

    // Adjust next tick timing
    scheduleSwungTick();
    timeLastMIDIClockReceived = audioSampleTimer;
}
```

## Cross-System Integration

### Audio Engine Coordination
```cpp
// Called from AudioEngine::routine_() for sample-accurate timing
void PlaybackHandler::considerTimingEvents(int32_t numSamples) {
    // Check if timer tick occurs during current audio window
    int32_t timeTilTick = timeNextTimerTickBig - audioSampleTimer;

    if (timeTilTick < numSamples && timeTilTick >= 0) {
        actionTimerTick();  // Process tick immediately
        return numSamples;  // Continue with remaining samples
    }

    return -1;  // No timing event in this window
}
```

### UI Mode Integration
```cpp
void PlaybackHandler::setPlayState(uint8_t newPlayState, bool sendMIDIStartStop) {
    playState = newPlayState;

    // Update UI indicators
    setLedStates();

    // Notify current UI mode
    if (getCurrentUI()) {
        getCurrentUI()->playStateChanged();
    }

    // Send MIDI transport messages
    if (sendMIDIStartStop) {
        if (newPlayState & PLAYSTATE_FLAG_PLAYING) {
            midiEngine.sendStart();
        } else {
            midiEngine.sendStop();
        }
    }
}
```

## Performance & Real-Time Considerations

### Sample-Accurate Scheduling
```cpp
// Events scheduled with sample precision within audio windows
void PlaybackHandler::scheduleEvent(uint32_t eventTime, EventType type) {
    if (eventTime >= audioSampleTimer) {
        ScheduledEvent event{eventTime, type};
        eventQueue.insert(event);  // Sorted by time
    } else {
        // Event in past - process immediately
        processEvent(type);
    }
}
```

### Memory-Efficient Timing
```cpp
// Fixed-point arithmetic avoids floating-point in real-time code
uint64_t timeNextTimerTickBig;  // High-precision timing (32.32 fixed-point)
uint32_t timerInterval;         // Samples between timer ticks

// Calculate next tick without division
timeNextTimerTickBig += timerInterval;
uint32_t nextTickSample = timeNextTimerTickBig >> 32;
```

### CPU Load Management
```cpp
void PlaybackHandler::adjustTimingPrecision() {
    if (cpuUsage > CPU_HIGH_THRESHOLD) {
        // Reduce timing precision under high CPU load
        timerTicksToSkip = 2;  // Process every 2nd timer tick
    } else {
        timerTicksToSkip = 0;  // Normal precision
    }
}
```

## Debug & Monitoring

### Timing Diagnostics
```cpp
#if ENABLE_TIMING_DEBUG
void logTimingEvent(const char* event) {
    timingLog[logIndex].eventName = event;
    timingLog[logIndex].audioTime = audioSampleTimer;
    timingLog[logIndex].systemTime = getSystemTime();
    logIndex = (logIndex + 1) % TIMING_LOG_SIZE;
}
#endif
```

### Performance Metrics
```cpp
struct TimingStats {
    uint32_t totalTicks;
    uint32_t missedDeadlines;
    float averageJitter;
    uint32_t maxLatency;
};
```

---

**Related Documentation:**
- [Audio Engine](audio-engine.md) - Real-time audio processing integration
- [Voice Management](voice-management.md) - Voice lifecycle coordination
- [MIDI Processing](midi-processing.md) - MIDI sync implementation
- [Sample Streaming](sample-streaming.md) - Audio clip timing integration
