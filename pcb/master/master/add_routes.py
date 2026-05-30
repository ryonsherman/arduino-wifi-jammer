#!/usr/bin/env python3
"""Add copper traces to master.kicad_pcb for routing."""

import uuid
import re

def gen_uuid():
    return str(uuid.uuid4())

# Read the existing PCB file
with open('master.kicad_pcb', 'r') as f:
    content = f.read()

# Component positions (from generate_pcb.py and the file):
# U1 (Nano socket) at (35.5, 23.79), no rotation
#   Pin 6 (SCL): footprint offset (-7.62, -5.08) -> absolute (27.88, 18.71)
#   Pin 7 (SDA): footprint offset (-7.62, -2.54) -> absolute (27.88, 21.25)
#   Pin 17 (GND): footprint offset (7.62, 15.24) -> absolute (43.12, 39.03)
#   Pin 18 (5V): footprint offset (7.62, 12.70) -> absolute (43.12, 36.49)

# U2 (LM2596) at (13.5, 18.25), rotated 90°
#   Rotation 90° means: new_x = -old_y, new_y = old_x
#   Pin 1 (36V): footprint offset (-8.50, -4.00) -> rotated (4.00, -8.50) -> absolute (17.5, 9.75)
#   Pin 2 (GND): footprint offset (-8.50, 4.00) -> rotated (-4.00, -8.50) -> absolute (9.5, 9.75)
#   Pin 3 (5V): footprint offset (8.50, -4.00) -> rotated (4.00, 8.50) -> absolute (17.5, 26.75)
#   Pin 4 (GND): footprint offset (8.50, 4.00) -> rotated (-4.00, 8.50) -> absolute (9.5, 26.75)

# J1 (36V input) at (8.5, 48.75), rotated 90°
#   Pin 1 (36V): footprint offset (0, 0) -> rotated (0, 0) -> absolute (8.5, 48.75)
#   Pin 2 (GND): footprint offset (0, 2.54) -> rotated (-2.54, 0) -> absolute (5.96, 48.75)

# J2 (5V output) at (20.5, 48.75), rotated 90°
#   Pin 1 (5V): footprint offset (0, 0) -> rotated (0, 0) -> absolute (20.5, 48.75)
#   Pin 2 (GND): footprint offset (0, 2.54) -> rotated (-2.54, 0) -> absolute (17.96, 48.75)

# J3 (I2C) at (36.5, 48.75), rotated 90°
#   Pin 1 (SDA): footprint offset (0, 0) -> absolute (36.5, 48.75)
#   Pin 2 (SCL): footprint offset (0, 2.54) -> rotated (-2.54, 0) -> absolute (33.96, 48.75)
#   Pin 3 (5V): footprint offset (0, 5.08) -> rotated (-5.08, 0) -> absolute (31.42, 48.75)
#   Pin 4 (GND): footprint offset (0, 7.62) -> rotated (-7.62, 0) -> absolute (28.88, 48.75)

# R1 at (8.5, 36.75), no rotation
#   Pin 1 (5V): (8.5, 36.75)
#   Pin 2 (SDA): (8.5 + 10.16, 36.75) = (18.66, 36.75)

# R2 at (8.5, 40.75), no rotation
#   Pin 1 (5V): (8.5, 40.75)
#   Pin 2 (SCL): (18.66, 40.75)

# Track width and layer
track_width = 0.5
layer_f = "F.Cu"
layer_b = "B.Cu"

# Generate tracks
tracks = []

def add_track(start, end, net, layer=layer_f, width=track_width):
    tracks.append(f'''	(segment
		(start {start[0]:.2f} {start[1]:.2f})
		(end {end[0]:.2f} {end[1]:.2f})
		(width {width})
		(layer "{layer}")
		(net {net})
		(uuid "{gen_uuid()}")
	)''')

def add_via(pos, net):
    tracks.append(f'''	(via
		(at {pos[0]:.2f} {pos[1]:.2f})
		(size 0.8)
		(drill 0.4)
		(layers "F.Cu" "B.Cu")
		(net {net})
		(uuid "{gen_uuid()}")
	)''')

# Net numbers:
# 1 = GND, 2 = 5V, 3 = 36V, 4 = SDA, 5 = SCL

# === 36V Net (net 3) ===
# J1.1 (8.5, 48.75) -> U2.1 (17.5, 9.75)
add_track((8.5, 48.75), (8.5, 9.75), 3)
add_track((8.5, 9.75), (17.5, 9.75), 3)

# === GND Net (net 1) ===
# Main GND bus along bottom edge
# J1.2 (5.96, 48.75) -> J2.2 (17.96, 48.75) -> J3.4 (28.88, 48.75)
add_track((5.96, 48.75), (17.96, 48.75), 1)
add_track((17.96, 48.75), (28.88, 48.75), 1)

# U2.2 (9.5, 9.75) -> down to GND bus
add_track((9.5, 9.75), (5.96, 9.75), 1)
add_track((5.96, 9.75), (5.96, 48.75), 1)

# U2.4 (9.5, 26.75) -> left and down to GND bus
add_track((9.5, 26.75), (5.96, 26.75), 1)
add_track((5.96, 26.75), (5.96, 48.75), 1)  # Already connected above

# U1.17 (43.12, 39.03) -> via to back, route to GND bus
add_via((43.12, 39.03), 1)
add_track((43.12, 39.03), (43.12, 52.0), 1, layer_b)  # On back layer, go down
add_track((43.12, 52.0), (28.88, 52.0), 1, layer_b)   # Along bottom
add_via((28.88, 52.0), 1)
add_track((28.88, 52.0), (28.88, 48.75), 1)  # Up to J3.4

# === 5V Net (net 2) ===
# U2.3 (17.5, 26.75) is 5V output
# Connect to: J2.1 (20.5, 48.75), J3.3 (31.42, 48.75), U1.18 (43.12, 36.49), R1.1 (8.5, 36.75), R2.1 (8.5, 40.75)

# U2.3 -> J2.1
add_track((17.5, 26.75), (20.5, 26.75), 2)
add_track((20.5, 26.75), (20.5, 48.75), 2)

# J2.1 -> J3.3 (along y=48.75)
add_track((20.5, 48.75), (31.42, 48.75), 2)

# J3.3 -> U1.18 (need to route around)
add_track((31.42, 48.75), (31.42, 36.49), 2)
add_track((31.42, 36.49), (43.12, 36.49), 2)

# R1.1 and R2.1 share 5V - connect them first
add_track((8.5, 36.75), (8.5, 40.75), 2)

# R1.1 -> U2.3 5V line (via back layer to avoid crossing other traces)
add_via((8.5, 36.75), 2)
add_track((8.5, 36.75), (8.5, 26.75), 2, layer_b)
add_track((8.5, 26.75), (17.5, 26.75), 2, layer_b)
add_via((17.5, 26.75), 2)

# === SDA Net (net 4) ===
# U1.7 (27.88, 21.25) -> J3.1 (36.5, 48.75) -> R1.2 (18.66, 36.75)

# U1.7 -> J3.1
add_track((27.88, 21.25), (27.88, 45.0), 4)
add_track((27.88, 45.0), (36.5, 45.0), 4)
add_track((36.5, 45.0), (36.5, 48.75), 4)

# R1.2 -> SDA line (via back to avoid crossing)
add_via((18.66, 36.75), 4)
add_track((18.66, 36.75), (27.88, 36.75), 4, layer_b)
add_track((27.88, 36.75), (27.88, 45.0), 4, layer_b)  # Connect to via point
add_via((27.88, 45.0), 4)

# === SCL Net (net 5) ===
# U1.6 (27.88, 18.71) -> J3.2 (33.96, 48.75) -> R2.2 (18.66, 40.75)

# U1.6 -> J3.2
add_track((27.88, 18.71), (25.0, 18.71), 5)
add_track((25.0, 18.71), (25.0, 46.5), 5)
add_track((25.0, 46.5), (33.96, 46.5), 5)
add_track((33.96, 46.5), (33.96, 48.75), 5)

# R2.2 -> SCL line (via back)
add_via((18.66, 40.75), 5)
add_track((18.66, 40.75), (25.0, 40.75), 5, layer_b)
add_track((25.0, 40.75), (25.0, 46.5), 5, layer_b)
add_via((25.0, 46.5), 5)

# Join all tracks
tracks_str = '\n'.join(tracks)

# Insert before the final closing paren
content = content.rstrip()
if content.endswith(')'):
    content = content[:-1] + '\n' + tracks_str + '\n)'

with open('master.kicad_pcb', 'w') as f:
    f.write(content)

print(f"Added {len(tracks)} routing elements (tracks and vias)")
print("Routes added for nets: 36V, GND, 5V, SDA, SCL")
