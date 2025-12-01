/*
 * Copyright © 2025 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "controller_mode_view.h"
#include "gui/ui_timer_manager.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "model/midi/message.h"
#include "gui/l10n/l10n.h"
#include "memory/general_memory_allocator.h"
#include <string.h>
#include <algorithm>

ControllerModeView controllerModeView{};

// MIDI SysEx manufacturer ID for Deluge
constexpr uint8_t SYSEX_MANUFACTURER_ID[] = {0x00, 0x21, 0x7D}; // Example ID (Synthstrom)
constexpr uint8_t SYSEX_DEVICE_ID = 0x01; // Deluge device ID

// SysEx command types
enum class SysExCommand : uint8_t {
	DEVICE_INQUIRY = 0x01,
	SET_DISPLAY_TEXT = 0x10,
	SET_7SEG_SEGMENTS = 0x11,
	SET_OLED_PIXELS = 0x12,
	SET_LED_COLOR = 0x20,
	SET_ALL_LEDS = 0x21,
};

ControllerModeView::ControllerModeView() {
	// Initialize with default configuration
	config_ = ControllerModeConfig();

	// Clear all state
	memset(padColors_, 0, sizeof(padColors_));
	memset(buttonLEDStates_, 0, sizeof(buttonLEDStates_));
	memset(encoderLEDStates_, 0, sizeof(encoderLEDStates_));
	memset(padPressed_, 0, sizeof(padPressed_));
	memset(displayText_, 0, sizeof(displayText_));
	memset(displaySegments_, 0, sizeof(displaySegments_));

	strcpy(displayText_, "CONTROLLER MODE");
}

bool ControllerModeView::opened() {
	// Select USB device cable for controller mode communication
	// Controller mode uses USB peripheral mode (Deluge -> DAW)
	if (MIDIDeviceManager::root_usb) {
		// Get the first USB cable (cable 0)
		activeCable_ = MIDIDeviceManager::root_usb->getCable(0);
	}

	focusRegained();

	// Send identity to DAW so remote script knows Deluge is ready
	sendIdentityReply();

	// Display welcome message
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CONTROLLER_MODE));

	return true;
}

void ControllerModeView::focusRegained() {
	// Update all displays
	updatePadLEDs();
	updateButtonLEDs();
	updateDisplay();
	uiTimerManager.setTimer(TimerName::DISPLAY, 1000);
}

void ControllerModeView::graphicsRoutine() {
	// Periodic update - remote script controls everything via MIDI
}

bool ControllerModeView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                         bool drawUndefinedArea) {
	if (!image) {
		return true; // Opaque
	}

	// Render pads based on colors set by remote script via MIDI
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			// Use color set by remote script, or black if not set
			image[y][x] = padColors_[x][y];

			// Brighten if currently pressed
			if (padPressed_[x][y]) {
				image[y][x].r = std::min(255, image[y][x].r + 50);
				image[y][x].g = std::min(255, image[y][x].g + 50);
				image[y][x].b = std::min(255, image[y][x].b + 50);
			}

			occupancyMask[y][x] = 64;
		}
	}

	return true;
}

bool ControllerModeView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	// Sidebar could show button states - controlled by remote script
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		// Remote script controls sidebar LEDs too
		image[y][kDisplayWidth] = {10, 10, 10}; // Dim by default
		occupancyMask[y][kDisplayWidth] = 64;
	}

	return true;
}

void ControllerModeView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	// Display text set by remote script
	updateDisplay();
}

void ControllerModeView::updateDisplay() {
	// Display text that was set via SysEx from remote script
	if (displayText_[0] != '\0') {
		display->displayPopup(displayText_);
	}
}

//==============================================================================
// INPUT HANDLING - Send ALL inputs as MIDI
//==============================================================================

ActionResult ControllerModeView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (velocity > 0) {
		// Pad pressed - Deluge doesn't have velocity sensing, send fixed velocity
		padPressed_[x][y] = true;
		sendPadNoteOn(x, y);
	}
	else {
		// Pad released
		padPressed_[x][y] = false;
		sendPadNoteOff(x, y);
	}

	uiNeedsRendering(this);
	return ActionResult::DEALT_WITH;
}

ActionResult ControllerModeView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Special case: SHIFT+BACK exits controller mode
	if (b == BACK && on && Buttons::isShiftButtonPressed()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_EXITING_CONTROLLER_MODE));
		changeRootUI(&sessionView);
		return ActionResult::DEALT_WITH;
	}

	// ALL other buttons send MIDI - remote script decides what they do
	sendButtonMidi(b, on);

	return ActionResult::DEALT_WITH;
}

ActionResult ControllerModeView::horizontalEncoderAction(int32_t offset) {
	// Send horizontal encoder as CC
	// Use relative CC encoding (64 = no change, <64 = left, >64 = right)
	int32_t value = 64 + offset;
	value = std::max(0_i32, std::min(127_i32, value));
	sendEncoderCC(config_.encoderBaseCC + 8, value); // CC +8 for horizontal encoder
	return ActionResult::DEALT_WITH;
}

ActionResult ControllerModeView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	// Send vertical encoder as CC
	int32_t value = 64 + offset;
	value = std::max(0_i32, std::min(127_i32, value));
	sendEncoderCC(config_.encoderBaseCC + 9, value); // CC +9 for vertical encoder
	return ActionResult::DEALT_WITH;
}

void ControllerModeView::selectEncoderAction(int8_t offset) {
	// Send select encoder as CC (relative)
	int32_t value = 64 + offset;
	value = std::max(0_i32, std::min(127_i32, value));
	sendEncoderCC(config_.encoderBaseCC + 10, value); // CC +10 for select encoder
}

void ControllerModeView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	// Send gold knobs as CC (relative or absolute depending on remote script preference)
	// Using relative encoding by default
	int32_t value = 64 + offset;
	value = std::max(0_i32, std::min(127_i32, value));
	sendEncoderCC(config_.encoderBaseCC + whichModEncoder, value);
}

void ControllerModeView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	if (!activeCable_) {
		return;
	}

	// Gold knob buttons send MIDI notes (button base + encoder number)
	int32_t note = config_.buttonBaseNote + 20 + whichModEncoder; // Offset for encoder buttons
	if (note >= 0 && note <= 127) {
		MIDIMessage msg = on ? MIDIMessage::noteOn(config_.midiChannel, note, 127)
		                     : MIDIMessage::noteOff(config_.midiChannel, note, 0);
		activeCable_->sendMessage(msg);
	}
}

void ControllerModeView::modButtonAction(uint8_t whichButton, bool on) {
	if (!activeCable_) {
		return;
	}

	// Mod matrix buttons send MIDI notes
	int32_t note = config_.buttonBaseNote + 30 + whichButton; // Offset for mod buttons
	if (note >= 0 && note <= 127) {
		MIDIMessage msg = on ? MIDIMessage::noteOn(config_.midiChannel, note, 127)
		                     : MIDIMessage::noteOff(config_.midiChannel, note, 0);
		activeCable_->sendMessage(msg);
	}
}

//==============================================================================
// MIDI OUTPUT - Send hardware state to DAW
//==============================================================================

void ControllerModeView::sendPadNoteOn(int32_t x, int32_t y) {
	if (!activeCable_) {
		return;
	}

	int32_t note = padToMidiNote(x, y);
	if (note >= 0 && note <= 127) {
		// Deluge doesn't have velocity sensing - send fixed velocity 127
		MIDIMessage msg = MIDIMessage::noteOn(config_.midiChannel, note, 127);
		activeCable_->sendMessage(msg);
	}
}

void ControllerModeView::sendPadNoteOff(int32_t x, int32_t y) {
	if (!activeCable_) {
		return;
	}

	int32_t note = padToMidiNote(x, y);
	if (note >= 0 && note <= 127) {
		MIDIMessage msg = MIDIMessage::noteOff(config_.midiChannel, note, 0);
		activeCable_->sendMessage(msg);
	}
}

void ControllerModeView::sendButtonMidi(deluge::hid::Button button, bool on) {
	if (!activeCable_) {
		return;
	}

	int32_t note = buttonToMidiNote(button);
	if (note >= 0) {
		MIDIMessage msg = on ? MIDIMessage::noteOn(config_.midiChannel, note, 127)
		                     : MIDIMessage::noteOff(config_.midiChannel, note, 0);
		activeCable_->sendMessage(msg);
	}
}

void ControllerModeView::sendEncoderCC(int32_t ccNumber, int32_t value) {
	if (!activeCable_) {
		return;
	}

	if (ccNumber >= 0 && ccNumber <= 127) {
		MIDIMessage msg = MIDIMessage::cc(config_.midiChannel, ccNumber, value);
		activeCable_->sendMessage(msg);
	}
}

//==============================================================================
// MIDI INPUT - Receive LED/display commands from DAW remote script
//==============================================================================

bool ControllerModeView::noteOnReceivedForMidiLearn(MIDICable& fromCable, int32_t channel, int32_t note,
                                                      int32_t velocity) {
	if (channel != config_.midiChannel) {
		return false;
	}

	handleMidiNoteForLED(channel, note, velocity);
	return true;
}

bool ControllerModeView::ccReceivedForMidiLearn(MIDICable& fromCable, int32_t channel, int32_t cc, int32_t value) {
	if (channel != config_.midiChannel) {
		return false;
	}

	handleMidiCCForControl(channel, cc, value);
	return true;
}

void ControllerModeView::handleMidiNoteForLED(int32_t channel, int32_t note, int32_t velocity) {
	// Check if this is a pad LED command
	int32_t x, y;
	midiNoteToPad(note, x, y);

	if (x >= 0 && x < kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		// Set pad color based on velocity value
		// Remote script sends velocity as color index
		// Simple mapping: velocity -> RGB color
		if (velocity == 0) {
			// Note off = LED off
			padColors_[x][y] = {0, 0, 0};
		}
		else {
			// Map velocity to color (remote script defines the palette)
			// This is a simple default mapping - remote script can send SysEx for full RGB
			uint8_t r = 0, g = 0, b = 0;

			// Example color palette (similar to Launchpad/Push)
			if (velocity == 1) { r = 30; g = 30; b = 30; }      // Dim white
			else if (velocity < 16) { r = 255; g = 0; b = 0; }   // Red
			else if (velocity < 32) { r = 255; g = 127; b = 0; } // Orange
			else if (velocity < 48) { r = 255; g = 255; b = 0; } // Yellow
			else if (velocity < 64) { r = 0; g = 255; b = 0; }   // Green
			else if (velocity < 80) { r = 0; g = 255; b = 255; } // Cyan
			else if (velocity < 96) { r = 0; g = 0; b = 255; }   // Blue
			else if (velocity < 112) { r = 255; g = 0; b = 255; } // Magenta
			else { r = 255; g = 255; b = 255; }                   // White

			padColors_[x][y] = {r, g, b};
		}

		uiNeedsRendering(this);
	}
	else {
		// Check if this is a button LED command
		int32_t buttonIndex = note - config_.buttonBaseNote;
		if (buttonIndex >= 0 && buttonIndex < 64) {
			buttonLEDStates_[buttonIndex] = (velocity > 0);
			updateButtonLEDs();
		}
	}
}

void ControllerModeView::handleMidiCCForControl(int32_t channel, int32_t cc, int32_t value) {
	// Check if this is an encoder LED control message
	// Encoder LEDs use CC = encoderBaseCC + 20 + encoder_id
	int32_t encoderLEDBase = config_.encoderBaseCC + 20;
	if (cc >= encoderLEDBase && cc < encoderLEDBase + 8) {
		int32_t encoderIndex = cc - encoderLEDBase;
		encoderLEDStates_[encoderIndex] = value;
		updateEncoderLEDs();
	}
	// Other CCs could control display brightness, etc.
}

void ControllerModeView::handleMidiSysexForDisplay(uint8_t* data, int32_t len) {
	if (!config_.receiveDisplaySysex || len < 5) {
		return;
	}

	// Check manufacturer ID (F0 [ID] [device] [command] ... F7)
	if (memcmp(&data[1], SYSEX_MANUFACTURER_ID, 3) != 0) {
		return;
	}

	if (data[4] != SYSEX_DEVICE_ID) {
		return;
	}

	SysExCommand cmd = static_cast<SysExCommand>(data[5]);

	switch (cmd) {
	case SysExCommand::SET_DISPLAY_TEXT:
		processSysexDisplayCommand(data, len);
		break;

	case SysExCommand::SET_7SEG_SEGMENTS:
		processSysex7SegCommand(data, len);
		break;

	case SysExCommand::SET_OLED_PIXELS:
		processSysexOLEDCommand(data, len);
		break;

	case SysExCommand::SET_LED_COLOR:
		// Set specific LED to RGB color (for full color control)
		// Format: F0 [ID] [device] [cmd] [pad_x] [pad_y] [r] [g] [b] F7
		if (len >= 11) {
			int32_t x = data[6];
			int32_t y = data[7];
			uint8_t r = data[8];
			uint8_t g = data[9];
			uint8_t b = data[10];

			if (x < kDisplayWidth && y < kDisplayHeight) {
				padColors_[x][y] = {r, g, b};
				uiNeedsRendering(this);
			}
		}
		break;

	case SysExCommand::DEVICE_INQUIRY:
		sendIdentityReply();
		break;

	default:
		break;
	}
}

void ControllerModeView::processSysexDisplayCommand(uint8_t* data, int32_t len) {
	// Set display text: F0 [ID] [device] [cmd] [text...] F7
	int32_t textLen = len - 7; // Subtract header and F7
	if (textLen > 0 && textLen < sizeof(displayText_)) {
		memcpy(displayText_, &data[6], textLen);
		displayText_[textLen] = '\0';
		updateDisplay();
	}
}

void ControllerModeView::processSysex7SegCommand(uint8_t* data, int32_t len) {
	// Set 7-segment display: F0 [ID] [device] [cmd] [seg0] [seg1] [seg2] [seg3] F7
	if (len >= 10) {
		memcpy(displaySegments_, &data[6], 4);
		// Update 7-seg display hardware
	}
}

void ControllerModeView::processSysexOLEDCommand(uint8_t* data, int32_t len) {
	// Set OLED pixels: F0 [ID] [device] [cmd] [x] [y] [width] [height] [pixel_data...] F7
	// This would allow remote script to draw graphics on OLED
	// Implementation depends on OLED driver interface
}

void ControllerModeView::sendIdentityReply() {
	// Send device identity so remote script knows what controller is connected
	// Format: F0 7E [device] 06 02 [manufacturer] [family] [model] [version] F7
	uint8_t identity[] = {
	    0xF0, // SysEx start
	    0x7E, // Universal non-realtime
	    0x00, // Device ID (0 = all)
	    0x06, // General Information
	    0x02, // Identity Reply
	    SYSEX_MANUFACTURER_ID[0], SYSEX_MANUFACTURER_ID[1], SYSEX_MANUFACTURER_ID[2],
	    0x00, 0x01, // Device family (Deluge)
	    0x00, 0x01, // Device model
	    0x01, 0x00, 0x00, 0x00, // Software version
	    0xF7  // SysEx end
	};

	// Send via active MIDI cable
	if (activeCable_) {
		activeCable_->sendSysex(identity, sizeof(identity));
	}
}

//==============================================================================
// Helper functions
//==============================================================================

int32_t ControllerModeView::padToMidiNote(int32_t x, int32_t y) const {
	if (x < 0 || x >= config_.gridWidth || y < 0 || y >= config_.gridHeight) {
		return -1;
	}

	int32_t note;
	if (config_.rowMajorNotes) {
		// Row-major: note increases left-to-right, then top-to-bottom
		note = config_.gridBaseNote + (y * config_.gridWidth) + x;
	}
	else {
		// Column-major: note increases top-to-bottom, then left-to-right
		note = config_.gridBaseNote + (x * config_.gridHeight) + y;
	}

	return (note >= 0 && note <= 127) ? note : -1;
}

void ControllerModeView::midiNoteToPad(int32_t note, int32_t& x, int32_t& y) const {
	if (note < config_.gridBaseNote) {
		x = y = -1;
		return;
	}

	int32_t offset = note - config_.gridBaseNote;

	if (config_.rowMajorNotes) {
		x = offset % config_.gridWidth;
		y = offset / config_.gridWidth;
	}
	else {
		x = offset / config_.gridHeight;
		y = offset % config_.gridHeight;
	}

	if (x >= config_.gridWidth || y >= config_.gridHeight) {
		x = y = -1;
	}
}

int32_t ControllerModeView::buttonToMidiNote(deluge::hid::Button button) const {
	// Map each button to a unique MIDI note
	// This mapping can be customized based on the target DAW
	using namespace deluge::hid::button;

	int32_t offset = 0;

	// Main transport/function buttons
	switch (button) {
	case PLAY: offset = 0; break;
	case RECORD: offset = 1; break;
	case TAP_TEMPO: offset = 2; break;
	case SYNC_SCALING: offset = 3; break;
	case LEARN: offset = 4; break;
	case SCALE_MODE: offset = 5; break;
	case CROSS_SCREEN_EDIT: offset = 6; break;
	case BACK: offset = 7; break;
	case LOAD: offset = 8; break;
	case SAVE: offset = 9; break;
	case KEYBOARD: offset = 10; break;
	case KIT: offset = 11; break;
	case SYNTH: offset = 12; break;
	case MIDI: offset = 13; break;
	case CV: offset = 14; break;
	case CLIP_VIEW: offset = 15; break;
	case SESSION_VIEW: offset = 16; break;
	case AFFECT_ENTIRE: offset = 17; break;
	case SHIFT: offset = 18; break;
	case SELECT_ENC: offset = 19; break;
	default: return -1;
	}

	return config_.buttonBaseNote + offset;
}

deluge::hid::Button ControllerModeView::midiNoteToButton(int32_t note) const {
	// Reverse mapping from MIDI note to button
	// Used for LED control
	int32_t offset = note - config_.buttonBaseNote;
	if (offset < 0 || offset > 19) {
		return static_cast<deluge::hid::Button>(-1);
	}

	using namespace deluge::hid::button;
	const deluge::hid::Button buttons[] = {
	    PLAY, RECORD, TAP_TEMPO, SYNC_SCALING,
	    LEARN, SCALE_MODE, CROSS_SCREEN_EDIT, BACK,
	    LOAD, SAVE, KEYBOARD, KIT,
	    SYNTH, MIDI, CV, CLIP_VIEW,
	    SESSION_VIEW, AFFECT_ENTIRE, SHIFT, SELECT_ENC
	};

	return buttons[offset];
}

void ControllerModeView::setConfig(const ControllerModeConfig& config) {
	config_ = config;
}

void ControllerModeView::updatePadLEDs() {
	uiNeedsRendering(this);
}

void ControllerModeView::updateButtonLEDs() {
	// Update button LEDs based on state set by remote script via MIDI
	// Map button note numbers to actual button LEDs
	using namespace indicator_leds;

	// Map the first 20 button states to actual button LEDs
	// Note offsets match buttonToMidiNote() mapping
	if (config_.buttonBaseNote + 0 < 64 && buttonLEDStates_[config_.buttonBaseNote + 0]) {
		setLedState(LED::PLAY, buttonLEDStates_[0]);
	}
	if (config_.buttonBaseNote + 1 < 64) {
		setLedState(LED::RECORD, buttonLEDStates_[1]);
	}
	if (config_.buttonBaseNote + 2 < 64) {
		setLedState(LED::TAP_TEMPO, buttonLEDStates_[2]);
	}
	if (config_.buttonBaseNote + 3 < 64) {
		setLedState(LED::SYNC_SCALING, buttonLEDStates_[3]);
	}
	if (config_.buttonBaseNote + 4 < 64) {
		setLedState(LED::LEARN, buttonLEDStates_[4]);
	}
	if (config_.buttonBaseNote + 5 < 64) {
		setLedState(LED::SCALE_MODE, buttonLEDStates_[5]);
	}
	if (config_.buttonBaseNote + 6 < 64) {
		setLedState(LED::CROSS_SCREEN_EDIT, buttonLEDStates_[6]);
	}
	if (config_.buttonBaseNote + 7 < 64) {
		setLedState(LED::BACK, buttonLEDStates_[7]);
	}
	if (config_.buttonBaseNote + 8 < 64) {
		setLedState(LED::LOAD, buttonLEDStates_[8]);
	}
	if (config_.buttonBaseNote + 9 < 64) {
		setLedState(LED::SAVE, buttonLEDStates_[9]);
	}
	if (config_.buttonBaseNote + 10 < 64) {
		setLedState(LED::KEYBOARD, buttonLEDStates_[10]);
	}
	if (config_.buttonBaseNote + 11 < 64) {
		setLedState(LED::KIT, buttonLEDStates_[11]);
	}
	if (config_.buttonBaseNote + 12 < 64) {
		setLedState(LED::SYNTH, buttonLEDStates_[12]);
	}
	if (config_.buttonBaseNote + 13 < 64) {
		setLedState(LED::MIDI, buttonLEDStates_[13]);
	}
	if (config_.buttonBaseNote + 14 < 64) {
		setLedState(LED::CV, buttonLEDStates_[14]);
	}
	if (config_.buttonBaseNote + 15 < 64) {
		setLedState(LED::CLIP_VIEW, buttonLEDStates_[15]);
	}
	if (config_.buttonBaseNote + 16 < 64) {
		setLedState(LED::SESSION_VIEW, buttonLEDStates_[16]);
	}
	if (config_.buttonBaseNote + 17 < 64) {
		setLedState(LED::AFFECT_ENTIRE, buttonLEDStates_[17]);
	}
	if (config_.buttonBaseNote + 18 < 64) {
		setLedState(LED::SHIFT, buttonLEDStates_[18]);
	}
}

void ControllerModeView::updateEncoderLEDs() {
	// Update gold encoder LEDs based on state set by remote script via MIDI CC
	// LED state 0-127 maps to brightness level
	for (int i = 0; i < 8; i++) {
		uint8_t level = encoderLEDStates_[i];
		// Convert 0-127 MIDI value to 0-50 LED brightness (Deluge indicator range)
		uint8_t brightness = (level * 50) / 127;
		indicator_leds::setKnobIndicatorLevel(i, brightness);
	}
	uiNeedsRendering(this);
}
