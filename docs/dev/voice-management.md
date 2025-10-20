# Voice Management System

**Resource allocation engine** - Intelligent voice allocation with priority-based culling for polyphonic synthesis.

## Key Concepts

**Voice Pools** - Pre-allocated objects to avoid real-time memory allocation
- 🎛️ **Voice**: Synth voice (oscillators, filters, envelopes)
- 📡 **VoiceSample**: Sample playback voice with time-stretching
- ⏰ **TimeStretcher**: Real-time tempo adaptation engine
- 🎯 **Priority System**: Intelligent voice stealing based on musical context

**Location**: Various voice types across `src/deluge/processing/` and `src/deluge/model/`

## Voice Allocation Architecture

### Object Pool System
```cpp
// Pre-allocated pools avoid real-time allocation
namespace AudioEngine {
    using VoicePool = ObjectPool<Voice, fast_allocator>;
    using VoiceSamplePool = ObjectPool<VoiceSample, fast_allocator>;
    using TimeStretcherPool = ObjectPool<TimeStretcher, fast_allocator>;
}

// Acquire voice (may trigger culling)
VoiceSample* AudioEngine::solicitVoiceSample() {
    try {
        return &VoiceSamplePool::get().acquire();
    } catch (deluge::exception e) {
        terminateOneVoice(currentWindowSize);  // Force voice culling
        return &VoiceSamplePool::get().acquire();
    }
}
```

### Voice Types Hierarchy
```cpp
class Voice {
    // Base voice class - synth oscillators/filters
    Envelope envelopes[NUM_ENVELOPES];
    LFO lfos[NUM_LFOS];
    Source sources[MAX_NUM_SOURCES];
    Filter filters[NUM_FILTERS];
};

class VoiceSample : public Voice {
    // Sample playback voice with streaming
    Sample* sample;
    VoiceUnisonSource* unisonSources;
    TimeStretcher* timeStretcher;  // Optional tempo adaptation
};

class VoiceVector : public Voice {
    // Wavetable synthesis voice
    WavetableSource* sources;
    int32_t morphPosition;
};
```

## Priority-Based Voice Culling

### Priority Calculation
```cpp
int32_t Voice::getPriorityRating() {
    int32_t priority = 0;

    // 1. Note velocity (higher = higher priority)
    priority += velocity << 16;

    // 2. Envelope stage (attack phase = critical)
    if (envelopes[0].state == EnvelopeStage::ATTACK) {
        priority += 0x1000000;  // Never cull attacking notes
    }

    // 3. Current amplitude (louder = higher priority)
    priority += getCurrentAmplitude() << 8;

    // 4. Voice age (newer = higher priority)
    priority -= (audioSampleTimer - noteOnTime) >> 4;

    // 5. Musical context (downbeat = higher priority)
    if (wasTriggeredOnDownbeat) {
        priority += 0x800000;
    }

    return priority;
}
```

### Intelligent Culling Algorithm
```cpp
void AudioEngine::terminateOneVoice(size_t numSamples) {
    // Collect all active voices across sounds
    std::vector<Voice*> allVoices;
    for (Sound* sound : sounds) {
        for (auto& voice : sound->voices) {
            allVoices.push_back(&voice);
        }
    }

    if (allVoices.empty()) return;

    // Find lowest priority voice
    Voice* victimVoice = allVoices[0];
    for (Voice* voice : allVoices) {
        // Skip voices already releasing fast
        if (voice->envelopes[0].state >= EnvelopeStage::FAST_RELEASE &&
            voice->envelopes[0].fastReleaseIncrement >= SOFT_CULL_INCREMENT) {
            continue;
        }

        // Select lower priority
        if (voice->getPriorityRating() < victimVoice->getPriorityRating()) {
            victimVoice = voice;
        }
    }

    // Apply fast release to selected voice
    bool stillActive = victimVoice->doFastRelease(SOFT_CULL_INCREMENT);
    if (!stillActive) {
        victimVoice->sound->freeActiveVoice(victimVoice);
    }
}
```

## Voice Lifecycle Management

### Note-On Processing
```cpp
void Sound::noteOn(ModelStackWithSoundFlags* modelStack,
                   uint8_t note, uint8_t velocity,
                   uint32_t sampleSyncLength) {

    // Check for voice stealing on same note
    Voice* existingVoice = getVoiceForNote(note);
    if (existingVoice) {
        // Retrigger existing voice
        existingVoice->noteOn(modelStack, note, velocity);
        return;
    }

    // Try to allocate new voice
    Voice* newVoice = AudioEngine::solicitVoice(voiceType);
    if (!newVoice) {
        // Pool exhausted even after culling - fail silently
        return;
    }

    // Initialize voice parameters
    newVoice->noteOnTime = audioSampleTimer;
    newVoice->velocity = velocity;
    newVoice->wasTriggeredOnDownbeat = isCurrentlyOnDownbeat();

    // Add to active voice list
    voices.push_back(newVoice);

    // Start synthesis pipeline
    newVoice->noteOn(modelStack, note, velocity);
}
```

### Note-Off Processing
```cpp
void Sound::noteOff(uint8_t note, uint8_t velocity) {
    Voice* voice = getVoiceForNote(note);
    if (!voice) return;

    // Begin release phase
    voice->noteOff();

    // Mark for potential cleanup
    voice->waitingToBeDeleted = true;

    // Fast cleanup if silent
    if (voice->getCurrentAmplitude() < VOICE_SILENCE_THRESHOLD) {
        freeActiveVoice(voice);
    }
}
```

### Voice Cleanup
```cpp
void AudioEngine::cullVoices() {
    for (Sound* sound : sounds) {
        auto it = sound->voices.begin();
        while (it != sound->voices.end()) {
            Voice& voice = *it;

            // Remove if finished releasing
            if (voice.waitingToBeDeleted &&
                voice.getCurrentAmplitude() < VOICE_SILENCE_THRESHOLD) {

                sound->freeActiveVoice(&voice);
                it = sound->voices.erase(it);
            } else {
                ++it;
            }
        }
    }
}
```

## Sample Voice System

### VoiceSample Architecture
```cpp
class VoiceSample {
    Sample* sample;                    // Audio file reference
    VoiceUnisonSource* unisonSources;  // Multiple sources for thick sound
    TimeStretcher* timeStretcher;      // Tempo adaptation (optional)

    // Playback state
    uint32_t playbackPosition;         // Current sample position
    int32_t pitchAdjust;              // Pitch shift amount
    bool looping;                     // Loop enable flag

    void render(std::span<StereoSample> outputBuffer);
};
```

### Sample Streaming Integration
```cpp
void VoiceSample::render(std::span<StereoSample> outputBuffer) {
    if (!sample) return;

    // Calculate which cluster we need
    int32_t clusterIndex = playbackPosition / CLUSTER_SIZE;

    // Ensure cluster is loaded (non-blocking)
    if (!audioFileManager.isClusterLoaded(sample, clusterIndex)) {
        audioFileManager.queueClusterForLoading(sample, clusterIndex);

        // Use cached data or silence while loading
        renderFallback(outputBuffer);
        return;
    }

    // Render from loaded cluster
    uint8_t* clusterData = audioFileManager.getClusterData(sample, clusterIndex);
    renderFromClusterData(outputBuffer, clusterData);

    // Advance playback position
    advancePlaybackPosition(outputBuffer.size());
}
```

## Time-Stretching System

### TimeStretcher Integration
```cpp
class TimeStretcher {
    static constexpr int32_t BUFFER_SIZE = 2048;

    StereoSample inputBuffer[BUFFER_SIZE];
    StereoSample outputBuffer[BUFFER_SIZE];

    float stretchRatio;        // 1.0 = original tempo
    int32_t inputPos;          // Read position
    int32_t outputPos;         // Write position

    void setupPitchAndTimeStretch(float pitchAdjust, float timeStretch);
    void processBuffer(std::span<StereoSample> input, std::span<StereoSample> output);
};
```

### Real-Time Tempo Adaptation
```cpp
void VoiceSample::updateTimeStretching() {
    if (!timeStretcher) return;

    // Calculate stretch ratio from current tempo
    float currentTempo = playbackHandler.getCurrentTempo();
    float originalTempo = sample->recordedTempo;
    float stretchRatio = originalTempo / currentTempo;

    // Apply pitch compensation if enabled
    float pitchAdjust = (pitchCompensationEnabled) ? stretchRatio : 1.0f;

    timeStretcher->setupPitchAndTimeStretch(pitchAdjust, stretchRatio);
}
```

## Unison Voice Management

### VoiceUnisonSource System
```cpp
struct VoiceUnisonSource {
    Source source;
    float panning;           // Stereo spread
    float detuning;         // Pitch offset
    float phaseOffset;      // Phase relationship
};

class VoiceUnison {
    static constexpr int32_t MAX_UNISON_VOICES = 8;
    VoiceUnisonSource sources[MAX_UNISON_VOICES];
    int32_t numSources;

    void setupUnison(int32_t numVoices, float spread, float detune);
    void render(std::span<StereoSample> outputBuffer);
};
```

### Unison Rendering
```cpp
void VoiceUnison::render(std::span<StereoSample> outputBuffer) {
    // Clear output buffer
    std::fill(outputBuffer.begin(), outputBuffer.end(), StereoSample{0, 0});

    // Render each unison voice
    for (int32_t i = 0; i < numSources; i++) {
        VoiceUnisonSource& source = sources[i];

        // Render source to temp buffer
        StereoSample tempBuffer[outputBuffer.size()];
        source.source.render({tempBuffer, outputBuffer.size()});

        // Mix with panning and level adjustment
        float levelAdjust = 1.0f / std::sqrt(numSources);  // Maintain RMS level
        for (size_t j = 0; j < outputBuffer.size(); j++) {
            StereoSample sample = tempBuffer[j];
            sample.l *= levelAdjust * (1.0f - source.panning);
            sample.r *= levelAdjust * (1.0f + source.panning);

            outputBuffer[j].l += sample.l;
            outputBuffer[j].r += sample.r;
        }
    }
}
```

## Performance Optimization

### Memory Pool Sizing
```cpp
// Pool sizes tuned for typical usage patterns
constexpr size_t MAX_VOICES = 64;              // Synth voices
constexpr size_t MAX_VOICE_SAMPLES = 32;       // Sample voices
constexpr size_t MAX_TIME_STRETCHERS = 16;     // Time-stretch instances
constexpr size_t MAX_UNISON_SOURCES = 256;     // Unison voice sources
```

### Voice Allocation Strategies
```cpp
// Allocation preference order for optimal performance
enum VoiceAllocationStrategy {
    PREFER_UNUSED_VOICES,      // Reuse voices that finished naturally
    PREFER_FINISHED_VOICES,    // Use voices in release phase
    CULL_LOWEST_PRIORITY,      // Force voice stealing
    ALLOCATION_FAILED          // No voices available
};
```

### CPU Load Balancing
```cpp
void AudioEngine::balanceVoiceLoad() {
    int32_t activeVoices = getTotalActiveVoices();

    if (activeVoices > HIGH_VOICE_COUNT_THRESHOLD) {
        // Reduce quality for performance
        enableVoiceCulling = true;
        maxUnisonVoices = 4;  // Reduce max unison

        // Increase culling threshold
        voiceCullAmplitudeThreshold *= 2;
    } else if (activeVoices < LOW_VOICE_COUNT_THRESHOLD) {
        // Restore quality when CPU allows
        enableVoiceCulling = false;
        maxUnisonVoices = 8;
        voiceCullAmplitudeThreshold = DEFAULT_CULL_THRESHOLD;
    }
}
```

## Debug & Monitoring

### Voice Statistics
```cpp
struct VoiceStats {
    int32_t totalVoices;
    int32_t synthVoices;
    int32_t sampleVoices;
    int32_t timeStretcherVoices;
    int32_t cullEvents;
    float averagePriority;
};

VoiceStats getVoiceStats() {
    VoiceStats stats = {};
    for (Sound* sound : sounds) {
        stats.totalVoices += sound->voices.size();
        // ... calculate other metrics
    }
    return stats;
}
```

### Voice Priority Visualization
```cpp
#if VOICE_DEBUG_DISPLAY
void displayVoicePriorities() {
    for (Sound* sound : sounds) {
        for (Voice& voice : sound->voices) {
            int32_t priority = voice.getPriorityRating();
            uint8_t brightness = (priority >> 16) & 0xFF;

            // Display on LED matrix
            setLED(voice.note, brightness);
        }
    }
}
#endif
```

---

**Related Documentation:**
- [Audio Engine](audio-engine.md) - Voice rendering integration
- [Sample Streaming](sample-streaming.md) - VoiceSample cluster loading
- [Playback System](playback-system.md) - Musical timing coordination
