# Sample Streaming System

**Asynchronous storage engine** - Seamless sample playback from SD card with intelligent caching and predictive loading.

## Key Concepts

**Cluster-Based Architecture** - 32KB chunks aligned with filesystem for optimal I/O
- 📁 **Clusters**: 32KB sample data blocks aligned with FAT32 clusters
- 🧠 **Multi-Level Cache**: Sample, percussion, and wavetable caches
- ⚡ **Asynchronous Loading**: Non-blocking I/O prevents audio dropouts
- 🎯 **Predictive Loading**: Time-stretcher-aware preloading

**Location**: `src/deluge/storage/audio/audio_file_manager.cpp`

## Architecture Overview

### Cluster System
```cpp
// 32KB clusters aligned with filesystem blocks
constexpr int32_t CLUSTER_SIZE = 32768;  // 32KB

enum ClusterType {
    SAMPLE,                    // Raw audio data
    PERC_CACHE_FORWARDS,      // Time-stretched data (forward)
    PERC_CACHE_REVERSED       // Time-stretched data (reversed)
};

class Cluster {
    uint8_t* data;             // 32KB data buffer
    Sample* sample;            // Parent sample reference
    int32_t index;             // Cluster index within sample
    bool loaded;               // Load status
    uint32_t lastAccessTime;   // LRU tracking
};
```

### Sample Playback Coordination
```cpp
class SamplePlaybackGuide {
    uint32_t startPlaybackAtByte;          // Sample start position
    uint32_t endPlaybackAtByte;            // Sample end position
    uint32_t sequenceSyncLengthTicks;      // 0 = no tempo sync
    int32_t sequenceSyncStartedAtTick;     // Sync reference point

    void setupPlaybackBounds(bool reversed);
    uint64_t getSyncedNumSamplesIn();      // Tempo-aware position
};
```

## Asynchronous Loading System

### Load Request Queue
```cpp
class AudioFileManager {
    struct ClusterLoadRequest {
        Cluster* cluster;
        int32_t priority;          // Higher = more urgent
        ClusterType type;
        uint32_t requestTime;
    };

    std::queue<ClusterLoadRequest> loadQueue;

    void addClusterToQueue(Cluster* cluster, int32_t priority = NORMAL_PRIORITY);
    void loadAnyEnqueuedClusters(int32_t maxNum = 2);  // Limit per audio frame
};
```

### Loading Modes
```cpp
Error AudioFileManager::getAudioFileFromFilename(
    String const& path,
    bool mayReadCard,           // true = blocking, false = async
    AudioFile** getAudioFile,
    FilePointer* filePointer) {

    if (mayReadCard) {
        // Synchronous load - blocks until complete
        return loadFileImmediate(path, getAudioFile);
    } else {
        // Asynchronous load - queue for background loading
        queueFileForLoading(path);
        return Error::NONE;
    }
}
```

## Multi-Level Caching

### Cache Hierarchy
```cpp
class SampleCache {
    static constexpr int32_t MAX_CACHE_SIZE = 2 * 1024 * 1024;  // 2MB

    struct CacheEntry {
        Sample* sample;
        int32_t clusterIndex;
        uint8_t* data;
        uint32_t lastAccessTime;
        bool dirty;
        int32_t priority;
    };

    std::vector<CacheEntry> entries;
    int32_t currentSize;

    uint8_t* getClusterData(Sample* sample, int32_t clusterIndex);
    void evictLeastRecentlyUsed(int32_t bytesNeeded);
};
```

### LRU with Priority Boosting
```cpp
void SampleCache::evictLeastRecentlyUsed(int32_t bytesNeeded) {
    int32_t bytesFreed = 0;

    // Sort by access time, but preserve high-priority entries
    std::sort(entries.begin(), entries.end(),
              [](const CacheEntry& a, const CacheEntry& b) {
                  // High priority entries stay longer
                  if (a.priority != b.priority) {
                      return a.priority < b.priority;
                  }
                  return a.lastAccessTime < b.lastAccessTime;
              });

    auto it = entries.begin();
    while (it != entries.end() && bytesFreed < bytesNeeded) {
        // Skip entries currently being played
        if (isCurrentlyPlaying(*it)) {
            ++it;
            continue;
        }

        bytesFreed += CLUSTER_SIZE;
        deallocateCluster(it->data);
        it = entries.erase(it);
    }
}
```

## Predictive Loading System

### Time-Stretcher Integration
```cpp
class TimeStretcher {
    void updateClustersForPercLookahead(VoiceSample* voiceSample,
                                       int32_t numSamples,
                                       int32_t clusterLoadInstruction);

    // Predict which clusters will be needed
    std::vector<int32_t> predictNeededClusters(int32_t lookaheadSamples) {
        std::vector<int32_t> clusters;

        // Calculate reading speed based on time-stretch ratio
        float readSpeed = getTimeStretchRatio();
        int32_t samplesAhead = lookaheadSamples * readSpeed;

        // Convert to cluster indices
        int32_t startCluster = getCurrentClusterIndex();
        int32_t endCluster = (getCurrentSamplePos() + samplesAhead) / CLUSTER_SIZE;

        for (int32_t i = startCluster; i <= endCluster; i++) {
            clusters.push_back(i);
        }

        return clusters;
    }
};
```

### Predictive Queue Management
```cpp
void AudioFileManager::updatePredictiveLoading() {
    // Analyze active time-stretchers for upcoming needs
    for (TimeStretcher* ts : activeTimeStretchers) {
        if (!ts->isActive()) continue;

        // Get prediction for next 2048 samples (~46ms at 44.1kHz)
        auto predictedClusters = ts->predictNeededClusters(2048);

        for (int32_t clusterIndex : predictedClusters) {
            if (!isClusterLoaded(ts->getSample(), clusterIndex)) {
                // Queue with prediction priority
                Cluster* cluster = ts->getSample()->getCluster(clusterIndex);
                addClusterToQueue(cluster, PREDICTION_PRIORITY);
            }
        }
    }
}
```

## Sync-Compensated Streaming

### Tempo-Aware Positioning
```cpp
uint64_t SamplePlaybackGuide::getSyncedNumSamplesIn() {
    if (!sequenceSyncLengthTicks) {
        return 0;  // No syncing - use raw position
    }

    // Calculate position within sync window
    int32_t ticksIn = playbackHandler.lastSwungTickActioned - sequenceSyncStartedAtTick;
    if (ticksIn < 0) ticksIn = 0;

    // Convert ticks to samples accounting for current tempo
    uint64_t samplesIn = (uint64_t)ticksIn * audioFileHolder->audioFile->sampleRate
                        / currentSong->getTimePerTimerTickFloat();

    return samplesIn;
}
```

### Cross-Cluster Boundary Handling
```cpp
void VoiceSample::renderAcrossClusterBoundary(std::span<StereoSample> outputBuffer) {
    int32_t samplesRemaining = outputBuffer.size();
    int32_t bufferPos = 0;

    while (samplesRemaining > 0) {
        int32_t currentCluster = getCurrentClusterIndex();
        int32_t samplesInCluster = getSamplesRemainingInCluster();
        int32_t samplesToRender = std::min(samplesRemaining, samplesInCluster);

        // Render from current cluster
        auto subBuffer = outputBuffer.subspan(bufferPos, samplesToRender);
        if (isClusterLoaded(sample, currentCluster)) {
            renderFromCluster(subBuffer, currentCluster);
        } else {
            // Queue missing cluster with high priority
            queueClusterWithHighPriority(currentCluster);
            renderSilence(subBuffer);  // Graceful degradation
        }

        // Advance to next section
        bufferPos += samplesToRender;
        samplesRemaining -= samplesToRender;
        advancePlayPosition(samplesToRender);
    }
}
```

## Memory Management

### Bounded Memory Usage
```cpp
// Configurable memory limits
struct CacheConfig {
    int32_t maxSampleCacheSize = 2 * 1024 * 1024;      // 2MB sample data
    int32_t maxPercCacheSize = 1 * 1024 * 1024;        // 1MB percussion cache
    int32_t maxWavetableCacheSize = 512 * 1024;        // 512KB wavetables
    int32_t maxPendingLoads = 64;                       // Queue size limit

    float memoryPressureThreshold = 0.85f;              // Start eviction at 85%
};

void handleMemoryPressure() {
    if (getCurrentMemoryUsage() > config.memoryPressureThreshold) {
        // 1. Evict non-critical cached clusters
        sampleCache.evictNonCritical();

        // 2. Reduce time-stretcher lookahead
        for (TimeStretcher* ts : activeTimeStretchers) {
            ts->reduceLookahead();
        }

        // 3. Force voice cleanup to free sample references
        AudioEngine::forceVoiceCleanup();
    }
}
```

### Cluster Memory Allocator
```cpp
class ClusterAllocator {
    uint8_t* allocateCluster() {
        // Use GeneralMemoryAllocator for cluster data
        void* memory = GeneralMemoryAllocator::get().allocLowSpeed(CLUSTER_SIZE);
        return static_cast<uint8_t*>(memory);
    }

    void deallocateCluster(uint8_t* cluster) {
        GeneralMemoryAllocator::get().dealloc(cluster);
    }

    void compactMemory() {
        // Defragment when memory becomes fragmented
        GeneralMemoryAllocator::get().defragment();
    }
};
```

## Error Handling & Recovery

### Missing Cluster Fallback
```cpp
void VoiceSample::handleMissingCluster(std::span<StereoSample> outputBuffer) {
    if (isInAttackPhase()) {
        // Critical phase - silence to avoid clicks
        std::fill(outputBuffer.begin(), outputBuffer.end(), StereoSample{0, 0});
    } else if (hasValidPreviousData()) {
        // Non-critical - extend previous sample
        extendPreviousSample(outputBuffer);
    } else {
        // Fallback - exponential fade to silence
        fadeToSilence(outputBuffer);
    }

    // Always queue missing cluster with highest priority
    queueClusterWithHighPriority(getCurrentClusterIndex());
}
```

### SD Card Error Recovery
```cpp
void AudioFileManager::handleSDCardError(Error error) {
    switch (error) {
        case Error::SD_CARD_NOT_PRESENT:
            // Pause all sample playback
            pauseAllSamplePlayback();
            display->displayPopup("NO SD CARD");
            break;

        case Error::FILE_CORRUPTED:
            // Skip corrupted section if possible
            skipCorruptedCluster();
            logError("Sample file corrupted");
            break;

        case Error::SD_CARD_FAT_ERROR:
            // Attempt remount, fallback to cached data
            if (attemptSDCardRemount() != Error::NONE) {
                fallbackToCachedDataOnly();
            }
            break;
    }
}
```

## Performance Optimization

### I/O Batching
```cpp
void AudioFileManager::loadAnyEnqueuedClusters(int32_t maxNum) {
    // Group requests by file for sequential I/O
    std::sort(pendingLoads.begin(), pendingLoads.end(),
              [](const auto& a, const auto& b) {
                  if (a.sample != b.sample) {
                      return a.sample < b.sample;
                  }
                  return a.clusterIndex < b.clusterIndex;  // Sequential within file
              });

    int32_t loaded = 0;
    for (const auto& request : pendingLoads) {
        if (loaded >= maxNum) break;

        // Batch multiple clusters from same file
        loadClusterFromFile(request);
        loaded++;
    }
}
```

### Filesystem Alignment
```cpp
int32_t Sample::getOptimalClusterIndex(int32_t bytePosition) {
    // Align with FAT32 cluster boundaries for optimal I/O
    int32_t filesystemClusterSize = getSDCardClusterSize();
    int32_t alignedPosition = (bytePosition / filesystemClusterSize) * filesystemClusterSize;

    return alignedPosition / DELUGE_CLUSTER_SIZE;
}
```

## Debug & Monitoring

### Streaming Statistics
```cpp
struct StreamingStats {
    int32_t cacheHits;
    int32_t cacheMisses;
    int32_t clustersLoaded;
    int32_t clustersEvicted;
    float averageLoadTime;
    int32_t memoryUsage;

    float getCacheHitRatio() const {
        return (float)cacheHits / (cacheHits + cacheMisses);
    }
};

#if SAMPLE_STREAMING_DEBUG
void logCacheAccess(Sample* sample, int32_t clusterIndex, bool hit) {
    D_PRINTLN("Cache %s: sample=%p cluster=%d",
              hit ? "HIT" : "MISS", sample, clusterIndex);
}
#endif
```

### Performance Monitoring
```cpp
void updateStreamingPerformance() {
    stats.cacheHitRatio = calculateCacheHitRatio();
    stats.memoryUsage = getTotalCacheMemoryUsage();

    // Adjust loading strategy based on performance
    if (stats.cacheHitRatio < 0.90f) {
        // Poor cache performance - increase lookahead
        increasePredictiveLookahead();
    }

    if (stats.averageLoadTime > LOAD_TIME_THRESHOLD) {
        // Slow loading - prioritize more aggressively
        increaseHighPriorityThreshold();
    }
}
```

---

**Related Documentation:**
- [Audio Engine](audio-engine.md) - Sample rendering integration
- [Voice Management](voice-management.md) - VoiceSample lifecycle
- [Playback System](playback-system.md) - Tempo sync integration
