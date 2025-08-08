# Deluge Firmware File Format Architecture

## Song File Structure (XML/JSON)

### Root Element Structure
- **Root Element**: `<song>` with firmware version and compatibility metadata
- **Preview Data**: 144-pad RGB preview stored as hex strings for browser display
- **Navigation State**: View positions (`xScroll`, `yScroll`, `xZoom`, `yZoom`) for each UI context
- **Global Settings**: Tempo (`timePerTimerTick`), key signature, swing, triplets mode
- **Clip Arrays**: `sessionClips` and `arrangementOnlyClips` with nested clip data
- **Instrument Definitions**: Embedded or referenced preset data per output type

## Preset File Structure

### Synth Presets
- **Root**: `<sound>` with oscillators, envelopes, LFOs, effects chain
- **Parameters**: Nested parameter trees with automation data

### Kit Presets
- **Root**: `<kit>` with drum sound definitions and routing
- **Sound Slots**: Individual drum sounds with sample references

### MIDI Presets
- **Root**: `<midiInstrument>` with CC mappings and device definitions
- **Device Mapping**: External controller configurations

### Parameter Serialization
- **Structure**: Nested `<defaultParams>` with automation data
- **Types**: String, integer, hex values, boolean flags, nested structures

## Serialization Patterns

### XML Serializer
- **Interface**: `XMLSerializer` with `writeAttribute()`, `writeTag()`, structured output
- **Attributes**: Type-safe attribute writing with proper escaping
- **Nesting**: Proper tag opening/closing with indentation

### JSON Serializer
- **Modern Format**: `JsonSerializer` for modern format support
- **Compatibility**: Maintains backward compatibility with XML format

### File Safety
- **Atomic Operations**: Temporary file creation, atomic rename operations for crash protection
- **Error Handling**: Proper cleanup on write failures

## File I/O Pattern

### Standard Write Pattern
```cpp
Error error = StorageManager::createXMLFile(filePath, smSerializer, overwrite);
if (error != Error::NONE) return error;

Serializer& writer = GetSerializer();
writer.writeOpeningTagBeginning("song");
writer.writeFirmwareVersion();
writer.writeAttribute("tempo", song->tempo);
// ... write content
writer.writeClosingTag("song");
error = writer.closeFileAfterWriting(filePath, header, footer);
```

### Sample Loading Pattern
```cpp
// Always check for SD card access permissions
if (sdRoutineLock) return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;

// Use AudioFileManager for all file operations
auto result = audioFileManager.getAudioFileFromFilename(path, mayReadCard);
```

## Configuration Files (SETTINGS/ folder)

### Core Configuration Files
- **MIDIDevices.XML**: Device definitions and port configurations
- **MIDIFollow.XML**: CC mapping assignments for MIDI follow mode
- **PerformanceView.XML**: Performance view FX column and pad assignments
- **CommunityFeatures.XML**: Runtime feature flags and community settings

### MIDI Device Definitions
- **Location**: `MIDI_DEVICES/DEFINITION/` folder
- **Purpose**: External device CC label mappings
- **Format**: XML with device-specific CC definitions
