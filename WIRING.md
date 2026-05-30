# Wi-Fi Jammer Swarm - Wiring Diagram (ASCII)

## System Overview

                      MASTER NODE
            (Arduino Nano + NRF24L01+)
            I2C Address: 0x70
            NRF24 Mode: RX

            USB ---> Serial Monitor (115200 baud)

            SDA ----+---------------------------+
            SCL ----+---------------------------+
            VCC ----+---------------------------+
            GND ----+---------------------------+
                    |                           |
                    |         I2C BUS           |
                    |    (SDA/SCL parallel)     |
                    |                           |
      +----------+    +----------+    +----------+
      |          |    |          |    |          |
  +---+----------+----+----------+----+----------+---+
  |  MH-TINY    |    |  MH-TINY  |    |  MH-TINY  |
  |  SLAVE #0   |    |  SLAVE #1 |    |  SLAVE #11|
  |  0x01       |    |  0x02     |    |  0x0C     |
  +-------------+    +-----------+    +-----------+


## Master Node (0x70)

+---------------------------------------------+
|              ARDUINO NANO                    |
|                                              |
|   +-------------------------------------+    |
|   |           NRF24L01+ MODULE           |    |
|   |                                      |    |
|   |   VCC  ----+------------------------+    |
|   |             | (3.3V critical!)         |    |
|   |   GND  ----+------------------------+    |
|   |   CE   ----+------- Pin 9               |    |
|   |   CSN  ----+------- Pin 10              |    |
|   |   SCK  ----+------- Pin 13 (SPI SCK)    |    |
|   |   MISO ----+------- Pin 12 (SPI MISO)   |    |
|   |   MOSI ----+------- Pin 11 (SPI MOSI)   |    |
|   |   IRQ  ----+------- (optional)          |    |
|   +-------------------------------------+    |
|                                              |
|   I2C Connections:                          |
|   SDA  ------------------------ Pin A4       |
|   SCL  ------------------------ Pin A5       |
|                                              |
|   USB Serial:                               |
|   TX   ------------------------ USB TX       |
|   RX   ------------------------ USB RX       |
|   GND  ------------------------ USB GND      |
+---------------------------------------------+


## Slave Nodes (0x01-0x0C)

The slaves use MH-Tiny ATtiny88 clones (TQFP-32) with ATTinyCore board package.
Pin numbers below are Arduino-logical (mapped by ATTinyCore).

+---------------------------------------------+
|            MH-TINY (ATTINY88)                |
|                                              |
|   +-------------------------------------+    |
|   |           NRF24L01+ MODULE           |    |
|   |                                      |    |
|   |   VCC  ----+------------------------+    |
|   |             | (3.3V critical!)         |    |
|   |   GND  ----+------------------------+    |
|   |   CE   ----+------- Pin 9  (PB1)        |    |
|   |   CSN  ----+------- Pin 10 (PB2)        |    |
|   |   SCK  ----+------- Pin 13 (PB5)        |    |
|   |   MISO ----+------- Pin 12 (PB4)        |    |
|   |   MOSI ----+------- Pin 11 (PB3)        |    |
|   |   IRQ  ----+------- (optional)          |    |
|   +-------------------------------------+    |
|                                              |
|   I2C Connections:                          |
|   SDA  ------------------------ Pin 23 (PC4) |
|   SCL  ------------------------ Pin 24 (PC5) |
|                                              |
|   Configuration:                            |
|   #define SLAVE_ID X (X = 0-11)             |
|   I2C Address = 0x01 + SLAVE_ID             |
+---------------------------------------------+


## I2C Bus Topology

                         MASTER (0x70)
                             |
       +---------------------+----------------------+
       |                     |                      |
    +--+--------+       +----+-------+        +-----+-------+
    | MH-TINY   |       |  MH-TINY   |        |  MH-TINY    |
    | SLAVE #0  |       |  SLAVE #1  |        |  SLAVE #11  |
    | 0x01      |       |  0x02      |        |  0x0C       |
    +-----------+       +------------+        +-------------+

    All nodes share the same SDA and SCL lines (parallel bus)


## Wiring Table

| Connection | Master (Nano) | Slaves (MH-Tiny) |
|------------|---------------|------------------|
| SDA        | A4            | Pin 23 (PC4)     |
| SCL        | A5            | Pin 24 (PC5)     |
| VCC        | 3.3V          | 3.3V             |
| GND        | GND           | GND              |
| CE         | Pin 9         | Pin 9  (PB1)     |
| CSN        | Pin 10        | Pin 10 (PB2)     |
| SCK        | Pin 13        | Pin 13 (PB5)     |
| MOSI       | Pin 11        | Pin 11 (PB3)     |
| MISO       | Pin 12        | Pin 12 (PB4)     |


## NRF24L01+ Module Pinout

+--------------------------------------------+
|          NRF24L01+ MODULE                  |
|                                            |
|   VCC   ----+                              |
|             | (3.3V DC)                    |
|   GND   ----+-------------------+          |
|   CE    ----+-------------------+ Pin 9    |
|   CSN   ----+-------------------+ Pin 10   |
|   SCK   ----+-------------------+ Pin 13   |
|   MOSI  ----+-------------------+ Pin 11   |
|   MISO  ----+-------------------+ Pin 12   |
|   IRQ   ----+ (optional, unused)           |
|                                            |
|   Antenna ---+-----------------------------+
|                                              |
|   Notes:                                     |
|   - VCC MUST be 3.3V (not 5V!)               |
|   - Add capacitor 10uF + 0.1uF near VCC/GND |
|   - Keep antenna away from digital lines    |
+--------------------------------------------+


## Power Distribution

            +---------------+
            |  Power Source |
            | (USB/Battery) |
            +-------+-------+
                    |
            +-------+-------+
            |               |
         +--+--+       +----+----+
         |Mstr |       | Slaves |
         |0x70|       | S0-S11  |
         +--+--+       +----+----+
            |               |
      +-----+-----+   +-----+-----+
      |   GND     |   |   GND     |
      +-----------+   +-----------+

    All nodes must share common ground


## Current Requirements

| Node Type            | Max Current | Typical Current |
|----------------------|-------------|-----------------|
| Arduino Nano (master)| 50mA        | 20mA            |
| MH-Tiny ATtiny88     | 10mA        | 5mA             |
| NRF24L01+ TX mode    | 11.5mA      | 10mA            |
| NRF24L01+ RX mode    | 14.5mA      | 12mA            |
| **Total per Slave**  | ~62mA       | ~32mA           |
| **Total System**     | ~806mA      | ~416mA          |
| (13 nodes combined)  |             |                 |

    Recommendation: 1A+ power supply for all 13 nodes


## Connection Checklist

Pre-Flight Wiring Check:

[ ] All nodes share common GND
[ ] NRF24L01+ VCC: 3.3V (direct to module) or 5V (if breakout has regulator)
[ ] SDA connected on all nodes (Nano A4, MH-Tiny Pin 23)
[ ] SCL connected on all nodes (Nano A5, MH-Tiny Pin 24)
[ ] CE pin (9) connected on all nodes
[ ] CSN pin (10) connected on all nodes
[ ] SPI pins (11, 12, 13) connected on all nodes
[ ] Antenna attached to all NRF24L01+ modules
[ ] Master connected to USB serial
[ ] Power source capable of 1A+


## I2C Address Verification

| Node Type  | SLAVE_ID | I2C Address |
|------------|----------|-------------|
| Master     | N/A      | 0x70        |
| Slave 0    | 0        | 0x01        |
| Slave 1    | 1        | 0x02        |
| ...        | ...      | ...         |
| Slave 11   | 11       | 0x0C        |


## Troubleshooting Wiring Issues

No ACK on I2C:
- Check common ground connection
- Verify SDA/SCL pull-up resistors (4.7k ohm recommended)
- Ensure VCC is stable (voltage drops affect I2C)

NRF24L01+ Not Found:
- Verify VCC is 3.3V (critical!)
- Check CE pin connection (Pin 9)
- Check CSN pin connection (Pin 10)
- Ensure SPI bus is intact (MOSI, MISO, SCK)

Slave Count < 12:
- Check unique SLAVE_ID (0-11) in each slave code
- Verify I2C address not duplicated
- Check for wiring shorts on SDA/SCL lines

Frequency Drift:
- Ensure stable 3.3V supply to NRF24L01+
- Add 10uF + 0.1uF capacitors near VCC/GND
- Minimize distance between power source and nodes

---

Version: 1.0
Last Updated: May 2026
Notes: Use shielded I2C cables for best results in noisy environments
