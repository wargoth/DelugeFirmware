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
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"
#include "memory/general_memory_allocator.h"
#include "model/midi/message.h"
#include <algorithm>
#include <string.h>

ControllerModeView controllerModeView{};

// SysEx command types for controller mode
enum class SysExCommand : uint8_t {
	DEVICE_INQUIRY = 0x01,
	SET_DISPLAY_TEXT = 0x10,
	SET_7SEG_SEGMENTS = 0x11,
	SET_OLED_PIXELS = 0x12,
	SET_LED_COLOR = 0x20,
	SET_ALL_LEDS = 0x21,
	SIDEBAR_PAD_EVENT = 0x30,
	SIDEBAR_LED_CONTROL = 0x31,
	BUTTON_EVENT = 0x40,       // Button press/release
	BUTTON_LED_CONTROL = 0x41, // Button LED control
	BATCH_LED_UPDATE = 0x22,   // Batch LED update
};

ControllerModeView::ControllerModeView() {
	// Initialize with default configuration
	config_ = ControllerModeConfig();

	// Clear all state
	memset(padColors_, 0, sizeof(padColors_));
	memset(sidebarColors_, 0, sizeof(sidebarColors_));
	memset(buttonLEDStates_, 0, sizeof(buttonLEDStates_));
	memset(encoderLEDStates_, 0, sizeof(encoderLEDStates_));

	memset(displayText_, 0, sizeof(displayText_));
	memset(displaySegments_, 0, sizeof(displaySegments_));

	// Leave display empty so SysEx feedback works immediately
	// strcpy(displayText_, "CONTROLLER MODE");
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
	updateEncoderLEDs();
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

	// Sidebar LEDs controlled by remote script via SysEx (2 columns)
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t sidebarCol = 0; sidebarCol < kSideBarWidth; sidebarCol++) {
			int32_t x = kDisplayWidth + sidebarCol;
			// Use colors set via SysEx sidebar LED control
			image[y][x] = sidebarColors_[sidebarCol][y];
			occupancyMask[y][x] = 64; // Match main grid brightness
		}
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
	// Support both main grid (16x8) and sidebar (2 columns: x=16, x=17)
	// Main grid: x=0-15 uses standard Note On/Off
	// Sidebar: x=16-17 use SysEx for press/release to avoid MIDI note conflicts

	// Input validation
	if (x < 0 || x >= kDisplayWidth + kSideBarWidth || y < 0 || y >= kDisplayHeight) {
		return ActionResult::NOT_DEALT_WITH;
	}

	if (velocity > 0) {

		sendPadNoteOn(x, y);
	}
	else {

		sendPadNoteOff(x, y);
	}

	uiNeedsRendering(this);
	return ActionResult::DEALT_WITH;
}

ActionResult ControllerModeView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

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

void ControllerModeView::tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed) {
	// Send tempo encoder as CC (relative)
	// CC 73 for tempo encoder (mod encoder 2 position)
	int32_t value = 64 + offset;
	value = std::max(0_i32, std::min(127_i32, value));
	sendEncoderCC(73, value); // CC 73 for tempo encoder
}

void ControllerModeView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	// Send gold knobs as CC (relative or absolute depending on remote script preference)
	// Using relative encoding by default
	int32_t value = 64 + offset;
	value = std::max(0_i32, std::min(127_i32, value));
	sendEncoderCC(config_.encoderBaseCC + whichModEncoder, value);
}

void ControllerModeView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	ensureActiveCable();
	if (!activeCable_) {
		return;
	}

	// Gold knob buttons send via SysEx
	// Button ID: 24 + encoder index
	if (whichModEncoder < 8) {
		sendButtonSysex(24 + whichModEncoder, on);
	}
}

void ControllerModeView::modButtonAction(uint8_t whichButton, bool on) {
	ensureActiveCable();
	if (!activeCable_) {
		return;
	}

	// Mod matrix buttons send via SysEx
	// Button ID: 32 + button index
	if (whichButton < 8) {
		sendButtonSysex(32 + whichButton, on);
	}
}

//==============================================================================
// MIDI OUTPUT - Send hardware state to DAW
//==============================================================================

void ControllerModeView::ensureActiveCable() {
	if (!activeCable_ && MIDIDeviceManager::root_usb) {
		activeCable_ = MIDIDeviceManager::root_usb->getCable(0);
		if (activeCable_) {
			sendIdentityReply();
		}
	}
}

void ControllerModeView::sendPadNoteOn(int32_t x, int32_t y) {
	ensureActiveCable();
	if (!activeCable_) {
		return;
	}

	// Sidebar pads (x=16, x=17) send via SysEx to avoid MIDI note conflicts
	if (x >= kDisplayWidth) {
		sendSidebarPadSysex(x, y, true); // true = pressed
		return;
	}

	int32_t note = padToMidiNote(x, y);
	// Input validation
	if (note < 0 || note > 127 || config_.midiChannel < 0 || config_.midiChannel > 15) {
		return;
	}

	// Deluge doesn't have velocity sensing - send fixed velocity 127
	MIDIMessage msg = MIDIMessage::noteOn(config_.midiChannel, note, 127);
	activeCable_->sendMessage(msg);
}

void ControllerModeView::sendPadNoteOff(int32_t x, int32_t y) {
	ensureActiveCable();
	if (!activeCable_) {
		return;
	}

	// Sidebar pads (x=16, x=17) send via SysEx
	if (x >= kDisplayWidth) {
		sendSidebarPadSysex(x, y, false); // false = released
		return;
	}

	int32_t note = padToMidiNote(x, y);
	// Input validation
	if (note < 0 || note > 127 || config_.midiChannel < 0 || config_.midiChannel > 15) {
		return;
	}

	MIDIMessage msg = MIDIMessage::noteOff(config_.midiChannel, note, 0);
	activeCable_->sendMessage(msg);
}

void ControllerModeView::sendButtonMidi(deluge::hid::Button button, bool on) {
	ensureActiveCable();
	if (!activeCable_) {
		return;
	}

	// Map button to ID and send via SysEx
	int32_t buttonId = buttonToMidiNote(button);
	if (buttonId >= 0) {
		sendButtonSysex(buttonId, on);
	}
}

void ControllerModeView::sendEncoderCC(int32_t ccNumber, int32_t value) {
	ensureActiveCable();
	if (!activeCable_) {
		return;
	}

	// Input validation
	if (ccNumber < 0 || ccNumber > 127 || value < 0 || value > 127 || config_.midiChannel < 0
	    || config_.midiChannel > 15) {
		return;
	}

	MIDIMessage msg = MIDIMessage::cc(config_.midiChannel, ccNumber, value);
	activeCable_->sendMessage(msg);
}

//==============================================================================
// MIDI INPUT - Receive LED/display commands from DAW remote script
//==============================================================================

bool ControllerModeView::noteOnReceivedForMidiLearn(MIDICable& fromCable, int32_t channel, int32_t note,
                                                    int32_t velocity) {
	// Prevent MIDI loop: only accept from the same cable we're sending to
	if (&fromCable != activeCable_) {
		return false;
	}

	// Only accept on main channel (pads only, buttons use SysEx)
	if (channel != config_.midiChannel) {
		return false;
	}

	// Input validation
	if (note < 0 || note > 127 || velocity < 0 || velocity > 127) {
		return false;
	}

	handleMidiNoteForLED(channel, note, velocity);
	return true;
}

bool ControllerModeView::ccReceivedForMidiLearn(MIDICable& fromCable, int32_t channel, int32_t cc, int32_t value) {
	// Prevent MIDI loop: only accept from the same cable we're sending to
	if (&fromCable != activeCable_) {
		return false;
	}

	if (channel != config_.midiChannel) {
		return false;
	}

	// Input validation
	if (cc < 0 || cc > 127 || value < 0 || value > 127) {
		return false;
	}

	handleMidiCCForControl(channel, cc, value);
	return true;
}

void ControllerModeView::handleMidiNoteForLED(int32_t channel, int32_t note, int32_t velocity) {
	// Check if this is a pad LED command (main grid only, sidebar uses SysEx)
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
			if (velocity == 1) {
				r = 30;
				g = 30;
				b = 30;
			} // Dim white
			else if (velocity < 16) {
				r = 255;
				g = 0;
				b = 0;
			} // Red
			else if (velocity < 32) {
				r = 255;
				g = 127;
				b = 0;
			} // Orange
			else if (velocity < 48) {
				r = 255;
				g = 255;
				b = 0;
			} // Yellow
			else if (velocity < 64) {
				r = 0;
				g = 255;
				b = 0;
			} // Green
			else if (velocity < 80) {
				r = 0;
				g = 255;
				b = 255;
			} // Cyan
			else if (velocity < 96) {
				r = 0;
				g = 0;
				b = 255;
			} // Blue
			else if (velocity < 112) {
				r = 255;
				g = 0;
				b = 255;
			} // Magenta
			else {
				r = 255;
				g = 255;
				b = 255;
			} // White

			padColors_[x][y] = {r, g, b};
		}

		uiNeedsRendering(this);
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
}
void ControllerModeView::handleMidiSysexForDisplay(uint8_t* data, int32_t len) {
	if (!config_.receiveDisplaySysex || len < 5) {
		return;
	}

	// Check manufacturer ID (F0 [ID[0]] [ID[1]] [ID[2]] [ID[3]] [command] ... F7)
	if (data[1] != SysEx::DELUGE_SYSEX_ID_BYTE0 || data[2] != SysEx::DELUGE_SYSEX_ID_BYTE1
	    || data[3] != SysEx::DELUGE_SYSEX_ID_BYTE2 || data[4] != SysEx::DELUGE_SYSEX_ID_BYTE3) {
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

	case SysExCommand::SIDEBAR_LED_CONTROL:
		processSysexSidebarLED(data, len);
		break;

	case SysExCommand::BUTTON_LED_CONTROL:
		processSysexButtonLED(data, len);
		break;

	case SysExCommand::BATCH_LED_UPDATE:
		processSysexBatchLEDCommand(data, len);
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
	// Set 7-segment display: F0 [ID] [device] [cmd] [text...] F7
	// Extract text from SysEx message
	int32_t textLen = len - 7; // Subtract header (6 bytes) and F7 (1 byte)

	// Input validation
	if (textLen <= 0 || textLen > 4) {
		return; // 7-segment display supports max 4 characters
	}

	// Copy text and null-terminate
	char text[5] = {0};
	memcpy(text, &data[6], textLen);

	// Display on 7-segment hardware
	display->setText(text, false); // alignRight=false for left alignment
}

void ControllerModeView::processSysexOLEDCommand(uint8_t* data, int32_t len) {
	// Set OLED pixels: F0 [ID] [device] [cmd] [x] [y] [width] [height] [pixel_data...] F7
	// This would allow remote script to draw graphics on OLED
	// Implementation depends on OLED driver interface
}

void ControllerModeView::processSysexButtonLED(uint8_t* data, int32_t len) {
	// Set button LED: F0 [ID] [device] 41 [button_id] [state] F7
	if (len >= 8) {
		int32_t buttonId = data[6];
		int32_t state = data[7];

		if (buttonId >= 0 && buttonId < 64) {
			buttonLEDStates_[buttonId] = (state > 0);
			updateButtonLEDs();
		}
	}
}

void ControllerModeView::processSysexBatchLEDCommand(uint8_t* data, int32_t len) {
	// Batch LED update: F0 [ID] [device] 22 [x] [y] [r] [g] [b] ... F7
	// Start at index 6
	int32_t i = 6;

	// Process chunks of 5 bytes: x, y, r, g, b
	// Ensure we have enough data (i + 5 <= len)
	// Note: len includes F7, so strictly we should stop before F7
	while (i + 5 < len) {
		int32_t x = data[i];
		int32_t y = data[i + 1];
		uint8_t r = data[i + 2];
		uint8_t g = data[i + 3];
		uint8_t b = data[i + 4];

		if (x < kDisplayWidth && y < kDisplayHeight) {
			// Main grid pad
			// Scale from SysEx range (0-127) to RGB range (0-255)
			padColors_[x][y] = {static_cast<uint8_t>(r * 2), static_cast<uint8_t>(g * 2), static_cast<uint8_t>(b * 2)};
		}
		else if (x >= kDisplayWidth && x < kDisplayWidth + kSideBarWidth && y < kDisplayHeight) {
			// Sidebar pad
			int32_t sidebarCol = x - kDisplayWidth;
			sidebarColors_[sidebarCol][y] = {static_cast<uint8_t>(r * 2), static_cast<uint8_t>(g * 2),
			                                 static_cast<uint8_t>(b * 2)};
		}

		i += 5;
	}

	uiNeedsRendering(this);
}

void ControllerModeView::sendIdentityReply() {
	// Send device identity so remote script knows what controller is connected
	// Format: F0 7E [device] 06 02 [manufacturer] [family] [model] [version] F7
	uint8_t identity[] = {SysEx::SYSEX_START,
	                      SysEx::SYSEX_UNIVERSAL_NONRT,
	                      0x00, // Device ID (0 = all)
	                      SysEx::SYSEX_UNIVERSAL_IDENTITY,
	                      0x02, // Identity Reply
	                      SysEx::DELUGE_SYSEX_ID_BYTE0,
	                      SysEx::DELUGE_SYSEX_ID_BYTE1,
	                      SysEx::DELUGE_SYSEX_ID_BYTE2,
	                      0x00,
	                      0x01, // Device family (Deluge)
	                      0x00,
	                      0x01, // Device model
	                      0x01,
	                      0x00,
	                      0x00,
	                      display->haveOLED() ? 0x01 : 0x00, // Software version (byte 4: 0=7seg, 1=OLED)
	                      SysEx::SYSEX_END};

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
	case PLAY:
		offset = 0;
		break;
	case RECORD:
		offset = 1;
		break;
	case TAP_TEMPO:
		offset = 2;
		break;
	case SYNC_SCALING:
		offset = 3;
		break;
	case LEARN:
		offset = 4;
		break;
	case SCALE_MODE:
		offset = 5;
		break;
	case CROSS_SCREEN_EDIT:
		offset = 6;
		break;
	case BACK:
		offset = 7;
		break;
	case LOAD:
		offset = 8;
		break;
	case SAVE:
		offset = 9;
		break;
	case KEYBOARD:
		offset = 10;
		break;
	case KIT:
		offset = 11;
		break;
	case SYNTH:
		offset = 12;
		break;
	case MIDI:
		offset = 13;
		break;
	case CV:
		offset = 14;
		break;
	case CLIP_VIEW:
		offset = 15;
		break;
	case SESSION_VIEW:
		offset = 16;
		break;
	case AFFECT_ENTIRE:
		offset = 17;
		break;
	case SHIFT:
		offset = 18;
		break;
	case SELECT_ENC:
		offset = 19;
		break;
	case TRIPLETS:
		offset = 20;
		break;
	case X_ENC:
		offset = 21;
		break; // Horizontal encoder button
	case Y_ENC:
		offset = 22;
		break; // Vertical encoder button
	case TEMPO_ENC:
		offset = 23;
		break; // Tempo encoder button
	case MOD_ENCODER_0:
		offset = 24;
		break; // Gold Encoder 0
	case MOD_ENCODER_1:
		offset = 25;
		break; // Gold Encoder 1
	// Note: MOD buttons (effect buttons) are handled by modButtonAction() -> IDs 32-39
	default:
		return -1;
	}

	return config_.buttonBaseNote + offset;
}

deluge::hid::Button ControllerModeView::midiNoteToButton(int32_t note) const {
	// Reverse mapping from MIDI note to button
	// Used for LED control
	int32_t offset = note - config_.buttonBaseNote;
	if (offset < 0 || offset > 25) {
		return static_cast<deluge::hid::Button>(-1);
	}

	using namespace deluge::hid::button;
	const deluge::hid::Button buttons[] = {
	    PLAY,  RECORD,     TAP_TEMPO, SYNC_SCALING, LEARN, SCALE_MODE, CROSS_SCREEN_EDIT, BACK,         LOAD,
	    SAVE,  KEYBOARD,   KIT,       SYNTH,        MIDI,  CV,         CLIP_VIEW,         SESSION_VIEW, AFFECT_ENTIRE,
	    SHIFT, SELECT_ENC, TRIPLETS,  X_ENC,        Y_ENC, TEMPO_ENC,  MOD_ENCODER_0,     MOD_ENCODER_1};

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

	// Helper to set LED state if index is valid
	auto updateLed = [&](int32_t offset, LED led) {
		int32_t index = config_.buttonBaseNote + offset;
		if (index >= 0 && index < 64) {
			setLedState(led, buttonLEDStates_[index]);
		}
	};

	updateLed(0, LED::PLAY);
	updateLed(1, LED::RECORD);
	updateLed(2, LED::TAP_TEMPO);
	updateLed(3, LED::SYNC_SCALING);
	updateLed(4, LED::LEARN);
	updateLed(5, LED::SCALE_MODE);
	updateLed(6, LED::CROSS_SCREEN_EDIT);
	updateLed(7, LED::BACK);
	updateLed(8, LED::LOAD);
	updateLed(9, LED::SAVE);
	updateLed(10, LED::KEYBOARD);
	updateLed(11, LED::KIT);
	updateLed(12, LED::SYNTH);
	updateLed(13, LED::MIDI);
	updateLed(14, LED::CV);
	updateLed(15, LED::CLIP_VIEW);
	updateLed(16, LED::SESSION_VIEW);
	updateLed(17, LED::AFFECT_ENTIRE);
	updateLed(18, LED::SHIFT);
	// 19 is SELECT_ENC (no LED)
	updateLed(20, LED::TRIPLETS);

	// Mod buttons (32-39)
	updateLed(32, LED::MOD_0);
	updateLed(33, LED::MOD_1);
	updateLed(34, LED::MOD_2);
	updateLed(35, LED::MOD_3);
	updateLed(36, LED::MOD_4);
	updateLed(37, LED::MOD_5);
	updateLed(38, LED::MOD_6);
	updateLed(39, LED::MOD_7);
}

void ControllerModeView::updateEncoderLEDs() {
	// Update gold encoder LEDs based on state set by remote script via MIDI CC
	// LED state 0-127 maps to brightness level
	// Only update the 2 physical gold knob LEDs
	for (int i = 0; i < 2; i++) {
		uint8_t level = encoderLEDStates_[i];
		// Use actuallySetKnobIndicatorLevel to bypass metering timer (which clears LED after 500ms)
		indicator_leds::actuallySetKnobIndicatorLevel(i, level);
	}
	uiNeedsRendering(this);
}

void ControllerModeView::sendButtonSysex(int32_t buttonId, bool pressed) {
	// Button press/release via SysEx
	// Format: F0 00 21 7D 01 40 [button_id] [state] F7
	if (!activeCable_ || buttonId < 0 || buttonId > 127) {
		return;
	}

	uint8_t sysex[] = {SysEx::SYSEX_START,
	                   SysEx::DELUGE_SYSEX_ID_BYTE0,
	                   SysEx::DELUGE_SYSEX_ID_BYTE1,
	                   SysEx::DELUGE_SYSEX_ID_BYTE2,
	                   SysEx::DELUGE_SYSEX_ID_BYTE3,
	                   0x40, // BUTTON_EVENT command
	                   static_cast<uint8_t>(buttonId),
	                   pressed ? uint8_t(0x7F) : uint8_t(0x00),
	                   SysEx::SYSEX_END};

	activeCable_->sendSysex(sysex, sizeof(sysex));
}

void ControllerModeView::sendSidebarPadSysex(int32_t x, int32_t y, bool pressed) {
	// Sidebar pad press/release via SysEx
	// Format: F0 00 21 7D 01 30 [x] [y] [state] F7
	if (!activeCable_ || x < kDisplayWidth || x >= kDisplayWidth + kSideBarWidth || y < 0 || y >= kDisplayHeight) {
		return;
	}

	// Convert x to sidebar column index (0 or 1)
	int32_t sidebarCol = x - kDisplayWidth;

	uint8_t sysex[] = {SysEx::SYSEX_START,
	                   SysEx::DELUGE_SYSEX_ID_BYTE0,
	                   SysEx::DELUGE_SYSEX_ID_BYTE1,
	                   SysEx::DELUGE_SYSEX_ID_BYTE2,
	                   SysEx::DELUGE_SYSEX_ID_BYTE3,
	                   0x30, // SIDEBAR_PAD_EVENT command
	                   static_cast<uint8_t>(sidebarCol),
	                   static_cast<uint8_t>(y),
	                   pressed ? 0x7F : 0x00,
	                   SysEx::SYSEX_END};

	activeCable_->sendSysex(sysex, sizeof(sysex));
}

void ControllerModeView::processSysexSidebarLED(uint8_t* data, int32_t len) {
	// Set sidebar LED color via SysEx
	// Format: F0 00 21 7B 01 31 [x] [y] [r] [g] [b] F7
	if (len < 11) {
		return;
	}

	int32_t sidebarCol = data[6]; // 0 or 1 for the two sidebar columns
	int32_t y = data[7];
	uint8_t r = data[8];
	uint8_t g = data[9];
	uint8_t b = data[10];

	if (sidebarCol >= 0 && sidebarCol < kSideBarWidth && y >= 0 && y < kDisplayHeight) {
		// Scale from SysEx range (0-127) to RGB range (0-255)
		// Multiply by 2 to get full range, similar to how velocity mapping works
		sidebarColors_[sidebarCol][y] = {static_cast<uint8_t>(r * 2), static_cast<uint8_t>(g * 2),
		                                 static_cast<uint8_t>(b * 2)};
		uiNeedsRendering(this);
	}
}
