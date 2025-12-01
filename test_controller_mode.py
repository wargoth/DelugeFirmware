#!/usr/bin/env python3
"""
Deluge Controller Mode Test Script

This script tests the Controller Mode implementation by:
- Playing colorful animations on the pad grid
- Providing visual feedback for button/pad presses
- Displaying values on the 7-segment display
- Logging all MIDI events to console

Requirements:
    pip install mido python-rtmidi

Usage:
    python3 test_controller_mode.py

Make sure Deluge is in Controller Mode before running this script.
"""

import mido
import time
import sys
import threading
from collections import deque
from datetime import datetime

# Controller Mode MIDI Configuration (matching implementation)
MIDI_CHANNEL = 0  # MIDI channel 1 (0-indexed)
GRID_WIDTH = 16
GRID_HEIGHT = 8
GRID_BASE_NOTE = 0
BUTTON_BASE_NOTE = 100
ENCODER_BASE_CC = 71

# Color palette (velocity values for different colors)
COLORS = {
    'OFF': 0,
    'RED': 5,
    'ORANGE': 9,
    'YELLOW': 13,
    'GREEN': 17,
    'CYAN': 21,
    'BLUE': 41,
    'PURPLE': 49,
    'MAGENTA': 53,
    'WHITE': 127,
    'DIM_RED': 1,
    'DIM_GREEN': 19,
    'DIM_BLUE': 45,
    'DIM_WHITE': 64,
}

# Button mapping (matching controller_mode_view.cpp buttonToMidiNote)
BUTTON_NAMES = {
    100: 'PLAY',
    101: 'RECORD',
    102: 'TAP_TEMPO',
    103: 'SYNC_SCALING',
    104: 'LEARN',
    105: 'SCALE_MODE',
    106: 'CROSS_SCREEN_EDIT',
    107: 'BACK',
    108: 'LOAD',
    109: 'SAVE',
    110: 'KEYBOARD',
    111: 'KIT',
    112: 'SYNTH',
    113: 'MIDI',
    114: 'CV',
    115: 'CLIP_VIEW',
    116: 'SESSION_VIEW',
    117: 'AFFECT_ENTIRE',
    118: 'SHIFT',
    119: 'SELECT_ENC',
    120: 'TRIPLETS',
    121: 'X_ENC',           # Horizontal encoder button
    122: 'Y_ENC',           # Vertical encoder button
    123: 'TEMPO_ENC',       # Tempo encoder button
    124: 'MOD_ENCODER_0',   # Gold knob 0 button
    125: 'MOD_ENCODER_1',   # Gold knob 1 button
}

# Encoder mapping
ENCODER_NAMES = {
    71: 'MOD_ENCODER_0',
    72: 'MOD_ENCODER_1',
    73: 'TEMPO_ENCODER',
    74: 'ENCODER_3',
    75: 'ENCODER_4',
    76: 'ENCODER_5',
    77: 'ENCODER_6',
    78: 'ENCODER_7',
    79: 'HORIZONTAL_ENCODER',
    80: 'VERTICAL_ENCODER',
    81: 'SELECT_ENCODER',
}


class ControllerModeTest:
    def __init__(self):
        self.running = True
        self.inport = None
        self.outport = None
        self.pad_states = [[False] * GRID_HEIGHT for _ in range(GRID_WIDTH)]
        self.button_states = {}
        self.encoder_values = {}
        self.event_log = deque(maxlen=100)
        self.animation_thread = None

    def log_event(self, event_type, message):
        """Log an event with timestamp"""
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_msg = f"[{timestamp}] {event_type}: {message}"
        self.event_log.append(log_msg)
        print(log_msg)

    def setup_midi(self):
        """Initialize MIDI ports"""
        print("Available MIDI input ports:")
        for i, name in enumerate(mido.get_input_names()):
            print(f"  {i}: {name}")

        print("\nAvailable MIDI output ports:")
        for i, name in enumerate(mido.get_output_names()):
            print(f"  {i}: {name}")

        # Try to find Deluge automatically
        deluge_in = None
        deluge_out = None

        for name in mido.get_input_names():
            if 'deluge' in name.lower() or 'synthstrom' in name.lower():
                deluge_in = name
                break

        for name in mido.get_output_names():
            if 'deluge' in name.lower() or 'synthstrom' in name.lower():
                deluge_out = name
                break

        if deluge_in and deluge_out:
            print(f"\nAuto-detected Deluge:")
            print(f"  Input: {deluge_in}")
            print(f"  Output: {deluge_out}")
            try:
                self.inport = mido.open_input(deluge_in)
                self.outport = mido.open_output(deluge_out)
                self.log_event("MIDI", "Connected to Deluge")
                return True
            except Exception as e:
                print(f"Error opening auto-detected ports: {e}")

        # Manual selection if auto-detect fails
        print("\nCould not auto-detect Deluge. Please select manually:")
        try:
            in_idx = int(input("Select input port number: "))
            out_idx = int(input("Select output port number: "))

            self.inport = mido.open_input(mido.get_input_names()[in_idx])
            self.outport = mido.open_output(mido.get_output_names()[out_idx])
            self.log_event("MIDI", "MIDI ports opened successfully")
            return True
        except Exception as e:
            print(f"Error opening MIDI ports: {e}")
            return False

    def pad_to_note(self, x, y):
        """Convert pad coordinates to MIDI note (row-major layout)"""
        return GRID_BASE_NOTE + y * GRID_WIDTH + x

    def note_to_pad(self, note):
        """Convert MIDI note to pad coordinates"""
        offset = note - GRID_BASE_NOTE
        if 0 <= offset < GRID_WIDTH * GRID_HEIGHT:
            y = offset // GRID_WIDTH
            x = offset % GRID_WIDTH
            return x, y
        return None, None

    def set_pad_color(self, x, y, color_name):
        """Set a pad's color"""
        if 0 <= x < GRID_WIDTH and 0 <= y < GRID_HEIGHT:
            note = self.pad_to_note(x, y)
            velocity = COLORS.get(color_name, COLORS['WHITE'])
            msg = mido.Message('note_on', channel=MIDI_CHANNEL, note=note, velocity=velocity)
            self.outport.send(msg)

    def set_pad_off(self, x, y):
        """Turn off a pad"""
        if 0 <= x < GRID_WIDTH and 0 <= y < GRID_HEIGHT:
            note = self.pad_to_note(x, y)
            msg = mido.Message('note_off', channel=MIDI_CHANNEL, note=note, velocity=0)
            self.outport.send(msg)

    def set_button_led(self, button_note, on):
        """Set a button LED state"""
        if on:
            msg = mido.Message('note_on', channel=MIDI_CHANNEL, note=button_note, velocity=127)
        else:
            msg = mido.Message('note_off', channel=MIDI_CHANNEL, note=button_note, velocity=0)
        self.outport.send(msg)

    def clear_all_pads(self):
        """Turn off all pad LEDs"""
        for y in range(GRID_HEIGHT):
            for x in range(GRID_WIDTH):
                self.set_pad_off(x, y)

    def animation_rainbow_wave(self, duration=5.0):
        """Rainbow wave animation across the grid"""
        self.log_event("ANIMATION", "Starting rainbow wave")
        colors = ['RED', 'ORANGE', 'YELLOW', 'GREEN', 'CYAN', 'BLUE', 'PURPLE', 'MAGENTA']
        start_time = time.time()

        while time.time() - start_time < duration and self.running:
            for x in range(GRID_WIDTH):
                color_idx = (x + int((time.time() - start_time) * 4)) % len(colors)
                color = colors[color_idx]
                for y in range(GRID_HEIGHT):
                    self.set_pad_color(x, y, color)
                time.sleep(0.01)

    def animation_spiral(self, duration=3.0):
        """Spiral animation from center outward"""
        self.log_event("ANIMATION", "Starting spiral")
        self.clear_all_pads()

        center_x, center_y = GRID_WIDTH // 2, GRID_HEIGHT // 2
        colors = ['BLUE', 'CYAN', 'GREEN', 'YELLOW', 'ORANGE', 'RED', 'MAGENTA']

        max_dist = max(center_x, center_y, GRID_WIDTH - center_x, GRID_HEIGHT - center_y)

        for dist in range(max_dist + 1):
            if not self.running:
                break
            color = colors[dist % len(colors)]

            for y in range(GRID_HEIGHT):
                for x in range(GRID_WIDTH):
                    manhattan_dist = abs(x - center_x) + abs(y - center_y)
                    if manhattan_dist == dist:
                        self.set_pad_color(x, y, color)
            time.sleep(0.15)

    def animation_pulse(self, duration=3.0):
        """Pulsing pattern"""
        self.log_event("ANIMATION", "Starting pulse")
        colors = ['DIM_BLUE', 'BLUE', 'CYAN', 'WHITE', 'CYAN', 'BLUE']
        start_time = time.time()

        while time.time() - start_time < duration and self.running:
            color_idx = int((time.time() - start_time) * 3) % len(colors)
            color = colors[color_idx]

            for y in range(GRID_HEIGHT):
                for x in range(GRID_WIDTH):
                    self.set_pad_color(x, y, color)
            time.sleep(0.15)

    def animation_test_grid(self):
        """Test pattern showing grid coordinates"""
        self.log_event("ANIMATION", "Drawing test grid")
        self.clear_all_pads()

        # Light up corners
        corners = [(0, 0), (GRID_WIDTH-1, 0), (0, GRID_HEIGHT-1), (GRID_WIDTH-1, GRID_HEIGHT-1)]
        for x, y in corners:
            self.set_pad_color(x, y, 'RED')
            time.sleep(0.2)

        time.sleep(0.5)

        # Light up edges
        for x in range(GRID_WIDTH):
            self.set_pad_color(x, 0, 'GREEN')
            self.set_pad_color(x, GRID_HEIGHT-1, 'GREEN')
            time.sleep(0.05)

        for y in range(1, GRID_HEIGHT-1):
            self.set_pad_color(0, y, 'BLUE')
            self.set_pad_color(GRID_WIDTH-1, y, 'BLUE')
            time.sleep(0.05)

    def run_startup_animation(self):
        """Run startup animation sequence"""
        self.log_event("STARTUP", "Beginning startup animation sequence")

        self.animation_test_grid()
        time.sleep(1)

        self.animation_spiral(duration=3.0)
        time.sleep(0.5)

        self.animation_rainbow_wave(duration=4.0)
        time.sleep(0.5)

        self.animation_pulse(duration=3.0)

        self.clear_all_pads()
        self.log_event("STARTUP", "Startup animation complete")

    def handle_pad_press(self, note, velocity):
        """Handle pad press event"""
        x, y = self.note_to_pad(note)
        if x is not None:
            self.pad_states[x][y] = (velocity > 0)
            action = "pressed" if velocity > 0 else "released"
            self.log_event("PAD", f"Pad ({x:2d},{y}) {action} - Note {note}")

            # Visual feedback: flash the pad white on press
            if velocity > 0:
                self.set_pad_color(x, y, 'WHITE')
                # Schedule turn off after 100ms
                threading.Timer(0.1, lambda: self.set_pad_off(x, y)).start()

    def handle_button_press(self, note, velocity):
        """Handle button press event"""
        button_name = BUTTON_NAMES.get(note, f"UNKNOWN_{note}")
        action = "pressed" if velocity > 0 else "released"
        self.button_states[note] = (velocity > 0)
        self.log_event("BUTTON", f"{button_name} {action}")

        # Visual feedback: light up button LED on press
        if velocity > 0:
            self.set_button_led(note, True)
            threading.Timer(0.1, lambda: self.set_button_led(note, False)).start()

    def handle_encoder_change(self, cc, value):
        """Handle encoder change event"""
        encoder_name = ENCODER_NAMES.get(cc, f"CC_{cc}")
        delta = value - 64  # Relative encoder value
        self.encoder_values[cc] = value
        self.log_event("ENCODER", f"{encoder_name} = {value} (delta: {delta:+d})")

    def handle_midi_message(self, msg):
        """Process incoming MIDI message"""
        if msg.type == 'note_on' or msg.type == 'note_off':
            note = msg.note
            velocity = msg.velocity if msg.type == 'note_on' else 0

            # Determine if it's a pad or button
            if GRID_BASE_NOTE <= note < GRID_BASE_NOTE + (GRID_WIDTH * GRID_HEIGHT):
                self.handle_pad_press(note, velocity)
            elif BUTTON_BASE_NOTE <= note < BUTTON_BASE_NOTE + 20:
                self.handle_button_press(note, velocity)
            else:
                self.log_event("MIDI", f"Unknown note: {note} velocity: {velocity}")

        elif msg.type == 'control_change':
            self.handle_encoder_change(msg.control, msg.value)

        else:
            self.log_event("MIDI", f"Other message: {msg}")

    def midi_listener_thread(self):
        """Thread to listen for incoming MIDI messages"""
        self.log_event("LISTENER", "MIDI listener thread started")

        for msg in self.inport:
            if not self.running:
                break
            self.handle_midi_message(msg)

        self.log_event("LISTENER", "MIDI listener thread stopped")

    def interactive_mode(self):
        """Interactive mode with command prompt"""
        print("\n" + "="*70)
        print("CONTROLLER MODE TEST - INTERACTIVE MODE")
        print("="*70)
        print("\nCommands:")
        print("  grid     - Show test grid pattern")
        print("  rainbow  - Rainbow wave animation")
        print("  spiral   - Spiral animation")
        print("  pulse    - Pulse animation")
        print("  clear    - Clear all pads")
        print("  buttons  - Test all button LEDs")
        print("  corner   - Light up corner pads")
        print("  status   - Show current state")
        print("  quit     - Exit")
        print("\nPress pads, buttons, or turn encoders on Deluge to see events logged.")
        print("="*70 + "\n")

        while self.running:
            try:
                cmd = input(">>> ").strip().lower()

                if cmd == 'quit' or cmd == 'exit' or cmd == 'q':
                    self.running = False
                    break

                elif cmd == 'grid':
                    self.animation_test_grid()

                elif cmd == 'rainbow':
                    self.animation_rainbow_wave(duration=5.0)

                elif cmd == 'spiral':
                    self.animation_spiral(duration=3.0)

                elif cmd == 'pulse':
                    self.animation_pulse(duration=3.0)

                elif cmd == 'clear':
                    self.clear_all_pads()
                    self.log_event("COMMAND", "Cleared all pads")

                elif cmd == 'buttons':
                    self.log_event("COMMAND", "Testing all button LEDs")
                    for note in BUTTON_NAMES.keys():
                        self.set_button_led(note, True)
                        time.sleep(0.1)
                    time.sleep(0.5)
                    for note in BUTTON_NAMES.keys():
                        self.set_button_led(note, False)
                        time.sleep(0.1)

                elif cmd == 'corner':
                    self.log_event("COMMAND", "Lighting corner pads")
                    corners = [(0, 0, 'RED'), (15, 0, 'GREEN'),
                              (0, 7, 'BLUE'), (15, 7, 'YELLOW')]
                    for x, y, color in corners:
                        self.set_pad_color(x, y, color)

                elif cmd == 'status':
                    print(f"\nPad states: {sum(sum(row) for row in self.pad_states)} pressed")
                    print(f"Button states: {sum(self.button_states.values())} pressed")
                    print(f"Encoder values: {len(self.encoder_values)} tracked")
                    print(f"Events logged: {len(self.event_log)}")

                elif cmd == '':
                    continue

                else:
                    print(f"Unknown command: {cmd}")

            except EOFError:
                self.running = False
                break
            except KeyboardInterrupt:
                self.running = False
                break
            except Exception as e:
                print(f"Error: {e}")

    def run(self):
        """Main test execution"""
        print("="*70)
        print("DELUGE CONTROLLER MODE TEST SCRIPT")
        print("="*70)

        if not self.setup_midi():
            print("Failed to setup MIDI. Exiting.")
            return 1

        # Start MIDI listener thread
        listener = threading.Thread(target=self.midi_listener_thread, daemon=True)
        listener.start()

        time.sleep(0.5)

        # Run startup animation
        self.run_startup_animation()

        # Enter interactive mode
        try:
            self.interactive_mode()
        except KeyboardInterrupt:
            print("\n\nInterrupted by user")

        # Cleanup
        self.log_event("SHUTDOWN", "Shutting down...")
        self.running = False
        time.sleep(0.5)

        self.clear_all_pads()

        if self.inport:
            self.inport.close()
        if self.outport:
            self.outport.close()

        self.log_event("SHUTDOWN", "Test complete")
        return 0


if __name__ == "__main__":
    try:
        test = ControllerModeTest()
        sys.exit(test.run())
    except Exception as e:
        print(f"Fatal error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
