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

// Controller mode configuration
struct ControllerModeConfig {
	// MIDI channel for control messages (0-15, representing channels 1-16)
	int32_t midiChannel = 0;

	// Grid configuration
	int32_t gridWidth = kDisplayWidth;   // Number of columns in grid
	int32_t gridHeight = kDisplayHeight; // Number of rows in grid
	int32_t gridBaseNote = 0;            // Base MIDI note for grid (usually 0 or 36)

	// Note layout: true = row-major (default), false = column-major
	bool rowMajorNotes = true;

	// Button MIDI note offset (buttons start at this note number)
	int32_t buttonBaseNote = 100;

	// Encoder CC numbers start
	int32_t encoderBaseCC = 71;

	// Enable/disable features
	bool sendPadPressure = true;      // Send polyphonic aftertouch
	bool sendPadVelocity = true;      // Send velocity with note on
	bool receiveDisplaySysex = true;  // Accept display control via SysEx
};

/**
 * Controller Mode View - Generic MIDI controller interface
 *
 * This mode turns Deluge into a generic MIDI controller where:
 * - ALL hardware inputs send MIDI messages (pads, buttons, encoders)
 * - ALL LEDs and display are controlled via incoming MIDI
 * - Remote scripts in the DAW handle all logic and modes
 *
 * MIDI Protocol:
 *
 * OUTPUTS (Deluge -> DAW):
 * - Pads: Note On/Off (note = base + y*width + x, velocity = pressure)
 * - Polyphonic Aftertouch: Per-pad pressure updates
 * - Buttons: Note On/Off (note = buttonBase + button_id)
 * - Encoders: CC messages (CC = encoderBase + encoder_id, value = delta/absolute)
 * - Select encoder: CC (relative values for rotation)
 *
 * INPUTS (DAW -> Deluge):
 * - Pad LEDs: Note On (velocity = color index) / Note Off (turn off)
 * - Button LEDs: Note On/Off on button channel
 * - Display: SysEx messages for text/graphics
 * - 7-seg display: SysEx for segment control
 * - OLED display: SysEx for pixel data
 */
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

	// Input handling - ALL inputs send MIDI
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void selectEncoderAction(int8_t offset) override;
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;
	void modButtonAction(uint8_t whichButton, bool on) override;

	// MIDI input handling - for LED/display control
	bool noteOnReceivedForMidiLearn(MIDICable& fromCable, int32_t channel, int32_t note, int32_t velocity) override;
	void handleMidiNoteForLED(int32_t channel, int32_t note, int32_t velocity);
	void handleMidiCCForControl(int32_t channel, int32_t cc, int32_t value);
	void handleMidiSysexForDisplay(uint8_t* data, int32_t len);

	// Configuration
	ControllerModeConfig& getConfig() { return config_; }
	void setConfig(const ControllerModeConfig& config);

private:
	ControllerModeConfig config_;

	// Display state (controlled by remote script via MIDI)
	RGB padColors_[kDisplayWidth][kDisplayHeight];
	bool buttonLEDStates_[64]; // State for various button LEDs
	char displayText_[20];     // Text for 7-seg or OLED display
	uint8_t displaySegments_[4]; // 7-seg segment data

	// Pad pressure tracking for aftertouch
	uint8_t padPressure_[kDisplayWidth][kDisplayHeight];
	bool padPressed_[kDisplayWidth][kDisplayHeight];

	// Helper functions for MIDI mapping
	int32_t padToMidiNote(int32_t x, int32_t y) const;
	void midiNoteToPad(int32_t note, int32_t& x, int32_t& y) const;
	int32_t buttonToMidiNote(deluge::hid::Button button) const;
	deluge::hid::Button midiNoteToButton(int32_t note) const;

	// Send MIDI messages
	void sendPadNoteOn(int32_t x, int32_t y, int32_t velocity);
	void sendPadNoteOff(int32_t x, int32_t y);
	void sendPadAftertouch(int32_t x, int32_t y, int32_t pressure);
	void sendButtonMidi(deluge::hid::Button button, bool on);
	void sendEncoderCC(int32_t ccNumber, int32_t value);

	// SysEx protocol implementation
	void sendIdentityReply();
	void processSysexDisplayCommand(uint8_t* data, int32_t len);
	void processSysex7SegCommand(uint8_t* data, int32_t len);
	void processSysexOLEDCommand(uint8_t* data, int32_t len);

	// Update display based on MIDI-controlled state
	void updatePadLEDs();
	void updateButtonLEDs();
	void updateDisplay();
};

extern ControllerModeView controllerModeView;
