#!/usr/bin/env python3
"""
Add net assignments to Slave Carrier PCB pads for routing.
"""

import re
import uuid

def gen_uuid():
    return str(uuid.uuid4())

# Net definitions
NETS = {
    "GND": 1,
    "5V": 2,
    "SDA": 3,
    "SCL": 4,
    # SPI nets per MH-Tiny
    "U1_CE": 10, "U1_CSN": 11, "U1_MOSI": 12, "U1_MISO": 13, "U1_SCK": 14,
    "U2_CE": 20, "U2_CSN": 21, "U2_MOSI": 22, "U2_MISO": 23, "U2_SCK": 24,
    "U3_CE": 30, "U3_CSN": 31, "U3_MOSI": 32, "U3_MISO": 33, "U3_SCK": 34,
    "U4_CE": 40, "U4_CSN": 41, "U4_MOSI": 42, "U4_MISO": 43, "U4_SCK": 44,
    "U5_CE": 50, "U5_CSN": 51, "U5_MOSI": 52, "U5_MISO": 53, "U5_SCK": 54,
    "U6_CE": 60, "U6_CSN": 61, "U6_MOSI": 62, "U6_MISO": 63, "U6_SCK": 64,
}

# Pad to net assignments
PAD_NETS = {}

# For each MH-Tiny (U1-U6):
for i in range(1, 7):
    ref = f"U{i}"
    # Common nets
    PAD_NETS[(ref, "6")] = "SCL"      # A5/SCL - left col pin 6
    PAD_NETS[(ref, "7")] = "SDA"      # A4/SDA - left col pin 7
    PAD_NETS[(ref, "28")] = "5V"      # 5V - right col
    PAD_NETS[(ref, "29")] = "GND"     # GND - right col
    
    # SPI nets (right column)
    PAD_NETS[(ref, "17")] = f"U{i}_SCK"   # D13/SCK
    PAD_NETS[(ref, "18")] = f"U{i}_MISO"  # D12/MISO
    PAD_NETS[(ref, "19")] = f"U{i}_MOSI"  # D11/MOSI
    PAD_NETS[(ref, "20")] = f"U{i}_CSN"   # D10/CSN
    PAD_NETS[(ref, "21")] = f"U{i}_CE"    # D9/CE

# SPI headers (J1-J6) - pins 1-5: CE, CSN, MOSI, MISO, SCK
for i in range(1, 7):
    ref = f"J{i}"
    PAD_NETS[(ref, "1")] = f"U{i}_CE"
    PAD_NETS[(ref, "2")] = f"U{i}_CSN"
    PAD_NETS[(ref, "3")] = f"U{i}_MOSI"
    PAD_NETS[(ref, "4")] = f"U{i}_MISO"
    PAD_NETS[(ref, "5")] = f"U{i}_SCK"

# I2C header (J7) - pins 1-4 top to bottom: GND, 5V, SCL, SDA (bottom to top: SDA, SCL, 5V, GND)
PAD_NETS[("J7", "1")] = "GND"
PAD_NETS[("J7", "2")] = "5V"
PAD_NETS[("J7", "3")] = "SCL"
PAD_NETS[("J7", "4")] = "SDA"


def add_nets_to_pcb(filename):
    with open(filename, 'r') as f:
        content = f.read()
    
    # Add net definitions after setup section
    net_defs = "\n"
    for net_name, net_id in sorted(NETS.items(), key=lambda x: x[1]):
        net_defs += f'\t(net {net_id} "{net_name}")\n'
    
    # Find the first footprint and insert nets before it
    footprint_match = re.search(r'\n(\t\(footprint)', content)
    if footprint_match:
        insert_pos = footprint_match.start()
        content = content[:insert_pos] + net_defs + content[insert_pos:]
    
    # Process footprints to add nets to pads
    # We need to track which footprint we're in by finding Reference property
    
    result = []
    current_ref = None
    lines = content.split('\n')
    i = 0
    
    while i < len(lines):
        line = lines[i]
        
        # Track current footprint by Reference
        if '(property "Reference"' in line:
            ref_match = re.search(r'"Reference" "([^"]+)"', line)
            if ref_match:
                current_ref = ref_match.group(1)
        
        # Reset ref when leaving footprint
        if line.strip() == ')' and current_ref and i > 0:
            # Check if this closes a footprint (previous non-empty line would be a pad or property)
            pass
        
        # Check if this is a pad line that needs a net
        if current_ref and '\t\t(pad "' in line:
            pad_match = re.search(r'\(pad "(\d+)"', line)
            if pad_match:
                pad_num = pad_match.group(1)
                net_key = (current_ref, pad_num)
                if net_key in PAD_NETS:
                    net_name = PAD_NETS[net_key]
                    net_id = NETS[net_name]
                    
                    # Collect the full pad block
                    pad_lines = [line]
                    paren_count = line.count('(') - line.count(')')
                    i += 1
                    while i < len(lines) and paren_count > 0:
                        pad_lines.append(lines[i])
                        paren_count += lines[i].count('(') - lines[i].count(')')
                        i += 1
                    i -= 1  # Back up one since the loop will increment
                    
                    # Insert net before the closing paren
                    # Find the line with just "\t\t)" or similar
                    for j in range(len(pad_lines) - 1, -1, -1):
                        if pad_lines[j].strip() == ')':
                            pad_lines.insert(j, f'\t\t\t(net {net_id} "{net_name}")')
                            break
                    
                    result.extend(pad_lines)
                    i += 1
                    continue
        
        result.append(line)
        i += 1
    
    content = '\n'.join(result)
    
    with open(filename, 'w') as f:
        f.write(content)
    
    # Count actual net assignments in output
    net_count = len(re.findall(r'\(net \d+ "[^"]+"\)', content))
    print(f"Added {len(NETS)} net definitions")
    print(f"Assigned nets to {net_count - len(NETS)} pads")


if __name__ == "__main__":
    add_nets_to_pcb("slave.kicad_pcb")
