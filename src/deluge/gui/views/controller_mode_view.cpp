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
#include "io/midi/midi_engine.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "gui/l10n/l10n.h"
#include "memory/general_memory_allocator.h"
#include <string.h>
#include <algorithm>

ControllerModeView controllerModeView{};

ControllerModeView::ControllerModeView() {
	// Initialize with default settings
	gridMode_ = ControllerGridMode::PUSH_8X8;
	colorMode_ = ControllerColorMode::VELOCITY_TO_RGB;
	midiChannel_ = 0; // MIDI channel 1 (0-indexed)

	// Clear pad and button states
	memset(padPressed_, 0, sizeof(padPressed_));
	memset(buttonPressed_, 0, sizeof(buttonPressed_));

	// Initialize pad colors to black
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			padColors_[x][y] = deluge::gui::colours::black;
		}
	}
}

bool ControllerModeView::opened() {
	focusRegained();

	// Display welcome message
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CONTROLLER_MODE));

	return true;
}

void ControllerModeView::focusRegained() {
	// Update display
	updatePadDisplay();
	uiTimerManager.setTimer(TimerName::DISPLAY, 1000);
}

void ControllerModeView::graphicsRoutine() {
	// Periodic graphics update
}

bool ControllerModeView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                         bool drawUndefinedArea) {
	if (!image) {
		return true; // Opaque
	}

	// Render the pad grid based on current grid mode and LED feedback
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			// Check if this pad is active in the current grid mode
			bool isActivePad = false;

			switch (gridMode_) {
			case ControllerGridMode::GENERIC_16X8:
				isActivePad = true; // All pads active
				break;

			case ControllerGridMode::APC40_8X5:
				isActivePad = (x < 8 && y < 5);
				break;

			case ControllerGridMode::PUSH_8X8:
			case ControllerGridMode::LAUNCHPAD_8X8:
				isActivePad = (x < 8 && y < 8);
				break;
			}

			if (isActivePad) {
				// Show the pad color from MIDI feedback or dim if pressed
				if (padPressed_[x][y]) {
					// Brighten the pad when pressed
					image[y][x] = {
					    static_cast<uint8_t>(std::min(255, padColors_[x][y].r * 2)),
					    static_cast<uint8_t>(std::min(255, padColors_[x][y].g * 2)),
					    static_cast<uint8_t>(std::min(255, padColors_[x][y].b * 2))
					};
				}
				else {
					// Normal pad color
					image[y][x] = padColors_[x][y];
				}
			}
			else {
				// Inactive pads are dimmed
				image[y][x] = {10, 10, 10};
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

	// Render sidebar - show mode indicators or shortcuts
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		// Show current grid mode on sidebar
		if (y == 0) {
			image[y][kDisplayWidth] = {0, 255, 0}; // Green for active mode
		}
		else {
			image[y][kDisplayWidth] = {20, 20, 20}; // Dim
		}
		occupancyMask[y][kDisplayWidth] = 64;
	}

	return true;
}

void ControllerModeView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	// Display controller mode information on OLED
	displayStatus();
}

void ControllerModeView::displayStatus() {
	// Display current mode and settings
	char buffer[100];

	const char* modeName = "Unknown";
	switch (gridMode_) {
	case ControllerGridMode::GENERIC_16X8:
		modeName = "16x8 Generic";
		break;
	case ControllerGridMode::APC40_8X5:
		modeName = "APC40 (8x5)";
		break;
	case ControllerGridMode::PUSH_8X8:
		modeName = "Push 2 (8x8)";
		break;
	case ControllerGridMode::LAUNCHPAD_8X8:
		modeName = "Launchpad (8x8)";
		break;
	}

	sprintf(buffer, "%s Ch:%d", modeName, midiChannel_ + 1);
	display->displayPopup(buffer);
}

// Pad action - convert to MIDI note on/off
ActionResult ControllerModeView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (velocity > 0) {
		// Pad pressed
		padPressed_[x][y] = true;
		sendPadNoteOn(x, y, velocity);
	}
	else {
		// Pad released
		padPressed_[x][y] = false;
		sendPadNoteOff(x, y);
	}

	// Update display
	uiNeedsRendering(this);

	return ActionResult::DEALT_WITH;
}

// Button action - convert to MIDI notes or CCs
ActionResult ControllerModeView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	// Map common buttons to MIDI messages
	switch (b) {
	case deluge::hid::Button::BACK:
		// Exit controller mode
		if (on) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_EXITING_CONTROLLER_MODE));
			changeRootUI(&sessionView);
		}
		return ActionResult::DEALT_WITH;

	case deluge::hid::Button::PLAY:
	case deluge::hid::Button::RECORD:
	case deluge::hid::Button::TAP_TEMPO:
	case deluge::hid::Button::SYNC_SCALING:
		// Send as MIDI notes for transport control
		sendButtonNote(b, on);
		return ActionResult::DEALT_WITH;

	case deluge::hid::Button::SELECT_ENC:
		// Mode switching with select encoder button
		if (on) {
			// Cycle through grid modes
			switch (gridMode_) {
			case ControllerGridMode::GENERIC_16X8:
				setGridMode(ControllerGridMode::PUSH_8X8);
				break;
			case ControllerGridMode::PUSH_8X8:
				setGridMode(ControllerGridMode::APC40_8X5);
				break;
			case ControllerGridMode::APC40_8X5:
				setGridMode(ControllerGridMode::LAUNCHPAD_8X8);
				break;
			case ControllerGridMode::LAUNCHPAD_8X8:
				setGridMode(ControllerGridMode::GENERIC_16X8);
				break;
			}
			displayStatus();
			uiNeedsRendering(this);
		}
		return ActionResult::DEALT_WITH;

	default:
		break;
	}

	return ActionResult::NOT_DEALT_WITH;
}

ActionResult ControllerModeView::horizontalEncoderAction(int32_t offset) {
	// Horizontal encoder could control MIDI channel
	int32_t newChannel = midiChannel_ + offset;
	if (newChannel < 0) newChannel = 0;
	if (newChannel > 15) newChannel = 15;

	if (newChannel != midiChannel_) {
		setMidiChannel(newChannel);
		displayStatus();
	}

	return ActionResult::DEALT_WITH;
}

ActionResult ControllerModeView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}

void ControllerModeView::selectEncoderAction(int8_t offset) {
	// Select encoder could adjust parameters
}

void ControllerModeView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	// Send gold knobs/encoders as MIDI CCs
	int32_t ccValue = 64 + offset; // Relative CC value
	if (ccValue < 0) ccValue = 0;
	if (ccValue > 127) ccValue = 127;

	sendEncoderCC(whichModEncoder, ccValue);
}

void ControllerModeView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	// Encoder button presses could send MIDI notes
}

bool ControllerModeView::noteOnReceivedForMidiLearn(MIDICable& fromCable, int32_t channel, int32_t note,
                                                      int32_t velocity) {
	// Handle incoming MIDI for LED feedback
	if (channel != midiChannel_) {
		return false; // Not for us
	}

	// Map MIDI note to pad position
	int32_t x, y;
	midiNoteToPad(note, x, y);

	if (x >= 0 && x < kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		// Update pad color based on velocity
		padColors_[x][y] = velocityToColor(velocity);
		uiNeedsRendering(this);
		return true;
	}

	return false;
}

void ControllerModeView::ccReceivedForFeedback(MIDICable& fromCable, int32_t channel, int32_t cc, int32_t value) {
	// Handle incoming MIDI CC for feedback
	// Could be used to control displays or other visual feedback
}

// Configuration methods
void ControllerModeView::setGridMode(ControllerGridMode mode) {
	gridMode_ = mode;
}

void ControllerModeView::setColorMode(ControllerColorMode mode) {
	colorMode_ = mode;
}

void ControllerModeView::setMidiChannel(int32_t channel) {
	if (channel >= 0 && channel <= 15) {
		midiChannel_ = channel;
	}
}

// Helper functions for MIDI mapping
int32_t ControllerModeView::padToMidiNote(int32_t x, int32_t y) const {
	switch (gridMode_) {
	case ControllerGridMode::GENERIC_16X8:
		// Map full 16x8 grid to MIDI notes 0-127
		return (y * kDisplayWidth) + x;

	case ControllerGridMode::APC40_8X5:
		// APC40: 8 columns x 5 rows
		// Notes 0-39 (bottom-left is 0, top-right is 39)
		if (x >= 8 || y >= 5) return -1;
		return ((4 - y) * 8) + x;

	case ControllerGridMode::PUSH_8X8:
		// Push 2: 8x8 grid, notes 36-99
		if (x >= 8 || y >= 8) return -1;
		return 36 + ((7 - y) * 8) + x;

	case ControllerGridMode::LAUNCHPAD_8X8:
		// Launchpad: 8x8 grid, notes 0-63 (or other mapping)
		if (x >= 8 || y >= 8) return -1;
		return ((7 - y) * 8) + x;

	default:
		return -1;
	}
}

void ControllerModeView::midiNoteToPad(int32_t note, int32_t& x, int32_t& y) const {
	switch (gridMode_) {
	case ControllerGridMode::GENERIC_16X8:
		x = note % kDisplayWidth;
		y = note / kDisplayWidth;
		break;

	case ControllerGridMode::APC40_8X5:
		if (note < 0 || note >= 40) {
			x = y = -1;
			return;
		}
		x = note % 8;
		y = 4 - (note / 8);
		break;

	case ControllerGridMode::PUSH_8X8:
		if (note < 36 || note >= 100) {
			x = y = -1;
			return;
		}
		note -= 36;
		x = note % 8;
		y = 7 - (note / 8);
		break;

	case ControllerGridMode::LAUNCHPAD_8X8:
		if (note < 0 || note >= 64) {
			x = y = -1;
			return;
		}
		x = note % 8;
		y = 7 - (note / 8);
		break;

	default:
		x = y = -1;
		break;
	}
}

RGB ControllerModeView::velocityToColor(int32_t velocity) const {
	// Map MIDI velocity (0-127) to RGB color
	// Simple mapping: velocity = brightness
	uint8_t brightness = (velocity * 255) / 127;

	switch (colorMode_) {
	case ControllerColorMode::VELOCITY_TO_RGB:
		// Map velocity to hue
		if (velocity < 32) {
			return {brightness, 0, 0}; // Red
		}
		else if (velocity < 64) {
			return {0, brightness, 0}; // Green
		}
		else if (velocity < 96) {
			return {0, 0, brightness}; // Blue
		}
		else {
			return {brightness, brightness, 0}; // Yellow
		}

	case ControllerColorMode::FIXED_COLORS:
		// Use fixed color based on velocity ranges
		if (velocity == 0) return {0, 0, 0};
		if (velocity < 64) return {255, 0, 0};
		return {0, 255, 0};

	case ControllerColorMode::ABLETON_PALETTE:
		// Ableton Live color palette (simplified)
		// Full implementation would use the exact Ableton palette
		if (velocity == 0) return {0, 0, 0};
		if (velocity == 1) return {20, 20, 20};
		if (velocity < 16) return {255, 0, 0};
		if (velocity < 32) return {255, 127, 0};
		if (velocity < 48) return {255, 255, 0};
		if (velocity < 64) return {0, 255, 0};
		if (velocity < 80) return {0, 255, 255};
		if (velocity < 96) return {0, 0, 255};
		if (velocity < 112) return {255, 0, 255};
		return {255, 255, 255};

	default:
		return {brightness, brightness, brightness};
	}
}

int32_t ControllerModeView::buttonToMidiNote(deluge::hid::Button button) const {
	// Map buttons to MIDI notes (compatible with Push 2 / APC40)
	switch (button) {
	case deluge::hid::Button::PLAY:
		return 85; // Play button (Push 2 compatible)
	case deluge::hid::Button::RECORD:
		return 86; // Record button
	case deluge::hid::Button::TAP_TEMPO:
		return 99; // Tap tempo
	case deluge::hid::Button::SYNC_SCALING:
		return 9; // Metronome
	default:
		return -1;
	}
}

int32_t ControllerModeView::encoderToMidiCC(int32_t encoder) const {
	// Map encoders to MIDI CCs
	// Push 2 uses CCs 71-78 for encoders
	return 71 + encoder;
}

void ControllerModeView::sendPadNoteOn(int32_t x, int32_t y, int32_t velocity) {
	int32_t note = padToMidiNote(x, y);
	if (note >= 0) {
		midiEngine.sendNote(MIDISource::INTERNAL, true, note, velocity, midiChannel_ + 1);
	}
}

void ControllerModeView::sendPadNoteOff(int32_t x, int32_t y) {
	int32_t note = padToMidiNote(x, y);
	if (note >= 0) {
		midiEngine.sendNote(MIDISource::INTERNAL, false, note, 0, midiChannel_ + 1);
	}
}

void ControllerModeView::sendButtonNote(deluge::hid::Button button, bool on) {
	int32_t note = buttonToMidiNote(button);
	if (note >= 0) {
		midiEngine.sendNote(MIDISource::INTERNAL, on, note, on ? 127 : 0, midiChannel_ + 1);
	}
}

void ControllerModeView::sendEncoderCC(int32_t encoder, int32_t value) {
	int32_t cc = encoderToMidiCC(encoder);
	if (cc >= 0 && encoder >= 0 && encoder < 8) {
		midiEngine.sendCC(MIDISource::INTERNAL, midiChannel_ + 1, cc, value);
	}
}

void ControllerModeView::updatePadDisplay() {
	uiNeedsRendering(this);
}
