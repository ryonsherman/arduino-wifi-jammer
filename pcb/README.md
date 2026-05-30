# WiFi Jammer PCB Design

## Box Layout (top view, looking down into open-top box)

```
┌─────────────────────────────────────────────┐
│                                             │
│  ┌─────────────────────────────────────┐    │
│  │     NRF ADAPTERS (6x on perf/bread) │    │  FRONT
│  │     ○ ○ ○ ○ ○ ○  ← antennas         │    │  (antennas
│  ├─────────────────────────────────────┤    │   through
│  │     NRF ADAPTERS (6x on perf/bread) │    │   wall)
│  │     ○ ○ ○ ○ ○ ○  ← antennas         │    │
│  └─────────────────────────────────────┘    │
│                                             │
│ ┌───────┐                         ┌───────┐ │
│ │ SLAVE │                         │ SLAVE │ │
│ │CARRIER│                         │CARRIER│ │
│ │  6x   │      (open space)       │  6x   │ │
│ │  MH-  │     wire wrap           │  MH-  │ │
│ │ Tiny  │                         │ Tiny  │ │
│ └───────┘                         └───────┘ │
│  LEFT                               RIGHT   │
│                                             │
│  ┌───────┐ ┌───────────┐ ┌───────┐         │
│  │ 9V    │ │  MASTER   │ │ 9V    │         │  BACK
│  │ BATT  │ │ + STEPDOWN│ │ BATT  │         │
│  └───────┘ └───────────┘ └───────┘         │
│                                             │
└─────────────────────────────────────────────┘
```

## Boards

| Board | Qty | Orientation | Description |
|-------|-----|-------------|-------------|
| Slave Carrier | 2 | Vertical, side walls | 6x MH-Tiny sockets, 6x SPI headers, I2C bus |
| Master + Power | 1 | Horizontal, back wall | Arduino Nano socket, 9V step-down, power distribution |

## NRF Modules

Using **NRF24L01+ socket adapter boards** (not custom PCB):
- Adapters have onboard 3.3V LDO, LED, decoupling caps
- Accept 5V input
- Mount on perfboard or breadboard at front wall
- Connect to Slave Carriers via wire wrap

## Slave Carrier Layout

```
┌─────────────────────────────────────────────────────────────┐
│  SLAVE CARRIER BOARD                                        │
│                                                             │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐
│  │ MH-Tiny │ │ MH-Tiny │ │ MH-Tiny │ │ MH-Tiny │ │ MH-Tiny │ │ MH-Tiny │
│  │   #1    │ │   #2    │ │   #3    │ │   #4    │ │   #5    │ │   #6    │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘
│       │           │           │           │           │           │
│  ┌────┴────┐ ┌────┴────┐ ┌────┴────┐ ┌────┴────┐ ┌────┴────┐ ┌────┴────┐
│  │SPI HDR 1│ │SPI HDR 2│ │SPI HDR 3│ │SPI HDR 4│ │SPI HDR 5│ │SPI HDR 6│
│  │ ▪ ▪ ▪ ▪ ▪│ │ ▪ ▪ ▪ ▪ ▪│ │ ▪ ▪ ▪ ▪ ▪│ │ ▪ ▪ ▪ ▪ ▪│ │ ▪ ▪ ▪ ▪ ▪│ │ ▪ ▪ ▪ ▪ ▪│
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘
│                                                             │
│  [5V RAIL]  ← wire wrap daisy chain to NRF adapters         │
│  [GND RAIL] ← wire wrap daisy chain to NRF adapters         │
│                                                             │
│  [I2C HDR]  ← SDA, SCL, 5V, GND to Master                   │
└─────────────────────────────────────────────────────────────┘
```

### MH-Tiny Pinout (standard mounting, USB facing down)

```
LEFT                          RIGHT
┌────┐                        ┌────┐
│  2 │                        │VIN │
│  1 │                        │GND │ ← GND
│  0 │                        │ 5V │ ← 5V
│RST │                        │  3 │
│ 25 │                        │  4 │
│A5/24│ ← SCL (I2C)           │  5 │
│A4/23│ ← SDA (I2C)           │  6 │
│A3/22│                       │  7 │
│A2/21│                       │  8 │
│A1/20│                       │  9 │ ← CE
│A0/19│                       │ 10 │ ← CSN
│A7/18│                       │ 11 │ ← MOSI
│A6/17│                       │ 12 │ ← MISO
│ 16 │                        │ 13 │ ← SCK
│ 15 │                        │ 14 │
└────┘                        └────┘
            ┌─────┐
            │ USB │
            └─────┘
```

### SPI Header Pinout (5 pins per MH-Tiny)

All SPI signals route from right edge (pins 9-13):

| SPI Header | MH-Tiny Pin | Signal |
|------------|-------------|--------|
| 1 | 9 | CE |
| 2 | 10 | CSN |
| 3 | 13 | SCK |
| 4 | 11 | MOSI |
| 5 | 12 | MISO |

## Wiring (Wire Wrap)

### Per MH-Tiny → NRF Pair (5 wires)
```
MH-Tiny SPI Header    NRF Adapter
       CE  ─────────→ CE
       CSN ─────────→ CSN
       SCK ─────────→ SCK
       MOSI─────────→ MO
       MISO─────────→ MI
```

### Power (Daisy Chain)
```
Slave Carrier 5V Rail
    │
    └─→ NRF1 VCC ─→ NRF2 VCC ─→ NRF3 VCC ─→ NRF4 VCC ─→ NRF5 VCC ─→ NRF6 VCC

Slave Carrier GND Rail
    │
    └─→ NRF1 GND ─→ NRF2 GND ─→ NRF3 GND ─→ NRF4 GND ─→ NRF5 GND ─→ NRF6 GND
```

### Wire Count Per Slave Carrier
| Signal | Wires | Method |
|--------|-------|--------|
| 5V | 1 | Daisy chain to 6 NRFs |
| GND | 1 | Daisy chain to 6 NRFs |
| SPI+CE+CSN | 30 | 5 wires × 6 pairs |
| **Total** | **32** | |

## Component Notes

### Slave Carrier (per board)
- 6x 2x15 female headers (Nano footprint for MH-Tiny)
- 6x 1x5 male pin headers (SPI breakout for wire wrap)
- 5V and GND rails with wire wrap posts
- I2C header (SDA, SCL, 5V, GND)
- I2C pullup resistors (4.7kΩ) — only on one board or Master

### Master + Power
- 1x 2x15 female headers (Nano footprint for Arduino Nano)
- LM2596 buck converter module socket (21mm × 43mm, pins at corners)
- 9V battery connectors (2x, parallel config)
- 5V/GND distribution headers to Slave Carriers
- I2C header (SDA, SCL)
- I2C pullups (4.7kΩ)

#### LM2596 Buck Converter Module
```
┌─────────────────────────────────────────┐
│                                         │
│  [POT]   [INDUCTOR]   [CAP]            │
│                                         │
│  [IC]                 [DIODE]          │
│                                         │
│         21mm × 43mm                     │
▪ IN+                                 OUT+ ▪
│              ← 17mm →                   │
▪ IN-                                 OUT- ▪
└─────────────────────────────────────────┘
  ↑───── 8mm ─────↑
```
- Input: 36V from batteries (4× 9V in series)
- Output: 5V (adjustable via pot)
- Max input: 40V (36V is within spec)
- Max current: 3A (sufficient for system)

#### Battery Configuration
```
Pack 1: [9V]─[9V] = 18V
                         ↘
                          ─→ Series = 36V → LM2596 → 5V
                         ↗
Pack 2: [9V]─[9V] = 18V
```
- 4× USB-rechargeable 9V Li-ion batteries
- 2 packs × 2 batteries each, all in series
- Single 2-pin header for 36V input

### NRF Adapters (existing, not custom PCB)
- Socket adapter boards with onboard:
  - 3.3V LDO voltage regulator
  - LED power indicator
  - Decoupling capacitors
- Accept 5V input, 8-pin header output
- Mount on perfboard/breadboard, wire wrap to Slave Carriers

## Open Questions

None — ready for PCB design.

## Power Budget

### Current Draw
| Component | Qty | Avg | Peak |
|-----------|-----|-----|------|
| MH-Tiny + NRF pair | 12 | 30mA | 130mA |
| Arduino Nano (Master) | 1 | 50mA | 50mA |
| **Total** | | **~410mA** | **~1.6A** |

### Power Supply
- 4× USB-rechargeable 9V Li-ion batteries (all in series = 36V)
- Capacity: ~500mAh (series config)
- LM2596 buck converter: 40V max input, 3A output (36V within spec)

### Runtime Estimate
~500mAh ÷ ~500mA avg = **~1 hour**
