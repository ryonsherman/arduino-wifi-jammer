#!/usr/bin/env python3
"""
Add net assignments to existing master.kicad_pcb file.

Net assignments for Master+Power board:
- GND: LM2596.2, LM2596.4, Nano.29, J2.2, J3.2, J4.4
- 36V: LM2596.1, J2.1
- 5V:  LM2596.3, Nano.27, J3.1, J4.3, R1.1, R2.1
- SDA: Nano.4 (A4/pin 23 on socket), J4.1, R1.2
- SCL: Nano.5 (A5/pin 24 on socket), J4.2, R2.2

Arduino Nano pinout (as mounted in 2x15 socket):
Left row (pins 1-15):  D1, D0, RST, GND, D2, D3, D4, D5, D6, D7, D8, D9, D10, D11, D12
Right row (pins 16-30): D13, 3V3, AREF, A0, A1, A2, A3, A4(SDA), A5(SCL), A6, A7, 5V, RST, GND, VIN

Wait - need to verify Nano pinout. Standard Nano:
- Pin 4 on left = GND
- Pin 27 on right = 5V  
- Pin 29 on right = GND
- A4 = pin 23, A5 = pin 24

Actually checking the MH-Tiny/Nano pinout from README:
Left side (top to bottom): D2,D1,D0,RST,A5(25),A5/SCL(24),A4/SDA(23),A3(22),A2(21),A1(20),A0(19),A7(18),A6(17),D16,D15
Right side (top to bottom): VIN,GND,5V,D3,D4,D5,D6,D7,D8,D9,D10,D11,D12,D13,D14

So in 2x15 socket with pin 1 at top-left:
Left row 1-15:  D2,D1,D0,RST,25,A5/SCL,A4/SDA,A3,A2,A1,A0,A7,A6,16,15
Right row 16-30: VIN,GND,5V,D3,D4,D5,D6,D7,D8,D9,D10,D11,D12,D13,D14

So:
- GND = pin 17 (right row, 2nd from top)
- 5V = pin 18 (right row, 3rd from top)  
- A4/SDA = pin 7 (left row)
- A5/SCL = pin 6 (left row)
- VIN = pin 16 (right row, top)
"""

import re

# Net definitions
NETS = {
    1: "GND",
    2: "5V", 
    3: "36V",
    4: "SDA",
    5: "SCL",
}

# Pad to net mapping: (footprint_ref, pad_num) -> net_id
PAD_NETS = {
    # LM2596 (U2): 1=IN+(36V), 2=IN-(GND), 3=OUT+(5V), 4=OUT-(GND)
    ("U2", "1"): 3,  # 36V
    ("U2", "2"): 1,  # GND
    ("U2", "3"): 2,  # 5V
    ("U2", "4"): 1,  # GND
    
    # 36V input header (J1): 1=36V+, 2=GND
    ("J1", "1"): 3,  # 36V
    ("J1", "2"): 1,  # GND
    
    # 5V output header (J2): 1=5V+, 2=GND
    ("J2", "1"): 2,  # 5V
    ("J2", "2"): 1,  # GND
    
    # I2C header (J3): 1=SDA, 2=SCL, 3=5V, 4=GND
    ("J3", "1"): 4,  # SDA
    ("J3", "2"): 5,  # SCL
    ("J3", "3"): 2,  # 5V
    ("J3", "4"): 1,  # GND
    
    # Arduino Nano socket (U1):
    # Pin 6 = A5/SCL, Pin 7 = A4/SDA, Pin 17 = GND, Pin 18 = 5V
    ("U1", "6"): 5,   # SCL
    ("U1", "7"): 4,   # SDA
    ("U1", "17"): 1,  # GND
    ("U1", "18"): 2,  # 5V
    
    # Resistor R1 (SDA pullup): 1=5V, 2=SDA
    ("R1", "1"): 2,  # 5V
    ("R1", "2"): 4,  # SDA
    
    # Resistor R2 (SCL pullup): 1=5V, 2=SCL
    ("R2", "1"): 2,  # 5V
    ("R2", "2"): 5,  # SCL
    
    # Capacitor C1 (5V filter): 1=5V, 2=GND
    ("C1", "1"): 2,  # 5V
    ("C1", "2"): 1,  # GND
}

def main():
    with open("master.kicad_pcb", "r") as f:
        content = f.read()
    
    # Find current footprint being processed
    current_ref = None
    lines = content.split('\n')
    new_lines = []
    
    for i, line in enumerate(lines):
        # Track which footprint we're in by finding Reference property
        ref_match = re.search(r'\(property "Reference" "([^"]+)"', line)
        if ref_match:
            current_ref = ref_match.group(1)
        
        # Add net to pad if we have a mapping
        if '(pad "' in line and current_ref:
            pad_match = re.search(r'\(pad "(\d+)"', line)
            if pad_match:
                pad_num = pad_match.group(1)
                key = (current_ref, pad_num)
                if key in PAD_NETS:
                    net_id = PAD_NETS[key]
                    net_name = NETS[net_id]
                    # Check if net already assigned
                    if '(net ' not in line:
                        # Find the uuid line for this pad and add net before it
                        # For now, we'll add it in a second pass
                        pass
        
        new_lines.append(line)
    
    # Actually, let's do this more carefully - modify the pad blocks
    # Parse and rebuild with nets
    
    # First, ensure net declarations exist after setup section
    if '(net 0 "")' not in content:
        # Add net declarations after (setup ...) section
        setup_end = content.find(')\n\t(footprint')
        if setup_end == -1:
            setup_end = content.find(')\n(footprint')
        
        net_decls = '\n\t(net 0 "")\n'
        for net_id, net_name in NETS.items():
            net_decls += f'\t(net {net_id} "{net_name}")\n'
        
        content = content[:setup_end+1] + net_decls + content[setup_end+1:]
    
    # Now add net assignments to pads
    # Find each pad and add net if needed
    current_ref = None
    result = []
    lines = content.split('\n')
    
    i = 0
    while i < len(lines):
        line = lines[i]
        
        # Track footprint reference
        ref_match = re.search(r'\(property "Reference" "([^"]+)"', line)
        if ref_match:
            current_ref = ref_match.group(1)
        
        # Check for pad definition
        pad_match = re.search(r'(\s*)\(pad "(\d+)" thru_hole', line)
        if pad_match and current_ref:
            indent = pad_match.group(1)
            pad_num = pad_match.group(2)
            key = (current_ref, pad_num)
            
            if key in PAD_NETS and '(net ' not in line:
                net_id = PAD_NETS[key]
                net_name = NETS[net_id]
                
                # Collect the full pad block
                pad_lines = [line]
                i += 1
                paren_count = line.count('(') - line.count(')')
                
                while i < len(lines) and paren_count > 0:
                    pad_lines.append(lines[i])
                    paren_count += lines[i].count('(') - lines[i].count(')')
                    i += 1
                
                # Insert net assignment before the closing of the pad
                # Find the uuid line and insert net before it
                for j, pline in enumerate(pad_lines):
                    if '(uuid' in pline:
                        pad_lines.insert(j, f'{indent}\t(net {net_id} "{net_name}")')
                        break
                
                result.extend(pad_lines)
                continue
        
        result.append(line)
        i += 1
    
    # Write back
    with open("master.kicad_pcb", "w") as f:
        f.write('\n'.join(result))
    
    print("Added net assignments:")
    for (ref, pad), net_id in PAD_NETS.items():
        print(f"  {ref}.{pad} -> {NETS[net_id]}")

if __name__ == "__main__":
    main()
