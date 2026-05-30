# Wi-Fi Jammer Swarm - USAGE Guide

Command Reference for Master Controller

This guide provides quick reference for all master controller commands. Open USB Serial Monitor at 115200 baud to access the command interface.

## COMMAND SUMMARY

| Command | Description |
|---------|-------------|
| help | Show all available commands |
| get [ids] | Query slave configurations |
| set \<distribution\> | Set custom channel distribution |
| channel \<n\> | Set single channel (1-13) or full spectrum (0) |
| start | Begin transmitting |
| stop | Halt transmission (keep config) |
| status | Show slave distribution and frequencies |

## COMMAND DETAILS

### 1. help

Display all available commands and usage examples.

Usage: `help`

Output:
  Command List:
  - `help`: Show command list
  - `get [ids]`: Query slave configs (e.g., `get 0,1,2` or `get all`)
  - `set <dist>`: Set custom distribution (e.g., `set 4@1,2@6,2@11`)
  - `channel <n>`: Set single channel (1-13) or full spectrum (0)
  - `start`: Begin transmitting
  - `stop`: Halt transmission
  - `status`: Show slave distribution and frequency map


### 2. get [ids]

Query configuration of specific slaves or all slaves.

Usage: `get [ids]`
- ids: Comma-separated slave IDs (0-11) or "all"
- If omitted, shows all slaves

Examples:
  `get`              -> Show all 12 slaves
  `get 0,1,2`        -> Show slaves 0, 1, 2
  `get 3,5,7,9`      -> Show slaves 3, 5, 7, 9
  `get all`          -> Show all slaves (explicit)

Output Format:
```
=== Slave Status ===
Active: 12/12
Slave 1 [ACTIVE] Mode: Custom
Slave 2 [ACTIVE] Mode: Custom
...
Slave 12 [ACTIVE] Mode: Custom
```


### 3. set \<distribution\>

Configure custom channel distribution pattern.

Usage: `set <distribution>`
- Format: `n@channel1,n@channel2,...`
- n = number of slaves to assign to channel
- channel = Wi-Fi channel (1-13) or 0 for full spectrum
- Unspecified slaves become idle

Examples:
  `set 4@1,2@6,2@11,4@0`
     -> 4 slaves on ch1, 2 on ch6, 2 on ch11, 4 in full spectrum mode
     -> All 12 slaves active

  `set 6@1,6@6`
     -> 6 slaves on ch1, 6 on ch6
     -> 8 slaves become idle

  `set 12@6`
     -> All 12 slaves on channel 6

  `set 4@1,2@6`
     -> 4 slaves on ch1, 2 on ch6
     -> 6 slaves become idle

  `set 3@1,3@6,3@11,3@0`
     -> Balanced distribution across 3 channels + full spectrum

Distribution Syntax Rules:
- Use comma to separate groups: `4@1,2@6`
- Channel range: 1-13 or 0 (full spectrum)
- Total slaves must not exceed 12
- Slaves not assigned become idle (not transmitting)


### 4. channel \<n\>

Set single channel mode or full spectrum mode.

Usage: `channel <n>`
- n = channel number (1-13) or 0 for full spectrum

Examples:
  `channel 6`        -> All slaves on channel 6 (2420 MHz)

  `channel 1`        -> All slaves on channel 1 (2412 MHz)

  `channel 13`       -> All slaves on channel 13 (2472 MHz)
  
  `channel 0`        -> Full spectrum (slaves spread across 60MHz span)

Frequency Mapping:
- Channel 1:  2412 MHz
- Channel 6:  2427 MHz
- Channel 11: 2462 MHz
- Channel 13: 2472 MHz
- Full Spectrum (0): 2415-2470 MHz (5MHz spacing)


### 5. start

Begin transmission with current configuration.

Usage: `start`

Behavior:
- Activates all active slaves
- Sends mode, channel, and command via I2C
- Slaves begin continuous noise transmission
- Master enters command loop

Notes:
- Configuration persists after `stop`
- Use `stop` before `start` to reapply new settings
- All active slaves transmit simultaneously


### 6. stop

Halt transmission but keep configuration.

Usage: `stop`

Behavior:
- Sends `stop` command to all slaves via I2C
- Slaves enter idle state
- Configuration (mode, channels) retained
- Fast restart with `start` command

Notes:
- Does NOT reset configuration
- Use `set` or `channel` to change settings
- Physical restart (power cycle) clears config


### 7. status

Show current slave distribution and frequency map.

Usage: `status`

Output Sections:

```
=== Slave Status ===
  Active: X/12 (active slaves count)
  Channel X: Y (count per channel in custom mode)
  Channel: N (single channel mode)
  Channel: All (full spectrum mode)

=== Channel Distribution ===
  Mode: Single Channel / Full Spectrum / Custom Distribution
  Slave N [ACTIVE] Channel: X (freq MHz)
    or
  Slave N [IDLE] (inactive slave)
    or
  Slave N -> Channel X (freq MHz)
```

Example Output:

```
=== Slave Status ===
Active: 12/12
Channel: All

=== Channel Distribution ===
Mode: Full Spectrum
Slave 1 -> Freq 2415 (2415 MHz)
Slave 2 -> Freq 2420 (2420 MHz)
...
```


## MODES OVERVIEW

### SINGLE CHANNEL MODE

All active slaves transmit on the same channel frequency.
Best for: Maximum power density on one channel

Usage: `channel <1-13>`
Example: `channel 6` -> All slaves on 2427 MHz

### FULL SPECTRUM MODE

Slaves spread across 60MHz span (2415-2470 MHz).
Best for: Wide coverage across multiple channels

Usage: `channel 0`
Frequency distribution:
  Slave 1: 2415 MHz
  Slave 2: 2420 MHz
  Slave 3: 2425 MHz
  ...
  Slave 12: 2470 MHz

### CUSTOM DISTRIBUTION MODE

Flexible assignment of slaves to specific channels.
Best for: Targeted jamming of specific channels

Usage: `set <distribution>`
Example: `set 4@1,2@6,2@11,4@0`
  -> 4 slaves on ch1, 2 on ch6, 2 on ch11, 4 in full spectrum


## USAGE EXAMPLES

### Example 1: Single Channel Jamming
1. Connect to master via USB serial (115200 baud)
2. Type: `channel 6`
3. Type: `status`
4. Type: `start`
5. All 12 slaves transmit on channel 6

### Example 2: Full Spectrum Coverage
1. Type: `channel 0`
2. Type: `status`
3. Type: `start`
4. Slaves spread across 2415-2470 MHz

### Example 3: Targeted Multi-Channel
1. Type: `set 4@1,4@6,4@11`
2. Type: `status`
3. Type: `start`
4. 4 slaves each on channels 1, 6, and 11

### Example 4: Stop and Restart
1. Type: `stop` (slaves idle, config retained)
2. Type: `channel 11` (change to channel 11)
3. Type: `status`
4. Type: `start` (restart on new channel)

### Example 5: Reset to Full Spectrum
1. Type: `stop`
2. Type: `set 12@0`
3. Type: `start`
4. All 12 slaves in full spectrum mode

### Example 6: Partial Deployment
1. Type: `set 6@1,6@6`
2. Type: `status`
3. Type: `start`
4. 6 slaves on ch1, 6 on ch6, 6 idle


## FREQUENCY TABLES

### Wi-Fi Channel Frequencies (Single Channel Mode)
Channel | Frequency (MHz) | Notes
--------|-----------------|------------------
1       | 2412            | Common in US/EU
6       | 2427            | Non-overlapping with 1,11
11      | 2442            | Non-overlapping with 1,6
13      | 2462            | EU only (US limit 11)

### Full Spectrum Mode Frequencies
Slave   | Frequency (MHz) | Channel Approx
--------|-----------------|------------------
1       | 2415            | Channel 1-2
2       | 2420            | Channel 2-3
3       | 2425            | Channel 3-4
4       | 2430            | Channel 4-5
5       | 2435            | Channel 5-6
6       | 2440            | Channel 6-7
7       | 2445            | Channel 7-8
8       | 2450            | Channel 8-9
9       | 2455            | Channel 9-10
10      | 2460            | Channel 10-11
11      | 2465            | Channel 11-12
12      | 2470            | Channel 12-13


## COMMAND SEQUENCE PATTERNS

### Pattern 1: Quick Setup
```
channel 6
start
status
```

### Pattern 2: Mode Switch
```
stop
channel 0
start
```

### Pattern 3: Custom Distribution
```
set 4@1,4@6,4@11
status
start
```

### Pattern 4: Pause and Resume
```
stop
(status check)
start
```

### Pattern 5: Full Reset

```
stop
set 12@0
channel 0
(start)
```

## TROUBLESHOOTING

Issue: "Slave Not Found"
Solution:
- Check I2C wiring (`SDA=A4`, `SCL=A5`)
- Verify slave address (`0x01`-`0x0C`)
- Ensure common ground
- Check NRF24L01+ VCC (3.3V critical)

Issue: "Active: X/12" (fewer than 12)
Solution:
- Verify all slaves powered
- Check I2C connections on all nodes
- Verify `SLAVE_ID` set correctly (0-11)
- Ensure no duplicate addresses

Issue: Frequency Drift
Solution:
- Check stable 3.3V supply
- Add 10uF + 0.1uF capacitors near VCC/GND
- Minimize power supply distance

Issue: Config Not Persisting
Solution:
- Use `stop` before power cycle
- Configuration only resets on physical restart
- Check `current_mode` variable not overwritten

Issue: Slaves not transmitting
Solution:
- Verify `start` command issued
- Check mode and channel set correctly
- Verify NRF24L01+ CE pin (Pin 9) connected
- Check antenna attached


## QUICK REFERENCE CARD

Command         | Syntax                     | Effect
----------------|----------------------------|------------------
help            | help                       | Show commands
get             | get or get 0,1,2           | Query slaves
set             | set 4@1,2@6,2@11,4@0       | Custom dist
channel         | channel 0-13               | Set mode
start           | start                      | Begin TX
stop            | stop                       | Halt TX
status          | status                     | Show distribution


### Mode Values
0 = Full Spectrum (spread 60MHz)
1 = Single Channel (all on one freq)
3 = Custom Distribution (flexible)


## NOTES

- All commands case-insensitive
- Configuration persists after `stop`
- Physical restart required to clear config
- Slaves numbered 1-12 in output (internal: 0-11)
- Max frequency: 2527 MHz (NRF24L01+ limit)
- Min frequency: 2400 MHz (NRF24L01+ limit)
- Power requirement: 1A+ supply for 13 nodes


## VERSION

Version: 1.0
Last Updated: May 2026
Author: Distributed System Team
