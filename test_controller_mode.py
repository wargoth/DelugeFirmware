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
MIDI_CHANNEL = 0  # MIDI channel 1 (0-indexed) for PADS

GRID_WIDTH = 16
GRID_HEIGHT = 8
GRID_BASE_NOTE = 0
BUTTON_BASE_NOTE = 0  # Buttons now start at 0 on channel 2
ENCODER_BASE_CC = 71

# SysEx constants (matching Deluge firmware)
SYSEX_START = 0xF0
SYSEX_END = 0xF7
DELUGE_SYSEX_ID = [0x00, 0x21, 0x7B, 0x01]  # Official Synthstrom Deluge ID


# SysEx command types
class SysExCommand:
    DEVICE_INQUIRY = 0x01
    SET_DISPLAY_TEXT = 0x10
    SET_7SEG_SEGMENTS = 0x11
    SET_OLED_PIXELS = 0x12
    SET_LED_COLOR = 0x20
    SET_ALL_LEDS = 0x21
    SIDEBAR_PAD_EVENT = 0x30
    SIDEBAR_LED_CONTROL = 0x31
    BUTTON_EVENT = 0x40
    BUTTON_LED_CONTROL = 0x41
    BATCH_LED_UPDATE = 0x22


# Color palette (velocity values for different colors)
COLORS = {
    "OFF": 0,
    "RED": 5,  # < 16
    "ORANGE": 20,  # < 32
    "YELLOW": 35,  # < 48
    "GREEN": 50,  # < 64
    "CYAN": 66,  # < 80
    "BLUE": 82,  # < 96
    "PURPLE": 90,  # < 96 (Blue/Purple range)
    "MAGENTA": 100,  # < 112
    "WHITE": 127,  # >= 112
    "DIM_RED": 1,
    "DIM_GREEN": 49,
    "DIM_BLUE": 81,
    "DIM_WHITE": 113,
}

# Button mapping (matching controller_mode_view.cpp)
# ALL buttons now send via SysEx to avoid conflicts with pads
BUTTON_NAMES = {
    # Main buttons - IDs 0-23
    0: "PLAY",
    1: "RECORD",
    2: "TAP_TEMPO",
    3: "SYNC_SCALING",
    4: "LEARN",
    5: "SCALE_MODE",
    6: "CROSS_SCREEN_EDIT",
    7: "BACK",
    8: "LOAD",
    9: "SAVE",
    10: "KEYBOARD",
    11: "KIT",
    12: "SYNTH",
    13: "MIDI",
    14: "CV",
    15: "CLIP_VIEW",
    16: "SESSION_VIEW",
    17: "AFFECT_ENTIRE",
    18: "SHIFT",
    19: "SELECT_ENC",
    20: "TRIPLETS",
    21: "X_ENC",  # Horizontal encoder button
    22: "Y_ENC",  # Vertical encoder button
    23: "TEMPO_ENC",  # Tempo encoder button
    # Gold encoder buttons - IDs 24-31
    24: "GOLD_ENCODER_0",
    25: "GOLD_ENCODER_1",
    26: "GOLD_ENCODER_2",
    27: "GOLD_ENCODER_3",
    28: "GOLD_ENCODER_4",
    29: "GOLD_ENCODER_5",
    30: "GOLD_ENCODER_6",
    31: "GOLD_ENCODER_7",
    # Mod buttons - IDs 32-39
    32: "EFFECT_BUTTON_0",
    33: "EFFECT_BUTTON_1",
    34: "EFFECT_BUTTON_2",
    35: "EFFECT_BUTTON_3",
    36: "EFFECT_BUTTON_4",
    37: "EFFECT_BUTTON_5",
    38: "EFFECT_BUTTON_6",
    39: "EFFECT_BUTTON_7",
}

# Encoder mapping
ENCODER_NAMES = {
    71: "MOD_ENCODER_0",
    72: "MOD_ENCODER_1",
    73: "TEMPO_ENCODER",
    74: "ENCODER_3",
    75: "ENCODER_4",
    76: "ENCODER_5",
    77: "ENCODER_6",
    78: "ENCODER_7",
    79: "HORIZONTAL_ENCODER",
    80: "VERTICAL_ENCODER",
    81: "SELECT_ENCODER",
}


class ControllerModeTest:
    def __init__(self):
        self.running = True
        self.inport = None
        self.outport = None
        self.pad_states = [[False] * GRID_HEIGHT for _ in range(GRID_WIDTH)]
        self.button_states = {}
        self.encoder_values = {}
        self.encoder_accumulators = {
            cc: 64 for cc in range(71, 79)
        }  # Initialize gold knobs to center
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
            if "deluge" in name.lower() or "synthstrom" in name.lower():
                deluge_in = name
                break

        for name in mido.get_output_names():
            if "deluge" in name.lower() or "synthstrom" in name.lower():
                deluge_out = name
                break

        if deluge_in and deluge_out:
            print("\nAuto-detected Deluge:")
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

    def _build_sysex(self, command, *data_bytes):
        """Helper to build SysEx messages with proper framing"""
        return DELUGE_SYSEX_ID + [command] + list(data_bytes)

    def _color_name_to_rgb(self, color_name):
        """Convert color name to RGB tuple (0-127 range for SysEx)"""
        color = COLORS.get(color_name, COLORS["WHITE"])
        # Map velocity value to RGB (SysEx uses 0-127)
        if color == 0:
            return (0, 0, 0)
        elif color < 16:
            return (127, 0, 0)  # Red
        elif color < 32:
            return (127, 63, 0)  # Orange
        elif color < 48:
            return (127, 127, 0)  # Yellow
        elif color < 64:
            return (0, 127, 0)  # Green
        elif color < 80:
            return (0, 127, 127)  # Cyan
        elif color < 96:
            return (0, 0, 127)  # Blue
        else:
            return (127, 127, 127)  # White

    def set_pad_color(self, x, y, color_name):
        """Set a pad's color (main grid uses MIDI notes, sidebar uses SysEx)"""
        if x >= GRID_WIDTH:  # Sidebar (x=16 or x=17)
            sidebar_col = x - GRID_WIDTH  # 0 or 1
            if 0 <= sidebar_col < 2 and 0 <= y < GRID_HEIGHT:
                r, g, b = self._color_name_to_rgb(color_name)
                sysex_data = self._build_sysex(
                    SysExCommand.SIDEBAR_LED_CONTROL, sidebar_col, y, r, g, b
                )
                msg = mido.Message("sysex", data=sysex_data)
                self.outport.send(msg)
        elif 0 <= x < GRID_WIDTH and 0 <= y < GRID_HEIGHT:  # Main grid
            note = self.pad_to_note(x, y)
            velocity = COLORS.get(color_name, COLORS["WHITE"])
            msg = mido.Message(
                "note_on", channel=MIDI_CHANNEL, note=note, velocity=velocity
            )
            self.outport.send(msg)

    def set_pad_off(self, x, y):
        """Turn off a pad (main grid uses MIDI notes, sidebar uses SysEx)"""
        if x >= GRID_WIDTH:  # Sidebar (x=16 or x=17)
            sidebar_col = x - GRID_WIDTH  # 0 or 1
            if 0 <= sidebar_col < 2 and 0 <= y < GRID_HEIGHT:
                sysex_data = self._build_sysex(
                    SysExCommand.SIDEBAR_LED_CONTROL, sidebar_col, y, 0, 0, 0
                )
                msg = mido.Message("sysex", data=sysex_data)
                self.outport.send(msg)
        elif 0 <= x < GRID_WIDTH and 0 <= y < GRID_HEIGHT:  # Main grid
            note = self.pad_to_note(x, y)
            msg = mido.Message("note_off", channel=MIDI_CHANNEL, note=note, velocity=0)
            self.outport.send(msg)

    def set_button_led(self, button_note, on):
        """Set a button LED state via SysEx"""
        state = 1 if on else 0
        sysex_data = self._build_sysex(
            SysExCommand.BUTTON_LED_CONTROL, button_note, state
        )
        msg = mido.Message("sysex", data=sysex_data)
        self.outport.send(msg)

    def batch_set_pad_colors(self, updates):
        """
        Batch update pad colors.
        updates: list of tuples (x, y, color_name)
        """
        sysex_data = self._build_sysex(SysExCommand.BATCH_LED_UPDATE)

        for x, y, color_name in updates:
            r, g, b = self._color_name_to_rgb(color_name)
            sysex_data.extend([x, y, r, g, b])

        msg = mido.Message("sysex", data=sysex_data)
        self.outport.send(msg)

    def send_7seg_text(self, text):
        """Send text to Deluge 7-segment display via SysEx"""
        sysex_data = self._build_sysex(SysExCommand.SET_7SEG_SEGMENTS)
        sysex_data.extend([ord(c) for c in text[:4]])  # Max 4 chars
        msg = mido.Message("sysex", data=sysex_data)
        self.outport.send(msg)

    def clear_all_pads(self):
        """Turn off all pad LEDs (including both sidebar columns)"""
        updates = []
        for y in range(GRID_HEIGHT):
            for x in range(
                GRID_WIDTH + 2
            ):  # +2 to include both sidebar columns (x=16, x=17)
                updates.append((x, y, "OFF"))
        self.batch_set_pad_colors(updates)

    def animation_rainbow_wave(self, duration=5.0):
        """Rainbow wave animation across the grid including both sidebar columns"""
        self.log_event("ANIMATION", "Starting rainbow wave (BATCH)")
        colors = [
            "RED",
            "ORANGE",
            "YELLOW",
            "GREEN",
            "CYAN",
            "BLUE",
            "PURPLE",
            "MAGENTA",
        ]
        start_time = time.time()

        while time.time() - start_time < duration and self.running:
            updates = []
            for x in range(GRID_WIDTH + 2):  # +2 to include both sidebar columns
                color_idx = (x + int((time.time() - start_time) * 4)) % len(colors)
                color = colors[color_idx]
                for y in range(GRID_HEIGHT):
                    updates.append((x, y, color))

            self.batch_set_pad_colors(updates)
            time.sleep(0.05)

    def animation_spiral(self, duration=3.0):
        """Spiral animation from center outward (including both sidebar columns)"""
        self.log_event("ANIMATION", "Starting spiral (BATCH)")
        self.clear_all_pads()

        center_x, center_y = (
            (GRID_WIDTH + 2) // 2,
            GRID_HEIGHT // 2,
        )  # Include both sidebar columns
        colors = ["BLUE", "CYAN", "GREEN", "YELLOW", "ORANGE", "RED", "MAGENTA"]

        max_dist = max(
            center_x, center_y, GRID_WIDTH + 2 - center_x, GRID_HEIGHT - center_y
        )

        for dist in range(max_dist + 1):
            if not self.running:
                break
            color = colors[dist % len(colors)]

            updates = []
            for y in range(GRID_HEIGHT):
                for x in range(GRID_WIDTH + 2):  # Include both sidebar columns
                    manhattan_dist = abs(x - center_x) + abs(y - center_y)
                    if manhattan_dist == dist:
                        updates.append((x, y, color))

            if updates:
                self.batch_set_pad_colors(updates)
            time.sleep(0.15)

    def animation_pulse(self, duration=3.0):
        """Pulsing pattern (including both sidebar columns)"""
        self.log_event("ANIMATION", "Starting pulse (BATCH)")
        colors = ["DIM_BLUE", "BLUE", "CYAN", "WHITE", "CYAN", "BLUE"]
        start_time = time.time()

        while time.time() - start_time < duration and self.running:
            color_idx = int((time.time() - start_time) * 3) % len(colors)
            color = colors[color_idx]

            updates = []
            for y in range(GRID_HEIGHT):
                for x in range(GRID_WIDTH + 2):  # Include both sidebar columns
                    updates.append((x, y, color))

            self.batch_set_pad_colors(updates)
            time.sleep(0.15)

    def animation_test_grid(self):
        """Test pattern showing grid coordinates (including both sidebar columns)"""
        self.log_event("ANIMATION", "Drawing test grid (BATCH)")
        self.clear_all_pads()

        # Light up all 4 corners (treating sidebar as extension)
        corners = [
            (0, 0),
            (GRID_WIDTH + 1, 0),  # Top-right (x=17)
            (0, GRID_HEIGHT - 1),
            (GRID_WIDTH + 1, GRID_HEIGHT - 1),  # Bottom-right (x=17)
        ]
        for x, y in corners:
            self.set_pad_color(x, y, "RED")
            time.sleep(0.2)

        time.sleep(0.5)

        # Light up edges including both sidebar columns
        for x in range(GRID_WIDTH + 2):  # Include both sidebar columns
            updates = [(x, 0, "GREEN"), (x, GRID_HEIGHT - 1, "GREEN")]
            self.batch_set_pad_colors(updates)
            time.sleep(0.05)

        for y in range(1, GRID_HEIGHT - 1):
            updates = [
                (0, y, "BLUE"),
                (GRID_WIDTH + 1, y, "MAGENTA"),  # Right edge (x=17)
            ]
            self.batch_set_pad_colors(updates)
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
            self.pad_states[x][y] = velocity > 0
            action = "pressed" if velocity > 0 else "released"
            self.log_event("PAD", f"Pad ({x:2d},{y}) {action} - Note {note}")

            if velocity > 0:
                # Show pad note on 7-segment display
                self.send_7seg_text(f"P{note:02d}")
                # Visual feedback: flash the pad white on press
                self.set_pad_color(x, y, "WHITE")
                # Schedule turn off after 100ms
                threading.Timer(0.1, lambda: self.set_pad_off(x, y)).start()
            else:
                # Clear display on release
                self.send_7seg_text("")

    def handle_button_press(self, note, velocity):
        """Handle button press event"""
        button_name = BUTTON_NAMES.get(note, f"UNKNOWN_{note}")
        action = "pressed" if velocity > 0 else "released"
        self.button_states[note] = velocity > 0
        self.log_event("BUTTON", f"{button_name} {action} - Note {note}")

        if velocity > 0:
            # Show button on 7-segment display
            self.send_7seg_text(f"B{note:02d}")
            # TODO: Add button LED feedback when implemented
        else:
            # Clear display on release
            self.send_7seg_text("")

        if velocity > 0:
            # Show button note on 7-segment display
            self.send_7seg_text(f"B{note:02d}")
            # Visual feedback: light up button LED on press
            self.set_button_led(note, True)
            threading.Timer(0.1, lambda: self.set_button_led(note, False)).start()
        else:
            # Clear display on release
            self.send_7seg_text("")

    def handle_encoder_change(self, cc, value):
        """Handle encoder change event"""
        encoder_name = ENCODER_NAMES.get(cc, f"CC_{cc}")
        delta = value - 64  # Relative encoder value
        self.encoder_values[cc] = value
        self.log_event(
            "ENCODER", f"{encoder_name} = {value} (delta: {delta:+d}) - CC {cc}"
        )

        # Show encoder CC on 7-segment display
        self.send_7seg_text(f"E{cc:02d}")
        # Auto-clear after 500ms
        threading.Timer(0.5, lambda: self.send_7seg_text("")).start()

        # Send feedback to encoder LEDs (gold knobs only)
        # Gold knobs are CC 71-78 (ENCODER_BASE_CC + 0..7)
        # LED control is CC 91-98 (ENCODER_BASE_CC + 20 + 0..7)
        if ENCODER_BASE_CC <= cc < ENCODER_BASE_CC + 8:
            # Update accumulator
            current_val = self.encoder_accumulators.get(cc, 64)
            new_val = max(0, min(127, current_val + delta))
            self.encoder_accumulators[cc] = new_val

            led_cc = cc + 20
            # Map 0-127 value to LED brightness/state
            # For now, just echo the value back
            msg = mido.Message(
                "control_change", channel=MIDI_CHANNEL, control=led_cc, value=new_val
            )
            self.outport.send(msg)
            self.log_event(
                "FEEDBACK", f"Sent encoder LED update: CC {led_cc} = {new_val}"
            )

    def handle_midi_message(self, msg):
        """Process incoming MIDI message"""
        if msg.type == "sysex":
            # Handle SysEx messages (sidebar pads, buttons, etc.)
            data = bytes([0xF0] + list(msg.data) + [0xF7])
            self.handle_sysex(data)

        elif msg.type == "note_on" or msg.type == "note_off":
            note = msg.note
            velocity = msg.velocity if msg.type == "note_on" else 0
            channel = msg.channel

            # Route based on MIDI channel
            if channel == MIDI_CHANNEL:
                # Channel 1: Pads (notes 0-127)
                self.handle_pad_press(note, velocity)
            else:
                self.log_event(
                    "MIDI",
                    f"Unknown channel {channel + 1}: note {note} velocity {velocity}",
                )

        elif msg.type == "control_change":
            self.handle_encoder_change(msg.control, msg.value)

        else:
            self.log_event("MIDI", f"Other message: {msg}")

    def handle_sysex(self, data):
        """Handle SysEx messages"""
        # Check if it's a Deluge message: F0 00 21 7B 01 [cmd] ...
        if len(data) < 7:
            return

        if data[1:5] != bytes(DELUGE_SYSEX_ID):
            # Check for Identity Reply: F0 7E [device] 06 02 ...
            if (
                len(data) >= 15
                and data[1] == 0x7E
                and data[3] == 0x06
                and data[4] == 0x02
            ):
                # Check manufacturer ID (Synthstrom: 00 21 7B)
                if data[5:8] == bytes([0x00, 0x21, 0x7B]):
                    # Extract version bytes
                    v1, v2, v3, v4 = data[11:15]
                    display_type = "OLED" if v4 == 1 else "7-Segment"
                    self.log_event(
                        "IDENTITY",
                        f"Deluge detected! Firmware v{v1}.{v2}.{v3}, Display: {display_type}",
                    )
                    return
            return

        cmd = data[5]

        if cmd == 0x02:  # Identity Reply
            # F0 7E [device] 06 02 [manufacturer] [family] [model] [v1] [v2] [v3] [v4] F7
            # Note: The test script receives the full SysEx message including F0/F7
            # But the check above verifies the Deluge specific header which is NOT present in Identity Reply
            # Identity Reply is Universal Non-Realtime: F0 7E ...
            pass

        elif cmd == SysExCommand.SIDEBAR_PAD_EVENT:  # SIDEBAR_PAD_EVENT
            if len(data) >= 10:
                sidebar_col = data[6]  # 0 or 1
                y = data[7]
                state = data[8]
                action = "pressed" if state > 0 else "released"
                x_display = (
                    GRID_WIDTH + sidebar_col
                )  # Convert to display coordinates (16 or 17)
                self.log_event("SIDEBAR_PAD", f"Sidebar pad ({x_display},{y}) {action}")

                if state > 0:
                    # Show on 7-seg
                    self.send_7seg_text(f"S{sidebar_col}{y}")
                    # Visual feedback: flash the sidebar pad white on press
                    self.set_pad_color(x_display, y, "WHITE")
                    # Schedule turn off after 100ms
                    threading.Timer(0.1, lambda: self.set_pad_off(x_display, y)).start()
                else:
                    # Clear display on release
                    self.send_7seg_text("")

        elif cmd == SysExCommand.BUTTON_EVENT:  # BUTTON_EVENT
            if len(data) >= 9:
                button_id = data[6]
                state = data[7]
                button_name = BUTTON_NAMES.get(button_id, f"BUTTON_{button_id}")
                action = "pressed" if state > 0 else "released"
                self.log_event("BUTTON", f"{button_name} {action} - ID {button_id}")

                if state > 0:
                    # Show on 7-seg
                    self.send_7seg_text(f"B{button_id:02d}")
                    # Visual feedback: light up button LED on press
                    self.set_button_led(button_id, True)
                    threading.Timer(
                        0.1, lambda: self.set_button_led(button_id, False)
                    ).start()
                else:
                    # Clear display on release
                    self.send_7seg_text("")

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
        print("\n" + "=" * 70)
        print("CONTROLLER MODE TEST - INTERACTIVE MODE")
        print("=" * 70)
        print("\nCommands:")
        print("  grid     - Show test grid pattern")
        print("  rainbow  - Rainbow wave animation")
        print("  spiral   - Spiral animation")
        print("  pulse    - Pulse animation")
        print("  clear    - Clear all pads")
        print("  buttons  - Test all button LEDs")
        print("  corner   - Light up corner pads")
        print("  quit     - Exit")
        print("\nPress pads, buttons, or turn encoders on Deluge to see events logged.")
        print("=" * 70 + "\n")

        while self.running:
            try:
                cmd = input(">>> ").strip().lower()

                if cmd == "quit" or cmd == "exit" or cmd == "q":
                    self.running = False
                    break

                elif cmd == "grid":
                    self.animation_test_grid()

                elif cmd == "rainbow":
                    self.animation_rainbow_wave(duration=5.0)

                elif cmd == "spiral":
                    self.animation_spiral(duration=3.0)

                elif cmd == "pulse":
                    self.animation_pulse(duration=3.0)

                elif cmd == "clear":
                    self.clear_all_pads()
                    self.log_event("COMMAND", "Cleared all pads")

                elif cmd == "buttons":
                    self.log_event("COMMAND", "Testing all button LEDs")
                    for note in BUTTON_NAMES.keys():
                        self.set_button_led(note, True)
                        time.sleep(0.1)
                    time.sleep(0.5)
                    for note in BUTTON_NAMES.keys():
                        self.set_button_led(note, False)
                        time.sleep(0.1)

                elif cmd == "corner":
                    self.log_event("COMMAND", "Lighting corner pads")
                    corners = [
                        (0, 0, "RED"),
                        (15, 0, "GREEN"),
                        (0, 7, "BLUE"),
                        (15, 7, "YELLOW"),
                    ]
                    for x, y, color in corners:
                        self.set_pad_color(x, y, color)

                elif cmd == "":
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
        print("=" * 70)
        print("DELUGE CONTROLLER MODE TEST SCRIPT")
        print("=" * 70)

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
