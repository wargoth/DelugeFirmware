# MIDI Processing System

**Bidirectional MIDI communication** - Full-duplex MIDI I/O with real-time processing and device management.

## Key Concepts

**MidiEngine** - Central MIDI coordination for sequencing and external control
- 🎹 **Real-time Processing**: MIDI events within audio callback constraints
- 🔌 **Device Management**: Auto-detection of USB/DIN MIDI devices
- ⏰ **Clock Sync**: Master/slave synchronization with external gear
- 🎛️ **MIDI Follow**: Real-time parameter control via CC messages
- 📡 **Bidirectional**: Full-duplex DIN + USB MIDI communication

**Performance**: Sub-millisecond MIDI latency with intelligent buffering

## MIDI Engine Core

### Central Processing Hub
```cpp
class MidiEngine {
    void checkIncomingSerialMidi();    // Process incoming MIDI bytes
    void flushMIDI();                  // Output buffered MIDI data
    void sendMidi(uint8_t statusByte, uint8_t data1 = 0, uint8_t data2 = 0,
                  int32_t filter = MIDI_CHANNEL_NONE, bool sendUSB = true);

    // Clock and transport
    bool anythingInOutputBuffer();
    void sendClock(bool sendUSB = true, int32_t howMany = 1);
    void sendStart(bool sendUSB = true);
    void sendStop(bool sendUSB = true);
    void sendContinue(bool sendUSB = true);
};
```

### Device Management System
```cpp
class MidiDevice {
    String name;
    int32_t connectionFlags;          // DEVICE_HAS_PORTS, etc.
    DevicePortNames ports[MAX_NUM_PORTS];

    void sendCC(int32_t cc, int32_t value, int32_t channel);
    void sendNote(int32_t note, int32_t velocity, bool on, int32_t channel);
    bool worthReading();              // Check if device has data
};

class ConnectedUSBMidiDevice : public MidiDevice {
    uint8_t maxPortConnected;
    USBDeviceDescriptor device;

    void sendMessage(uint8_t statusByte, uint8_t data1, uint8_t data2, int32_t port = 0);
};
```

## MIDI Input Processing

### Real-time Input Processing

```cpp
void MidiEngine::checkIncomingSerialMidi() {
    // Process incoming MIDI bytes in real-time
    while (midiInputBuffer.numElementsStored > 0) {
        uint8_t byte = midiInputBuffer.read();

        // Handle running status and system messages
        if (byte >= 0x80) {
            // Status byte - new message
            currentMidiInputStatus = byte;
            midiInputState = WAITING_FOR_DATA_BYTE;
        } else {
            // Data byte - process according to current status
            processMidiDataByte(byte);
        }
    }
}
```

### Message Type Handling

```cpp
void processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    uint8_t messageType = status & 0xF0;
    uint8_t channel = status & 0x0F;

    switch (messageType) {
        case 0x90:  // Note On
            handleNoteOn(channel, data1, data2);
            break;

        case 0x80:  // Note Off
            handleNoteOff(channel, data1, data2);
            break;

        case 0xB0:  // Control Change
            handleControlChange(channel, data1, data2);
            break;

        case 0xE0:  // Pitch Bend
            handlePitchBend(channel, data1, data2);
            break;

        case 0xC0:  // Program Change
            handleProgramChange(channel, data1);
            break;

        case 0xA0:  // Aftertouch
            handleAftertouch(channel, data1, data2);
            break;
    }
}
```

### Clock and Transport Messages

```cpp
void handleSystemRealTime(uint8_t byte) {
    switch (byte) {
        case 0xF8:  // Clock pulse
            if (playbackHandler.playState & PLAYSTATE_FLAG_STOPPED) {
                // Count incoming clocks to detect tempo
                incomingClockCounter++;
                calculateIncomingTempo();
            } else {
                // Sync to incoming clock
                syncToIncomingClock();
            }
            break;

        case 0xFA:  // Start
            if (midiClockInStatus == MIDI_CLOCK_IN_SLAVE) {
                playbackHandler.playButtonPressed();
            }
            resetIncomingClockCounter();
            break;

        case 0xFB:  // Continue
            if (midiClockInStatus == MIDI_CLOCK_IN_SLAVE) {
                playbackHandler.playButtonPressed();
            }
            break;

        case 0xFC:  // Stop
            if (midiClockInStatus == MIDI_CLOCK_IN_SLAVE) {
                playbackHandler.stopPlayback();
            }
            break;
    }
}
```

## MIDI Output System

### Message Queueing

```cpp
class MidiOutputBuffer {
    struct MidiMessage {
        uint8_t status;
        uint8_t data1;
        uint8_t data2;
        uint32_t timestamp;
        int32_t filter;
        bool sendUSB;
    };

    CircularBuffer<MidiMessage> buffer;

    void enqueueMessage(uint8_t status, uint8_t data1, uint8_t data2,
                       int32_t filter = MIDI_CHANNEL_NONE, bool sendUSB = true);
    void flushBuffer();
};
```

### Output Filtering

```cpp
bool shouldSendMidiMessage(const MidiMessage& msg, MidiDevice* device) {
    // Channel filtering
    if (msg.filter != MIDI_CHANNEL_NONE) {
        if (device->sendChannelA != msg.filter && device->sendChannelB != msg.filter) {
            return false;
        }
    }

    // Device-specific filtering
    if (!device->sendClock && isMidiClock(msg)) {
        return false;
    }

    if (!device->sendMCM && isMidiControlMessage(msg)) {
        return false;
    }

    return true;
}
```

### Clock Generation

```cpp
void MidiEngine::sendClock(bool sendUSB, int32_t howMany) {
    if (midiClockOutStatus == MIDI_CLOCK_OUT_OFF) {
        return;
    }

    for (int32_t i = 0; i < howMany; i++) {
        // Send to DIN MIDI
        sendMidiDIN(0xF8);

        // Send to USB devices
        if (sendUSB) {
            for (ConnectedUSBMidiDevice& device : connectedUSBMidiDevices) {
                if (device.sendClock) {
                    device.sendMessage(0xF8, 0, 0);
                }
            }
        }
    }

    midiClockOutTicksSinceStart++;
}
```

## MIDI Follow System

### CC Parameter Mapping

```cpp
struct MIDIKnob {
    uint8_t midiCC;
    uint8_t from7Bit;  // Min CC value
    uint8_t to7Bit;    // Max CC value

    // Deluge parameter mapping
    int32_t paramID;
    int32_t paramKind;

    bool relative;     // Relative vs absolute CC mode
    int32_t lastValue; // For relative mode
};

class MidiFollow {
    std::array<MIDIKnob, NUM_MIDI_FOLLOW_KNOBS> knobs;
    bool channelOrZone;  // Per-channel or per-zone mapping

    void processCCMessage(uint8_t channel, uint8_t cc, uint8_t value);
    void updateParameter(const MIDIKnob& knob, uint8_t value);
};
```

### Real-time Parameter Control

```cpp
void MidiFollow::processCCMessage(uint8_t channel, uint8_t cc, uint8_t value) {
    // Find matching knob mapping
    for (MIDIKnob& knob : knobs) {
        if (knob.midiCC != cc) continue;

        // Apply channel filtering
        if (!matchesChannelFilter(channel, knob)) continue;

        // Scale CC value to parameter range
        int32_t scaledValue = scaleCCValue(knob, value);

        // Apply to current context (clip, kit row, etc.)
        ModelStackWithAutoParam* modelStack = getCurrentAutoParamModelStack(knob.paramID);
        if (modelStack && modelStack->autoParam) {
            // Set parameter value with optional smoothing
            if (knob.relative) {
                int32_t delta = calculateRelativeDelta(knob, value);
                modelStack->autoParam->adjustValue(delta);
            } else {
                modelStack->autoParam->setValue(scaledValue, modelStack);
            }

            // Create Action for undo support
            Action* action = actionLogger.getNewAction(ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE);
            if (action) {
                action->recordParameterChange(modelStack, knob.paramID, scaledValue);
            }
        }
    }
}
```

### Device Configuration Loading

```cpp
Error MidiFollow::loadDeviceDefinition(const String& devicePath) {
    Error error = storageManager.openXMLFile(&filePointer, devicePath.get(), "device", "");
    if (error != Error::NONE) return error;

    // Parse device XML
    while (*(filePointer.currentPos) != '<') {
        char const* tagName;
        error = filePointer.readTagName(&tagName);
        if (error != Error::NONE) break;

        if (!strcmp(tagName, "knob")) {
            MIDIKnob knob;
            knob.midiCC = filePointer.readIntAttributeOrDefault("cc", 255);
            knob.paramID = filePointer.readIntAttributeOrDefault("param", -1);
            knob.from7Bit = filePointer.readIntAttributeOrDefault("min", 0);
            knob.to7Bit = filePointer.readIntAttributeOrDefault("max", 127);
            knob.relative = filePointer.readBoolAttributeOrDefault("relative", false);

            if (knob.midiCC < 128 && knob.paramID != -1) {
                knobs[numKnobs++] = knob;
            }
        }
    }

    filePointer.close();
    return Error::NONE;
}
```

## Clock Synchronization

### Master Clock Mode

```cpp
void AudioEngine::routine() {
    // Generate MIDI clock based on internal timing
    if (midiEngine.midiClockOutStatus == MIDI_CLOCK_OUT_ON) {
        uint32_t currentTime = getSystemTime();
        uint32_t timeSinceLastClock = currentTime - lastMidiClockTime;

        // Calculate clocks needed based on tempo
        uint32_t clockInterval = calculateClockInterval(currentTempo);

        if (timeSinceLastClock >= clockInterval) {
            int32_t clocksToSend = timeSinceLastClock / clockInterval;
            midiEngine.sendClock(true, clocksToSend);
            lastMidiClockTime = currentTime;
        }
    }

    // ... audio processing
}
```

### Slave Clock Mode

```cpp
void syncToIncomingClock() {
    if (midiClockInStatus != MIDI_CLOCK_IN_SLAVE) return;

    uint32_t currentTime = getSystemTime();
    uint32_t timeSinceLastClock = currentTime - lastIncomingClockTime;

    // Calculate incoming tempo
    if (incomingClockCounter >= 24) {  // One beat = 24 MIDI clocks
        float incomingTempo = 60000000.0f / (timeSinceLastBeat * 24);  // BPM

        // Smooth tempo changes to avoid jitter
        float smoothedTempo = (currentSlaveTempo * 0.9f) + (incomingTempo * 0.1f);
        currentSlaveTempo = smoothedTempo;

        // Adjust internal playback rate
        playbackHandler.adjustTempoToMidiClock(smoothedTempo);

        incomingClockCounter = 0;
        lastBeatTime = currentTime;
    }

    lastIncomingClockTime = currentTime;
}
```

## Device Management

### USB MIDI Device Detection

```cpp
void detectUSBMidiDevices() {
    // Scan USB bus for MIDI class devices
    for (uint8_t deviceIndex = 0; deviceIndex < MAX_NUM_USB_DEVICES; deviceIndex++) {
        USBDeviceDescriptor* device = getUSBDevice(deviceIndex);
        if (!device || !device->isConnected()) continue;

        // Check for MIDI class (0x01) or vendor-specific MIDI
        if (device->deviceClass == USB_CLASS_AUDIO ||
            device->subClass == USB_SUBCLASS_MIDISTREAMING ||
            isKnownMidiDevice(device->vendorID, device->productID)) {

            // Add to connected devices list
            ConnectedUSBMidiDevice newDevice;
            newDevice.device = *device;
            newDevice.name = getDeviceName(device);

            connectedUSBMidiDevices.push_back(newDevice);

            // Load device-specific configuration if available
            loadDeviceConfiguration(&newDevice);
        }
    }
}
```

### Device Configuration

```cpp
struct DeviceConfiguration {
    String manufacturer;
    String model;
    uint16_t vendorID;
    uint16_t productID;

    // Default MIDI channels
    uint8_t defaultInputChannel;
    uint8_t defaultOutputChannel;

    // Capabilities
    bool supportsMTC;       // MIDI Time Code
    bool supportsMMC;       // MIDI Machine Control
    bool supportsSysEx;     // System Exclusive
    bool hasDisplay;        // Has display for feedback

    // Controller mappings
    std::vector<MIDIKnob> knobMappings;
    std::vector<MIDIPad> padMappings;
};
```

## System Integration

### Action Logger Integration

```cpp
void recordMidiParameterChange(int32_t paramID, int32_t oldValue, int32_t newValue) {
    Action* action = actionLogger.getNewAction(ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE);
    if (!action) return;

    void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(
        sizeof(ConsequenceParamChange));
    ConsequenceParamChange* consequence = new (consMemory)
        ConsequenceParamChange(paramID, oldValue, newValue);

    action->addConsequence(consequence);
}
```

### UI Integration

```cpp
void updateMidiParameterDisplay(int32_t paramID, int32_t value) {
    // Update numeric display
    if (display && getCurrentUI()->getParameterName(paramID)) {
        char paramName[50];
        getCurrentUI()->getParameterName(paramID, paramName);

        display->displayPopup(paramName);

        // Show parameter value
        char valueStr[20];
        getCurrentUI()->getParameterValueString(paramID, value, valueStr);
        display->setText(valueStr);
    }

    // Update LED rings/displays on hardware
    if (getCurrentUI()->isParameterDisplayedOnLED(paramID)) {
        updateParameterLEDs(paramID, value);
    }
}
```

## Performance Optimization

### Buffer Management

```cpp
class MidiBufferManager {
    // Separate buffers for input/output to avoid contention
    CircularBuffer<uint8_t> inputBuffer{1024};
    CircularBuffer<MidiMessage> outputBuffer{256};

    // Priority queue for time-critical messages
    PriorityQueue<MidiMessage> priorityOutput{64};

    void flushBuffers();
    void prioritizeMessage(const MidiMessage& msg);
};
```

### Real-time Constraints

```cpp
void processIncomingMidiRealTime() {
    // Process MIDI within audio callback constraints
    uint32_t startTime = getSystemTime();
    constexpr uint32_t MAX_MIDI_PROCESSING_TIME = 100;  // 100μs max

    while (midiInputBuffer.numElementsStored > 0) {
        uint32_t elapsed = getSystemTime() - startTime;
        if (elapsed > MAX_MIDI_PROCESSING_TIME) {
            // Defer remaining processing to next callback
            break;
        }

        processMidiMessage(midiInputBuffer.read());
    }
}
```

### Memory Management

```cpp
void optimizeMidiMemoryUsage() {
    // Preallocate MIDI event objects to avoid real-time allocation
    static std::array<MidiEvent, MAX_CONCURRENT_MIDI_EVENTS> eventPool;
    static uint32_t nextFreeEvent = 0;

    // Recycle completed events
    for (MidiEvent& event : eventPool) {
        if (event.isComplete()) {
            event.reset();
        }
    }
}
```

## Configuration and Settings

### MIDI Settings

```cpp
struct MidiSettings {
    // Clock settings
    MidiClockInStatus clockInStatus = MIDI_CLOCK_IN_OFF;
    MidiClockOutStatus clockOutStatus = MIDI_CLOCK_OUT_OFF;

    // Channel assignments
    uint8_t defaultInputChannel = MIDI_CHANNEL_OMNI;
    uint8_t defaultOutputChannel = 1;

    // Device settings
    bool sendMidiClock = true;
    bool receiveMiddiNotes = true;
    bool sendMidiNotes = true;
    bool followMidiCC = false;

    // Timing settings
    uint8_t clockOutPPQN = 24;  // Pulses per quarter note
    uint8_t clockInSmoothingFactor = 4;

    void saveToFile();
    void loadFromFile();
};
```

### Follow Mode Configuration

```cpp
void configureMidiFollow() {
    // Load follow mappings from XML
    String followPath = "/SETTINGS/MIDIFollow.XML";
    if (storageManager.fileExists(followPath.get())) {
        midiFollow.loadConfiguration(followPath);
    }

    // Apply channel filtering
    for (MIDIKnob& knob : midiFollow.knobs) {
        if (knob.channelFilter == MIDI_CHANNEL_CURRENT_CLIP) {
            // Dynamic channel assignment
            knob.activeChannel = getCurrentClipMidiChannel();
        }
    }
}
```

## Error Handling

### MIDI Communication Errors

```cpp
void handleMidiError(MidiError error) {
    switch (error) {
        case MIDI_ERROR_BUFFER_OVERFLOW:
            // Clear buffers and restart
            midiInputBuffer.clear();
            midiOutputBuffer.clear();
            logError("MIDI buffer overflow");
            break;

        case MIDI_ERROR_INVALID_MESSAGE:
            // Skip malformed message, continue processing
            skipCurrentMidiMessage();
            logWarning("Invalid MIDI message received");
            break;

        case MIDI_ERROR_DEVICE_DISCONNECTED:
            // Remove from active devices
            removeDisconnectedDevice(failedDevice);
            display->displayPopup("MIDI DEVICE DISCONNECTED");
            break;

        case MIDI_ERROR_CLOCK_SYNC_LOST:
            // Fall back to internal clock
            midiClockInStatus = MIDI_CLOCK_IN_OFF;
            display->displayPopup("CLOCK SYNC LOST");
            break;
    }
}
```

### USB Communication Recovery

```cpp
void recoverUSBMidiConnection() {
    // Attempt to reinitialize USB MIDI
    for (auto& device : connectedUSBMidiDevices) {
        if (!device.isResponding()) {
            // Try to reestablish connection
            device.reconnect();

            if (!device.isConnected()) {
                // Mark for removal
                device.markForRemoval();
                continue;
            }

            // Send identity request
            device.sendSysEx({0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7});
        }
    }

    // Clean up failed devices
    connectedUSBMidiDevices.erase(
        std::remove_if(connectedUSBMidiDevices.begin(),
                       connectedUSBMidiDevices.end(),
                       [](const auto& device) { return device.isMarkedForRemoval(); }),
        connectedUSBMidiDevices.end());
}
```

## Debug and Monitoring

### MIDI Event Logging

```cpp
#ifdef MIDI_DEBUG
void logMidiEvent(const MidiMessage& msg) {
    char logBuffer[100];
    sprintf(logBuffer, "MIDI: Ch%d %s %d %d",
            msg.channel,
            getMidiMessageTypeName(msg.status),
            msg.data1, msg.data2);
    D_PRINTLN(logBuffer);
}
#endif
```

### Performance Monitoring

```cpp
struct MidiStats {
    uint32_t messagesReceived;
    uint32_t messagesSent;
    uint32_t bufferOverflows;
    uint32_t clockPulsesSent;
    uint32_t clockPulsesReceived;
    float averageProcessingTime;
    float clockJitter;

    void reset();
    void updateStats(uint32_t processingTime);
};
```

## Best Practices

### MIDI Integration Guidelines

1. **Real-time Safety**: Process MIDI within audio callback time constraints
2. **Buffer Management**: Use circular buffers with overflow protection
3. **Error Recovery**: Gracefully handle device disconnections and errors
4. **Clock Stability**: Implement smooth tempo transitions for external sync
5. **Memory Efficiency**: Preallocate MIDI event objects to avoid real-time allocation

### Performance Recommendations

1. **Batch Processing**: Group MIDI operations when possible
2. **Priority Queuing**: Prioritize time-critical messages (clocks, notes)
3. **Filtering**: Apply channel and message type filtering early
4. **Monitoring**: Track performance metrics for optimization

## Conclusion

The Deluge's MIDI system provides comprehensive bidirectional MIDI communication with:

- **Real-time Processing**: Sub-millisecond latency MIDI processing
- **Device Integration**: Automatic detection and configuration of MIDI devices
- **Clock Synchronization**: Robust master/slave clock synchronization
- **Parameter Control**: Sophisticated MIDI Follow system for real-time control
- **Error Recovery**: Robust error handling and connection recovery

The architecture balances real-time performance requirements with comprehensive MIDI functionality, enabling seamless integration with external MIDI gear and controllers.

## Related Documentation

- [Audio Engine Architecture](audio-engine.md)
- [Playback System Architecture](playback-system.md)
- [Voice Management System](voice-management.md)
- [Sample Streaming System](sample-streaming.md)
