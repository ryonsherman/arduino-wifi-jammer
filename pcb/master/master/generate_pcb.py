#!/usr/bin/env python3
"""
Generate KiCad PCB file for Master+Power board.

Components:
- Arduino Nano socket (2×15 pin socket, 2.54mm pitch)
- LM2596 socket (4 corner pins: 8mm × 17mm spacing)
- 36V input header (1×2)
- 5V output header (1×2)
- I2C header (1×4: SDA, SCL, 5V, GND)
- 2× 4.7kΩ pullup resistors (through-hole)

Board: 65mm × 75mm, 2 layer, 1.6mm thick
"""

import uuid

def gen_uuid():
    return str(uuid.uuid4())

# Pin spacing
PITCH = 2.54  # mm

# Board dimensions in mm
# Tightened layout - still solderable but less wasted space

BOARD_W = 50.0 - (3 * PITCH)  # 1 pin in from left, 1 pin in from right, plus 1 more from left
BOARD_H = 55.0 - PITCH  # 1 pin down from top

# Board edge offsets (to shift components relative to new smaller board)
BOARD_LEFT_TRIM = 2 * PITCH - 0.5  # 2 pins trimmed from left, nudged right 0.5mm
BOARD_TOP_TRIM = PITCH   # 1 pin trimmed from top

# Calculate bounding box of all components to center them
# Current positions (before centering):
_NANO_X = 37.0
_NANO_Y = 22.5
_LM2596_X = 15.0 + PITCH - 1.0  # moved right 1 pin, then left 1mm to align with resistors
_LM2596_Y = 19.5 + (2 * PITCH)  # moved down 2 pins total
_H36V_X = 10.0
_H5V_X = 22.0 - PITCH  # J2 moved left 1 pin
_I2C_X = 38.0 - (2 * PITCH)  # J3 moved left 2 pins
_HEADER_Y = 50.0
_I2C_Y = 50.0  # J3 same Y as other headers
_R1_X = 10.0 + PITCH  # moved right 1 pin
_R1_Y = 38.0 + PITCH  # moved down 1 pin
_R2_X = 10.0 + PITCH  # moved right 1 pin
_R2_Y = 42.0 + PITCH  # moved down 1 pin

# Capacitor above U2
_CAP_X = _LM2596_X - 1  # centered with U2 (offset for 2mm lead spacing)
_CAP_Y = _LM2596_Y - 16  # above U2, more space

# Find bounding box (approximate - Nano is widest at ~15mm, headers extend ~10mm)
_min_x = min(_R1_X, _H36V_X, _LM2596_X - 4) - 2  # left edge with margin
_max_x = max(_NANO_X + 8, _I2C_X + 5)  # right edge (Nano ~15mm wide)
_min_y = min(_LM2596_Y - 9, _NANO_Y - 18)  # top edge (Nano ~36mm tall)
_max_y = _HEADER_Y + 3  # bottom edge

_content_w = _max_x - _min_x
_content_h = _max_y - _min_y

# Offset to center content on board
# Use original board size for centering calculation, then apply trim offsets
_orig_board_w = 50.0
_orig_board_h = 55.0
_offset_x = (_orig_board_w - _content_w) / 2 - _min_x
_offset_y = (_orig_board_h - _content_h) / 2 - _min_y

# Adjust for trimmed board edges (shift components left and up)
_offset_x -= BOARD_LEFT_TRIM
_offset_y -= BOARD_TOP_TRIM

# Apply offset to all components
NANO_X = _NANO_X + _offset_x
NANO_Y = _NANO_Y + _offset_y + (1 * PITCH) + 1.32  # moved down 1 pin + 1.32mm to align with C1

LM2596_X = _LM2596_X + _offset_x
LM2596_Y = _LM2596_Y + _offset_y
LM2596_ROT = 90

HEADER_Y = _HEADER_Y + _offset_y
H36V_X = _H36V_X + _offset_x
H5V_X = _H5V_X + _offset_x
I2C_X = _I2C_X + _offset_x
I2C_Y = _I2C_Y + _offset_y
HEADER_ROT = 90

R1_X = _R1_X + _offset_x
R1_Y = _R1_Y + _offset_y
R2_X = _R2_X + _offset_x
R2_Y = _R2_Y + _offset_y

CAP_X = _CAP_X + _offset_x
CAP_Y = _CAP_Y + _offset_y

def generate_pcb():
    pcb = f'''(kicad_pcb
	(version 20241229)
	(generator "generate_pcb.py")
	(generator_version "1.0")
	(general
		(thickness 1.6)
		(legacy_teardrops no)
	)
	(paper "A4")
	(title_block
		(title "WiFi Jammer Master+Power Board")
		(rev "1.0")
	)
	(layers
		(0 "F.Cu" signal)
		(31 "B.Cu" signal)
		(32 "B.Adhes" user "B.Adhesive")
		(33 "F.Adhes" user "F.Adhesive")
		(34 "B.Paste" user)
		(35 "F.Paste" user)
		(36 "B.SilkS" user "B.Silkscreen")
		(37 "F.SilkS" user "F.Silkscreen")
		(38 "B.Mask" user)
		(39 "F.Mask" user)
		(40 "Dwgs.User" user "User.Drawings")
		(41 "Cmts.User" user "User.Comments")
		(42 "Eco1.User" user "User.Eco1")
		(43 "Eco2.User" user "User.Eco2")
		(44 "Edge.Cuts" user)
		(45 "Margin" user)
		(46 "B.CrtYd" user "B.Courtyard")
		(47 "F.CrtYd" user "F.Courtyard")
		(48 "B.Fab" user)
		(49 "F.Fab" user)
	)
	(setup
		(pad_to_mask_clearance 0)
		(allow_soldermask_bridges_in_footprints no)
		(tenting front back)
		(pcbplotparams
			(layerselection 0x00010fc_ffffffff)
			(plot_on_all_layers_selection 0x0000000_00000000)
			(disableapertmacros no)
			(usegerberextensions no)
			(usegerberattributes yes)
			(usegerberadvancedattributes yes)
			(creategerberjobfile yes)
			(svgprecision 4)
			(plotframeref no)
			(mode 1)
			(useauxorigin no)
			(hpglpennumber 1)
			(hpglpenspeed 20)
			(hpglpendiameter 15.000000)
			(pdf_front_fp_property_popups yes)
			(pdf_back_fp_property_popups yes)
			(pdf_metadata yes)
			(dxfpolygonmode yes)
			(dxfimperialunits yes)
			(dxfusepcbnewfont yes)
			(psnegative no)
			(psa4output no)
			(plotreference yes)
			(plotvalue yes)
			(plotfptext yes)
			(plotinvisibletext no)
			(sketchpadsonfab no)
			(subtractmaskfromsilk no)
			(outputformat 1)
			(mirror no)
			(drillshape 1)
			(scaleselection 1)
			(outputdirectory "")
		)
	)
	(net 0 "")
	(net 1 "GND")
	(net 2 "5V")
	(net 3 "36V")
	(net 4 "SDA")
	(net 5 "SCL")
'''
    
    # Board outline
    pcb += f'''
	(gr_rect
		(start 0 0)
		(end {BOARD_W} {BOARD_H})
		(stroke
			(width 0.15)
			(type solid)
		)
		(fill none)
		(layer "Edge.Cuts")
		(uuid "{gen_uuid()}")
	)
'''
    
    # Arduino Nano socket (2x15)
    pcb += generate_nano_socket("U1", NANO_X, NANO_Y)
    
    # LM2596 socket (rotated 90 degrees)
    pcb += generate_lm2596("U2", LM2596_X, LM2596_Y, LM2596_ROT)
    
    # Headers (rotated 90 degrees, along bottom edge)
    pcb += generate_header_1x2("J1", H36V_X, HEADER_Y, "36V_IN", HEADER_ROT)
    pcb += generate_header_1x2("J2", H5V_X, HEADER_Y, "5V_OUT", HEADER_ROT)
    pcb += generate_header_1x4("J3", I2C_X, I2C_Y, "I2C", HEADER_ROT)
    
    # Resistors
    pcb += generate_resistor("R1", R1_X, R1_Y, "4.7k")
    pcb += generate_resistor("R2", R2_X, R2_Y, "4.7k")
    
    # Capacitor
    pcb += generate_capacitor("C1", CAP_X, CAP_Y, "100uF")
    
    pcb += ")\n"
    return pcb

def generate_nano_socket(ref, x, y):
    """2x15 pin socket for Arduino Nano (15.24mm row spacing)"""
    row_spacing = 15.24 / 2  # half spacing from center
    uuid_fp = gen_uuid()
    
    pads = ""
    # Left row (pins 1-15)
    for i in range(15):
        pin = i + 1
        py = -17.78 + i * PITCH
        px = -row_spacing
        shape = "rect" if pin == 1 else "circle"
        pads += f'''
		(pad "{pin}" thru_hole {shape}
			(at {px:.2f} {py:.2f})
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)'''
    
    # Right row (pins 16-30, bottom to top)
    for i in range(15):
        pin = 30 - i
        py = -17.78 + i * PITCH
        px = row_spacing
        pads += f'''
		(pad "{pin}" thru_hole circle
			(at {px:.2f} {py:.2f})
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)'''
    
    return f'''
	(footprint "Connector_PinSocket_2.54mm:PinSocket_2x15_P2.54mm_Vertical"
		(layer "F.Cu")
		(uuid "{uuid_fp}")
		(at {x} {y})
		(property "Reference" "{ref}"
			(at 0 -20.78 0)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" ""
			(at 0 22 0)
			(layer "F.SilkS")
			(hide yes)
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(attr through_hole){pads}
	)
'''

def generate_lm2596(ref, x, y, rot=0):
    """LM2596 buck converter socket (4 pins, 8mm x 17mm spacing)"""
    hx = 17.0 / 2
    hy = 8.0 / 2
    uuid_fp = gen_uuid()
    
    # When rotated 90°, local Y becomes global X
    # Reference above footprint (negative Y in local coords)
    # Value (LM2596) below footprint (positive Y in local coords)
    # For 90° rotation: above = -Y local = -X global, below = +Y local = +X global
    ref_y = -6 if rot == 0 else 0
    ref_x = 0 if rot == 0 else 12
    val_y = 6 if rot == 0 else 0
    val_x = 0 if rot == 0 else -12
    
    return f'''
	(footprint "Custom:LM2596_Socket"
		(layer "F.Cu")
		(uuid "{uuid_fp}")
		(at {x} {y} {rot})
		(property "Reference" "{ref}"
			(at {ref_x} {ref_y} 0)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" "LM2596"
			(at {val_x} {val_y} 0)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(attr through_hole)
		(fp_rect
			(start -10.5 -4)
			(end 10.5 4)
			(stroke
				(width 0.12)
				(type solid)
			)
			(fill none)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
		)
		(pad "1" thru_hole rect
			(at {-hx:.2f} {-hy:.2f})
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
		(pad "2" thru_hole circle
			(at {-hx:.2f} {hy:.2f})
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
		(pad "3" thru_hole circle
			(at {hx:.2f} {-hy:.2f})
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
		(pad "4" thru_hole circle
			(at {hx:.2f} {hy:.2f})
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
	)
'''

def generate_header_1x2(ref, x, y, label, rot=0):
    """1x2 pin header"""
    uuid_fp = gen_uuid()
    
    # Header is 2 pins, pin 1 at (0,0), pin 2 at (0, 2.54) in local coords
    # Local center of 2-pin header: (0, 1.27)
    center_y = PITCH / 2  # 1.27
    
    # For 90° rotation: local -X becomes global +Y (below), local +X becomes global -Y (above)
    # So to put reference "above" (global -Y), we need local +X
    # And to put value "below" (global +Y), we need local -X
    if rot == 90:
        ref_x = 2.5
        ref_y = center_y
        val_x = -3.5
        val_y = center_y
    else:
        ref_x = 0
        ref_y = center_y - 4
        val_x = 0
        val_y = center_y + 5
    
    return f'''
	(footprint "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
		(layer "F.Cu")
		(uuid "{uuid_fp}")
		(at {x} {y} {rot})
		(property "Reference" "{ref}"
			(at {ref_x:.2f} {ref_y:.2f} 0)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" "{label}"
			(at {val_x:.2f} {val_y:.2f} 0)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(attr through_hole)
		(pad "1" thru_hole rect
			(at 0 0)
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
		(pad "2" thru_hole circle
			(at 0 {PITCH:.2f})
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
	)
'''

def generate_header_1x4(ref, x, y, label, rot=0):
    """1x4 pin header for I2C with pin labels"""
    uuid_fp = gen_uuid()
    pads = ""
    for i in range(4):
        shape = "rect" if i == 0 else "circle"
        pads += f'''
		(pad "{i+1}" thru_hole {shape}
			(at 0 {i * PITCH:.2f})
			(size 1.7 1.7)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)'''
    
    # Pin labels: SDA, SCL, 5V, GND (pins 1-4)
    # For 90° rotation: local -X becomes global +Y (below in top-down view)
    # Labels rotated 90° so text reads vertically
    pin_labels = ["SDA", "SCL", "5V", "GND"]
    labels_str = ""
    for i, lbl in enumerate(pin_labels):
        # Local coords: pin i is at (0, i*PITCH)
        # Label below pin in global coords = local -X direction
        labels_str += f'''
		(fp_text user "{lbl}"
			(at -3.5 {i * PITCH:.2f} 90)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 0.8 0.8)
					(thickness 0.12)
				)
			)
		)'''
    
    # Center of 4-pin header in local coords: (0, 1.5*PITCH) = (0, 3.81)
    center_y = 1.5 * PITCH
    
    # For 90° rotation: local +X = global -Y (above), local -X = global +Y (below)
    if rot == 90:
        ref_x = 2.5  # aligned with J1/J2
        ref_y = center_y
    else:
        ref_x = 0
        ref_y = center_y - 6
    
    return f'''
	(footprint "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"
		(layer "F.Cu")
		(uuid "{uuid_fp}")
		(at {x} {y} {rot})
		(property "Reference" "{ref}"
			(at {ref_x:.2f} {ref_y:.2f} 0)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" ""
			(at 0 12 0)
			(layer "F.SilkS")
			(hide yes)
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(attr through_hole){labels_str}{pads}
	)
'''

def generate_resistor(ref, x, y, value):
    """Through-hole resistor (7.62mm lead spacing, 3 pins)"""
    uuid_fp = gen_uuid()
    lead_spacing = 7.62
    
    return f'''
	(footprint "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"
		(layer "F.Cu")
		(uuid "{uuid_fp}")
		(at {x} {y})
		(property "Reference" "{ref}"
			(at -3 0 0)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" ""
			(at 5.08 3.5 0)
			(layer "F.Fab")
			(hide yes)
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(attr through_hole)
		(pad "1" thru_hole rect
			(at 0 0)
			(size 1.6 1.6)
			(drill 0.8)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
		(pad "2" thru_hole circle
			(at {lead_spacing:.2f} 0)
			(size 1.6 1.6)
			(drill 0.8)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
	)
'''

def generate_capacitor(ref, x, y, value):
    """Through-hole radial electrolytic capacitor (2mm lead spacing)"""
    uuid_fp = gen_uuid()
    lead_spacing = 2.0
    
    return f'''
	(footprint "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm"
		(layer "F.Cu")
		(uuid "{uuid_fp}")
		(at {x} {y})
		(property "Reference" "{ref}"
			(at {lead_spacing/2} -3.0 0)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" ""
			(at {lead_spacing/2} 3.5 0)
			(layer "F.SilkS")
			(hide yes)
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(attr through_hole)
		(pad "1" thru_hole rect
			(at 0 0)
			(size 1.6 1.6)
			(drill 0.8)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
		(pad "2" thru_hole circle
			(at {lead_spacing:.2f} 0)
			(size 1.6 1.6)
			(drill 0.8)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)
	)
'''

def main():
    pcb = generate_pcb()
    
    with open("master.kicad_pcb", "w") as f:
        f.write(pcb)
    
    print("Generated master.kicad_pcb")
    print(f"Board size: {BOARD_W}mm × {BOARD_H}mm")
    print("Components:")
    print(f"  U1: Arduino Nano socket at ({NANO_X}, {NANO_Y})")
    print(f"  U2: LM2596 socket at ({LM2596_X}, {LM2596_Y})")
    print(f"  J1: 36V input header at ({H36V_X}, {HEADER_Y})")
    print(f"  J2: 5V output header at ({H5V_X}, {HEADER_Y})")
    print(f"  J3: I2C header at ({I2C_X}, {HEADER_Y})")
    print(f"  R1, R2: 4.7kΩ pullups at ({R1_X}, {R1_Y}) and ({R2_X}, {R2_Y})")
    print(f"  C1: 100µF capacitor at ({CAP_X}, {CAP_Y})")

if __name__ == "__main__":
    main()
