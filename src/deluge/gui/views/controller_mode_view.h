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

#pragma once

#include "gui/ui/root_ui.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "hid/button.h"

class MIDICable;

// Grid layout modes for controller emulation
enum class ControllerGridMode {
	GENERIC_16X8,   // Full Deluge grid: 16x8 = 128 pads (MIDI notes 0-127)
	APC40_8X5,      // APC40 compatible: 8x5 = 40 pads
	PUSH_8X8,       // Push 2 compatible: 8x8 = 64 pads
	LAUNCHPAD_8X8   // Launchpad compatible: 8x8 = 64 pads
};

// Color palette modes for LED feedback
enum class ControllerColorMode {
	VELOCITY_TO_RGB,     // Map MIDI velocity (0-127) to RGB colors
	FIXED_COLORS,        // Use fixed color palette
	ABLETON_PALETTE      // Ableton Live color palette (compatible with Push/Launchpad)
};

class ControllerModeView : public RootUI {
public:
	ControllerModeView();

	// UI lifecycle
	bool opened() override;
	void focusRegained() override;
	bool canSeeViewUnderneath() override { return false; }
	UIType getUIType() override { return UIType::CONTROLLER_MODE; }

	// Rendering
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	void graphicsRoutine() override;

	// Input handling
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void selectEncoderAction(int8_t offset) override;
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;

	// MIDI feedback handling
	bool noteOnReceivedForMidiLearn(MIDICable& fromCable, int32_t channel, int32_t note, int32_t velocity) override;
	void ccReceivedForFeedback(MIDICable& fromCable, int32_t channel, int32_t cc, int32_t value);

	// Configuration
	void setGridMode(ControllerGridMode mode);
	void setColorMode(ControllerColorMode mode);
	void setMidiChannel(int32_t channel);

	ControllerGridMode getGridMode() const { return gridMode_; }
	ControllerColorMode getColorMode() const { return colorMode_; }
	int32_t getMidiChannel() const { return midiChannel_; }

private:
	// Grid layout configuration
	ControllerGridMode gridMode_;
	ControllerColorMode colorMode_;
	int32_t midiChannel_;  // MIDI channel for controller messages (0-15)

	// Pad state tracking
	bool padPressed_[kDisplayWidth][kDisplayHeight];
	RGB padColors_[kDisplayWidth][kDisplayHeight];

	// Button state tracking
	bool buttonPressed_[16];  // Track state of various buttons

	// Helper functions
	int32_t padToMidiNote(int32_t x, int32_t y) const;
	void midiNoteToPad(int32_t note, int32_t& x, int32_t& y) const;
	RGB velocityToColor(int32_t velocity) const;
	int32_t buttonToMidiNote(deluge::hid::Button button) const;
	int32_t encoderToMidiCC(int32_t encoder) const;

	void sendPadNoteOn(int32_t x, int32_t y, int32_t velocity);
	void sendPadNoteOff(int32_t x, int32_t y);
	void sendButtonNote(deluge::hid::Button button, bool on);
	void sendEncoderCC(int32_t encoder, int32_t value);

	void updatePadDisplay();
	void displayStatus();
};

extern ControllerModeView controllerModeView;
