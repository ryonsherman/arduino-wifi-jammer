#!/usr/bin/env python3
"""
Generate Slave Carrier PCB layout for KiCad 10.0
- 6x MH-Tiny sockets (2x15 female headers, Nano footprint)
- 6x SPI headers (1x5 male) opposite USB
- 1x I2C header (1x4)
- 1x Power header (1x2) for 5V/GND
- All headers, no other components
"""

import uuid

def gen_uuid():
    return str(uuid.uuid4())

# Standard 2.54mm (0.1") pitch
PITCH = 2.54

# MH-Tiny socket: 2x15 pins, 15.24mm row spacing (600 mil)
MH_ROWS = 2
MH_COLS = 15
MH_ROW_SPACING = 15.24  # mm between the two rows

# Spacing between MH-Tiny sockets (center to center)
# Socket width: 15.24mm, board overhang: 1.27mm each side, 1 pin gap: 2.54mm
# Total: 15.24 + 2.54 + 2*1.27 = 20.32mm
MH_SPACING = 20.32  # mm - accounts for board overhang + 1 pin gap

# Board dimensions
NUM_MH = 6
BOARD_WIDTH = (NUM_MH - 1) * MH_SPACING + MH_ROW_SPACING + 6 + 15 - PITCH  # ~135mm - shortened by 1 pin
BOARD_HEIGHT = MH_COLS * PITCH + 12  # ~50mm (15 pins + margins + SPI header)

# Starting positions
BOARD_LEFT = 0
BOARD_BOTTOM = 0
MARGIN = 3.0

# MH-Tiny sockets start position (first socket center)
MH_START_X = MARGIN + MH_ROW_SPACING / 2 + 1
MH_START_Y = MARGIN + 3 + PITCH * 7  # Center of the 15-pin column

# SPI header offset from MH-Tiny (above, opposite USB)
SPI_OFFSET_Y = (MH_COLS - 1) / 2 * PITCH + 5  # Above the top of MH-Tiny

# I2C header position (right edge, moved left to make room for labels on right)
I2C_X = BOARD_WIDTH - MARGIN - 8 + PITCH  # shifted 1 pin right
I2C_Y = BOARD_HEIGHT / 2

# Power header position (right edge, below I2C)
PWR_X = I2C_X
PWR_Y = I2C_Y - 15


def generate_header():
    """Generate KiCad PCB file header - matching KiCad 10 format"""
    return '''(kicad_pcb
	(version 20260206)
	(generator "pcbnew")
	(generator_version "10.0")
	(general
		(thickness 1.6)
		(legacy_teardrops no)
	)
	(paper "A4")
	(title_block
		(title "WiFi Jammer Slave Carrier Board")
		(rev "1.0")
	)
	(layers
		(0 "F.Cu" signal)
		(2 "B.Cu" signal)
		(9 "F.Adhes" user "F.Adhesive")
		(11 "B.Adhes" user "B.Adhesive")
		(13 "F.Paste" user)
		(15 "B.Paste" user)
		(5 "F.SilkS" user "F.Silkscreen")
		(7 "B.SilkS" user "B.Silkscreen")
		(1 "F.Mask" user)
		(3 "B.Mask" user)
		(17 "Dwgs.User" user "User.Drawings")
		(19 "Cmts.User" user "User.Comments")
		(21 "Eco1.User" user "User.Eco1")
		(23 "Eco2.User" user "User.Eco2")
		(25 "Edge.Cuts" user)
		(27 "Margin" user)
		(31 "F.CrtYd" user "F.Courtyard")
		(29 "B.CrtYd" user "B.Courtyard")
		(35 "F.Fab" user)
		(33 "B.Fab" user)
	)
	(setup
		(pad_to_mask_clearance 0)
		(allow_soldermask_bridges_in_footprints no)
		(tenting
			(front yes)
			(back yes)
		)
		(covering
			(front no)
			(back no)
		)
		(plugging
			(front no)
			(back no)
		)
		(capping no)
		(filling no)
		(pcbplotparams
			(layerselection 0x00000000_00000000_5555555d_ffffffff)
			(plot_on_all_layers_selection 0x00000000_00000000_00000000_00000000)
			(disableapertmacros no)
			(usegerberextensions no)
			(usegerberattributes yes)
			(usegerberadvancedattributes yes)
			(creategerberjobfile yes)
			(dashed_line_dash_ratio 12)
			(dashed_line_gap_ratio 3)
			(svgprecision 4)
			(plotframeref no)
			(mode 1)
			(useauxorigin no)
			(pdf_front_fp_property_popups yes)
			(pdf_back_fp_property_popups yes)
			(pdf_metadata yes)
			(pdf_single_document no)
			(dxfpolygonmode yes)
			(dxfimperialunits yes)
			(dxfusepcbnewfont yes)
			(psnegative no)
			(psa4output no)
			(plot_black_and_white yes)
			(sketchpadsonfab no)
			(plotpadnumbers no)
			(hidednponfab no)
			(sketchdnponfab yes)
			(crossoutdnponfab yes)
			(subtractmaskfromsilk no)
			(outputformat 1)
			(mirror no)
			(drillshape 0)
			(scaleselection 1)
			(outputdirectory "")
		)
	)
'''


def generate_2x15_socket(ref, x, y, rotation=0):
    """Generate a 2x15 female header socket (MH-Tiny/Nano footprint)"""
    pads = []
    pad_num = 1
    
    # Left column (pins 1-15, bottom to top)
    for row in range(MH_COLS):
        px = -MH_ROW_SPACING / 2
        py = (MH_COLS / 2 - 0.5 - row) * PITCH
        pads.append(f'''		(pad "{pad_num}" thru_hole oval
			(at {px:.3f} {py:.3f})
			(size 1.7 1.7)
			(drill 1.0)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)''')
        pad_num += 1
    
    # Right column (pins 16-30, top to bottom)  
    for row in range(MH_COLS):
        px = MH_ROW_SPACING / 2
        py = -(MH_COLS / 2 - 0.5 - row) * PITCH
        pads.append(f'''		(pad "{pad_num}" thru_hole oval
			(at {px:.3f} {py:.3f})
			(size 1.7 1.7)
			(drill 1.0)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)''')
        pad_num += 1
    
    pads_str = "\n".join(pads)
    
    # Courtyard and silkscreen
    cx1 = -MH_ROW_SPACING / 2 - 1.5
    cx2 = MH_ROW_SPACING / 2 + 1.5
    cy1 = -MH_COLS / 2 * PITCH - 1
    cy2 = MH_COLS / 2 * PITCH + 1
    
    return f'''
	(footprint "Connector_PinSocket_2.54mm:PinSocket_2x15_P2.54mm_Vertical"
		(layer "F.Cu")
		(uuid "{gen_uuid()}")
		(at {x:.3f} {y:.3f} {rotation})
		(property "Reference" "{ref}"
			(at 0 {cy1 - 2:.3f} {rotation})
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
			(at 0 {cy2 + 2:.3f} {rotation})
			(layer "F.Fab")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(fp_rect
			(start {cx1:.3f} {cy1:.3f})
			(end {cx2:.3f} {cy2:.3f})
			(stroke
				(width 0.12)
				(type solid)
			)
			(fill none)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
		)
		(fp_rect
			(start {cx1 - 0.25:.3f} {cy1 - 0.25:.3f})
			(end {cx2 + 0.25:.3f} {cy2 + 0.25:.3f})
			(stroke
				(width 0.05)
				(type solid)
			)
			(fill none)
			(layer "F.CrtYd")
			(uuid "{gen_uuid()}")
		)
{pads_str}
	)
'''


def generate_1xN_header(ref, x, y, num_pins, rotation=0, value="Header", pin_labels=None):
    """Generate a 1xN male pin header, optionally with pin labels"""
    pads = []
    for i in range(num_pins):
        py = (num_pins / 2 - 0.5 - i) * PITCH
        pads.append(f'''		(pad "{i + 1}" thru_hole oval
			(at 0 {py:.3f})
			(size 1.7 1.7)
			(drill 1.0)
			(layers "*.Cu" "*.Mask")
			(remove_unused_layers no)
			(uuid "{gen_uuid()}")
		)''')
    
    pads_str = "\n".join(pads)
    
    # Generate pin labels as fp_text if provided
    labels_str = ""
    if pin_labels:
        for i, label in enumerate(pin_labels):
            py = (num_pins / 2 - 0.5 - i) * PITCH
            labels_str += f'''
		(fp_text user "{label}"
			(at 4 {py:.3f} {rotation})
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 0.8 0.8)
					(thickness 0.12)
				)
				(justify left)
			)
		)'''
    
    cy1 = -num_pins / 2 * PITCH - 1
    cy2 = num_pins / 2 * PITCH + 1
    
    return f'''
	(footprint "Connector_PinHeader_2.54mm:PinHeader_1x{num_pins:02d}_P2.54mm_Vertical"
		(layer "F.Cu")
		(uuid "{gen_uuid()}")
		(at {x:.3f} {y:.3f} {rotation})
		(property "Reference" "{ref}"
			(at 0 {cy1 - 2:.3f} {rotation})
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" "{value}"
			(at 0 {cy2 + 2:.3f} {rotation})
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(fp_rect
			(start -1.5 {cy1:.3f})
			(end 1.5 {cy2:.3f})
			(stroke
				(width 0.12)
				(type solid)
			)
			(fill none)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
		)
		(fp_rect
			(start -1.75 {cy1 - 0.25:.3f})
			(end 1.75 {cy2 + 0.25:.3f})
			(stroke
				(width 0.05)
				(type solid)
			)
			(fill none)
			(layer "F.CrtYd")
			(uuid "{gen_uuid()}")
		){labels_str}
{pads_str}
	)
'''


def generate_board_outline():
    """Generate board edge cuts"""
    return f'''
	(gr_rect
		(start {BOARD_LEFT:.3f} {BOARD_BOTTOM:.3f})
		(end {BOARD_WIDTH:.3f} {BOARD_HEIGHT:.3f})
		(stroke
			(width 0.15)
			(type solid)
		)
		(fill none)
		(layer "Edge.Cuts")
		(uuid "{gen_uuid()}")
	)
'''


def generate_labels():
    """Generate silkscreen labels as a footprint for consistent rendering"""
    # SPI pinout reference as a footprint
    spi_pins = ["CE", "CSN", "MOSI", "MISO", "SCK"]
    labels_str = ""
    for i, pin in enumerate(spi_pins):
        labels_str += f'''
		(fp_text user "{pin}"
			(at {-i * 1.8:.3f} 0 90)
			(layer "F.SilkS")
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 0.8 0.8)
					(thickness 0.12)
				)
				(justify left)
			)
		)'''
    
    x = BOARD_WIDTH - MARGIN - 2 + PITCH  # 1 pin right
    y = BOARD_HEIGHT - MARGIN - 5 + 1 * PITCH  # 1 pin down (moved 1 up from 2)
    
    return f'''
	(footprint "SPI_Reference"
		(layer "F.Cu")
		(uuid "{gen_uuid()}")
		(at {x:.3f} {y:.3f})
		(property "Reference" "SPI"
			(at {-2 * 1.8:.3f} {6 - 2 * PITCH + 1:.3f} 0)
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
			(at 0 0 0)
			(layer "F.Fab")
			(hide yes)
			(uuid "{gen_uuid()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		){labels_str}
	)
'''


def main():
    pcb = generate_header()
    
    # Generate 6 MH-Tiny sockets
    for i in range(NUM_MH):
        x = MH_START_X + i * MH_SPACING
        y = MH_START_Y
        pcb += generate_2x15_socket(f"U{i + 1}", x, y)
    
    # Generate 6 SPI headers (1x5) directly below each MH-Tiny
    for i in range(NUM_MH):
        x = MH_START_X + i * MH_SPACING  # Centered under MH-Tiny
        y = MH_START_Y + SPI_OFFSET_Y
        pcb += generate_1xN_header(f"J{i + 1}", x, y, 5, rotation=90, value="SPI")
    
    # I2C header (1x4) on right edge - pins 1-4 top to bottom: GND, 5V, SCL, SDA
    pcb += generate_1xN_header("J7", I2C_X, I2C_Y, 4, rotation=0, value="I2C", 
                                pin_labels=["GND", "5V", "SCL", "SDA"])
    
    # Board outline
    pcb += generate_board_outline()
    
    # Labels
    pcb += generate_labels()
    
    # Close the file
    pcb += "\n)\n"
    
    # Write the file
    with open("slave.kicad_pcb", "w") as f:
        f.write(pcb)
    
    print(f"Generated slave.kicad_pcb")
    print(f"Board size: {BOARD_WIDTH:.1f} x {BOARD_HEIGHT:.1f} mm")
    print(f"Components:")
    print(f"  - 6x MH-Tiny sockets (U1-U6)")
    print(f"  - 6x SPI headers (J1-J6): CE, CSN, SCK, MOSI, MISO")
    print(f"  - 1x I2C header (J7): SDA, SCL, 5V, GND")


if __name__ == "__main__":
    main()
