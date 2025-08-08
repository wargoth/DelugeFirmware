# Audio Engine Architecture

**Core real-time audio processing system** - Window-based rendering with adaptive CPU load management.

## Key Concepts

**AudioEngine::routine_()** - Main audio loop running continuously at 44.1kHz
- ⚡ **Real-time deadline**: ~1.5ms (64-128 samples)
- 🔄 **Window-based**: Variable length rendering adapts to CPU load
- 🎵 **Event-precise**: Musical events scheduled to exact samples
- 🚫 **No allocation**: Uses pre-allocated object pools only

**Location**: `src/deluge/processing/engines/audio_engine.cpp`

## Audio Processing Flow

### Main Loop Structure
```cpp
[[gnu::hot]] void routine_() {
    // 1. Calculate window size (1-128 samples based on CPU load)
    size_t numSamples = calculateWindowLength();

    // 2. Handle musical timing events (notes, automation)
    int32_t timeWithinWindowAtWhichMIDIOrGateOccurs;
    tickSongFinalizeWindows(numSamples, timeWithinWindowAtWhichMIDIOrGateOccurs);

    // 3. Render all voices and effects
    renderAudio(numSamples);

    // 4. Output MIDI/CV events at exact sample times
    flushMIDIGateBuffers();
}
```

### Critical Threading Rules
- **Audio Thread**: `routine_()` - NO memory allocation, NO file I/O, NO blocking
- **Main Thread**: UI, memory management, non-real-time operations
- **SD Thread**: Sample/preset loading only

## Adaptive Window System

Windows automatically resize based on system load and musical events:

**Window Size Logic**:
- **Light Load**: 1-4 samples (immediate response)
- **Heavy Load**: 32-128 samples (CPU efficiency)
- **Event-Driven**: Always shortened for note-ons, ticks, automation
- **NEON-Aligned**: Rounded to multiples of 4 for SIMD

### Sample-Accurate Events
```cpp
// Events scheduled to exact sample positions within windows
if (timeTilNextTick < numSamples && timeTilNextTick >= 0) {
    numSamples = timeTilNextTick;  // Shorten window to event
}

if (timeTilNextTick <= 0) {
    playbackHandler.actionTimerTick();  // Process at sample 0
    timeWithinWindowAtWhichMIDIOrGateOccurs = 0;
}
```

## Voice Pool Management

**Object Pools** - Pre-allocated voices avoid real-time allocation:
```cpp
VoicePool voicePool;                    // Synth voices
VoiceSamplePool voiceSamplePool;        // Sample playback
TimeStretcherPool timeStretcherPool;    // Time-stretching
```

### Voice Lifecycle
```cpp
// Acquire voice (may trigger culling if pool exhausted)
VoiceSample* AudioEngine::solicitVoiceSample() {
    try {
        return &VoiceSamplePool::get().acquire();
    } catch (deluge::exception e) {
        terminateOneVoice(currentWindowSize);  // Cull lowest priority
        return &VoiceSamplePool::get().acquire();
    }
}

// Release voice back to pool
void AudioEngine::voiceSampleUnassigned(VoiceSample* voiceSample) {
    VoiceSamplePool::get().release(*voiceSample);
}
```

### Intelligent Voice Culling
**Priority factors** when resources constrained:
1. **Note velocity** - Higher velocity = higher priority
2. **Voice age** - Recent notes preferred
3. **Envelope stage** - Avoid cutting notes in attack
4. **Current amplitude** - Louder voices prioritized

See: [Voice Management System](voice-management.md)

## Audio Rendering Pipeline

```cpp
void renderAudio(size_t numSamples) {
    // Initialize buffers
    std::span renderingBuffer{renderingMemory.data(), numSamples};
    std::span reverbBuffer{reverbMemory.data(), numSamples};

    // Clear for accumulation
    memset(&renderingMemory, 0, renderingBuffer.size_bytes());
    memset(&reverbMemory, 0, reverbBuffer.size_bytes());

    // Render song content (voices, samples, automation)
    if (currentSong) {
        currentSong->renderAudio(renderingBuffer, reverbBuffer.data(), sideChainHitPending);
    }

    // Apply effects chain
    renderReverb(numSamples);
    renderSamplePreview(numSamples);
    renderSongFX(numSamples);           // Master compressor, EQ
    metronome.render(renderingBuffer);

    // Calculate levels for VU meters
    approxRMSLevel = envelopeFollower.calcApproxRMS(renderingBuffer);

    // Set monitoring routing
    setMonitoringMode();

    // Point DMA to rendered buffer
    renderingBufferOutputPos = renderingMemory.begin();
}
```

**Buffer System**: 128-sample circular buffer continuously output via I2S/DMA

## Real-Time Safety

### Memory Management
```cpp
// Pre-allocated object pools (NO malloc/free in audio thread)
VoicePool voicePool;
VoiceSamplePool voiceSamplePool;
TimeStretcherPool timeStretcherPool;

// Fast allocator for real-time operations
using fast_allocator = deluge::memory::fast_allocator;
```

### Sample Streaming Coordination
```cpp
void routineWithClusterLoading() {
    routine_();  // Audio first - always highest priority

    // Safe point for non-real-time operations
    audioFileManager.loadAnyEnqueuedClusters(1);  // Load 1 sample cluster max
}
```

### Lock-Free Communication
```cpp
// Atomic flags for cross-thread communication
std::atomic<bool> bypassCulling{false};
std::atomic<bool> mustUpdateReverbParamsBeforeNextRender{false};
std::atomic<InputMonitoringMode> inputMonitoringMode;
```

See: [Sample Streaming System](sample-streaming.md)

## Performance & Monitoring

### CPU Usage Tracking
```cpp
#if REPORT_AVERAGE_PERFORMANCE
uint32_t startTime = getTimerTicks();
routine_();
uint32_t endTime = getTimerTicks();
usageTimes[usageTimeIndex] = endTime - startTime;
#endif
```

### Voice Monitoring
```cpp
int32_t getNumVoices() {
    int32_t total = 0;
    for (Sound* sound : sounds) total += sound->voices_.size();
    return total;
}
```

### Key Optimizations
1. **NEON SIMD**: Process 4 samples simultaneously
2. **Branch Prediction**: `[[likely]]`/`[[unlikely]]` annotations
3. **Cache-Friendly**: Related data packed together in structs
4. **Hot Path Marking**: `[[gnu::hot]]` for critical functions

## Integration Patterns

### Song Coordination
```cpp
// Audio engine delegates to song for content rendering
currentSong->renderAudio(renderingBuffer, reverbBuffer.data(), sideChainHitPending);

// Song manages clip and voice rendering
void Song::renderAudio(outputBuffer, reverbBuffer, sideChainHitPending) {
    for (Output* output : outputs) {
        if (output->activeClip) {
            output->renderOutput(modelStack, outputBuffer, reverbBuffer, sideChainHitPending);
        }
    }
}
```

### Voice Processing Chain
```cpp
// Each voice renders through synthesis pipeline
void Voice::render(outputBuffer) {
    // Oscillators → Filters → Envelopes → Mix
    for (Source& source : sources) source.render(outputBuffer, envelopes, lfos);
    filters[0].render(outputBuffer, filterParams);
    filters[1].render(outputBuffer, filterParams);
    applyEnvelopes(outputBuffer);
    mixToOutput(outputBuffer);
}
```

## Debug & Logging
```cpp
#if DO_AUDIO_LOG
void logAction(char const* string) {
    audioLogStrings[numAudioLogItems] = string;
    audioLogTimes[numAudioLogItems] = audioSampleTimer;
}
#endif

// Performance tracking
extern int32_t cpuDireness;                    // CPU load estimate
extern uint8_t numHopsEndedThisRoutineCall;    // Sample streaming activity
```

---

**Related Documentation:**
- [Playback System](playback-system.md) - Musical timing coordination
- [Voice Management](voice-management.md) - Voice allocation & priority
- [Sample Streaming](sample-streaming.md) - SD card sample loading
- [MIDI Processing](midi-processing.md) - MIDI input/output integration
