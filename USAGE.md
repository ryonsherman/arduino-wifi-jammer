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
| status | Show slave distribution, frequencies, health |
| snapshot [N] | RF spectrum snapshot (default 5s, max 60s) |
| scan \<ch\> [N] | Live waterfall scan of a single channel |
| scan | Threshold-based scan of all 13 channels |
| scan threshold \<n\> | Set activity threshold (0-100%) |
| power \<1-4\> | Set NRF output power level |
| adaptive | One-shot: scan & target busiest channels |
| adaptive start | Start periodic adaptive jamming |
| adaptive stop | Stop adaptive jamming |
| adaptive thresh \<n\> | Set adaptive min activity % (0=auto) |
| adaptive intv \<n\> | Set rescan interval (5-300s) |
| sweep | Show sweep status |
| sweep start | Start sweep mode (cycles ch 1-13) |
| sweep stop | Stop sweep mode |
| sweep \<ms\> | Set dwell per channel (10-5000ms) |
| pattern | Show current pattern |
| pattern continuous | Continuous jamming (default) |
| pattern pulsed \<ms\> | Alternating on/off (5-5000ms) |
| pattern random | Random NRF frequency hop per packet |
| pattern burst \<on\> \<off\> | Custom on/off timing |
| profile save \<name\> | Save current config as profile |
| profile load \<name\> | Load a saved profile |
| profile list | List saved profiles |
| profile delete \<name\> | Delete a profile |

## COMMAND DETAILS

### 1. help

Display all available commands and usage examples.

Usage: `help`

Output:
  ============== Commands ==============
  help        - Show commands
  get         - Get slave config (get all, get 0,1,2)
  set         - Custom dist (set 4@1,2@6,2@11)
  channel     - Set channel (1-13) or 0=spectrum
  start       - Begin jamming
  stop        - Stop jamming
  status      - Show status & freq map
  snapshot    - RF snapshot 5s (default)
  snapshot 30 - RF snapshot 30s collection
  scan             - Scan all ch, show above threshold
  scan threshold N - Set scan threshold %
  scan 6           - Live scan ch 6, 10s (default)
  scan 6 30        - Live scan ch 6 for 30s
  power       - Show current power
  adaptive          - One-shot: scan & target busiest channels
  adaptive start    - Periodic adaptive jamming
  adaptive stop     - Stop adaptive jamming
  adaptive thresh N - Min activity % (0=auto)
  adaptive intv N   - Rescan interval (sec)
  profile list      - List saved profiles
  profile save <n>  - Save current config as profile
  profile load <n>  - Load a saved profile
  profile delete <n>- Delete a profile
  sweep             - Show sweep status
  sweep start       - Start sweep mode
  sweep stop        - Stop sweep mode
  sweep 500         - Set dwell (10-5000ms)
  pattern           - Show/set jamming pattern
  pattern continuous - Continuous (default)
  pattern pulsed 50 - Alternating on/off
  pattern random    - Random freq hop
  pattern burst 100 20 - Custom on/off

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
=== Status ===
Active: 12/12
Channel: All (spectrum)
Pattern: continuous

-- Slaves --
  #1 0x01 CH 14  [OK] 42us
  #2 0x02 CH 19  [OK] 38us
  ...
```

### 3. set \<distribution\>

Configure custom channel distribution pattern.

Usage: `set <distribution>`
- Format: `n@channel1,n@channel2,...`
- n = number of slaves to assign to channel
- channel = Wi-Fi channel (1-13)
- Unspecified slaves become idle

Examples:
  `set 4@1,2@6,2@11,4@6`
     -> 4 slaves on ch1, 2 on ch6, 2 on ch11, 4 on ch6
     -> All 12 slaves active

  `set 6@1,6@6`
     -> 6 slaves on ch1, 6 on ch6
     -> No idle slaves

  `set 12@6`
     -> All 12 slaves on channel 6

  `set 4@1,2@6`
     -> 4 slaves on ch1, 2 on ch6
     -> 6 slaves become idle

  `set 3@1,3@6,3@11,3@1`
     -> Balanced distribution across 2 channels

Distribution Syntax Rules:
- Use comma to separate groups: `4@1,2@6`
- Channel range: 1-13
- Total slaves must not exceed 12
- Slaves not assigned become idle (not transmitting)

### 4. channel \<n\>

Set single channel mode or full spectrum mode.

Usage: `channel <n>`
- n = channel number (1-13) or 0 for full spectrum

Examples:
  `channel 6`        -> All slaves spread across channel 6 (2426-2448 MHz)

  `channel 1`        -> All slaves spread across channel 1 (2401-2423 MHz)

  `channel 13`       -> All slaves spread across channel 13 (2461-2483 MHz)

  `channel 0`        -> Full spectrum (slaves spread across 60MHz span)

Frequency Mapping:
- Single Channel Mode uses **fan-out**: slaves spread across 22MHz channel width
  - Formula: `center_freq + (local_idx * 2) - (group_size - 1)`
  - With 12 slaves (group_size=12), offsets range from -11 to +11 MHz
  - Example: Channel 6 (center 2437 MHz) → Slaves cover 2426-2448 MHz
- Channel 1 center:  2412 MHz → fan-out 2401-2423 MHz
- Channel 6 center:  2437 MHz → fan-out 2426-2448 MHz
- Channel 11 center: 2462 MHz → fan-out 2451-2473 MHz
- Channel 13 center: 2472 MHz → fan-out 2461-2483 MHz
- Full Spectrum (0): 2415-2470 MHz (5MHz spacing, no fan-out)

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
- Hardware switch position 1 overrides software start

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

Show current slave distribution, frequency map, and per-slave health.

Usage: `status`

Output Sections:

```
=== Status ===
Active: 12/12
Channel: All (spectrum)
Pattern: continuous

-- Slaves --
  #1 0x01 CH 14  [OK] 42us
  #2 0x02 CH 19  [OK] 38us
  ...
12/12 online

=== Frequency Map ===
Mode: Full Spectrum
S0: 2415 MHz
S1: 2420 MHz
...
```

Health columns: slave number, I2C address, current channel, online/offline, response time in microseconds.

Example Output:

```
=== Status ===
Active: 12/12
Channel: All (spectrum)
Pattern: continuous

-- Slaves --
  #1 0x01 CH 14  [OK] 42us
  #2 0x02 CH 19  [OK] 38us
  #3 0x03 CH 24  [OK] 41us
  #4 0x04 CH 29  [OK] 43us
  #5 0x05 CH 34  [OK] 37us
  #6 0x06 CH 39  [OK] 39us
  #7 0x07 CH 44  [OK] 42us
  #8 0x08 CH 49  [OK] 36us
  #9 0x09 CH 54  [OK] 40us
  #10 0x0A CH 59  [OK] 38us
  #11 0x0B CH 64  [OK] 41us
  #12 0x0C CH 69  [OK] 43us
12/12 online
```

### 8. snapshot [N]

RF spectrum snapshot — collects carrier detect data across all 13 Wi-Fi channels for a specified duration, then displays an aggregate bar graph per channel.

Usage: `snapshot [N]`
- N: Collection time in seconds (1-60, default 5)

Example output:
```
======= RF Snapshot =======
Collecting data...
done
---------------------------
Ch  Freq        Signal
 1  2412 MHz  ##........  16%
 2  2417 MHz  #.........   8%
 3  2427 MHz  ###.......  22%
 4  2432 MHz  ####......  35%
 5  2437 MHz  #####.....  48%
 6  2442 MHz  ########..  71%
 7  2447 MHz  ########..  73%
 8  2452 MHz  #####.....  46%
 9  2457 MHz  ####......  38%
10  2462 MHz  ###.......  24%
11  2467 MHz  ##........  18%
12  2472 MHz  #.........   5%
13  2477 MHz  #.........   4%
===========================
```

The bar graph uses 10 characters (`#` = signal detected, `.` = no signal) scaled to the percentage of scan passes where the NRF RPD triggered on that channel. RPD triggers at approximately -64 dBm or stronger.

### 9. scan \<ch\> [N]

Live waterfall scan of a single Wi-Fi channel. Shows signal intensity over time with a per-second bar graph.

Usage: `scan <ch> [N]`
- ch: Wi-Fi channel (1-13)
- N: Duration in seconds (1-60, default 10)

Example output:
```
======== RF Live Scan ========
Channel 6 (2442 MHz) for 10s
------------------------------
....................   0%  1s
....................   0%  2s
####................  20%  3s
##########..........  50%  4s
##################..  90%  5s
###################.  95%  6s
###################.  95%  7s
##################..  85%  8s
########............  40%  9s
####................  20% 10s
==============================
```

Each bar is 20 characters (5% per `#`).

### 10. scan

Threshold-based scan of all 13 channels. Reports only channels with activity above the threshold.

Usage: `scan`

Example output:
```
Scanning all channels...
Channels above threshold:
 6  2442 MHz  ########..  71%
 7  2447 MHz  ########..  73%
Threshold: 50% (scan threshold <n> to change)
```

### 11. scan threshold \<n\>

Set or view the activity threshold for the `scan` command.

Usage: `scan threshold <n>`
- n: 0-100 (percentage). Default: 50

Examples:
  `scan threshold 30`   -> Set threshold to 30%
  `scan threshold`       -> Show current threshold

### 12. power \<1-4\>

Set the NRF24L01+ RF output power level. Sent to all slaves via I2C.

Usage: `power <1-4>`
- 1 = MIN (-18 dBm)
- 2 = LOW (-12 dBm)
- 3 = HIGH (-6 dBm)
- 4 = MAX (0 dBm, default)

Examples:
  `power`        -> Show current power
  `power 1`      -> Minimum power (shortest range)
  `power 4`      -> Maximum power (longest range)

### 13. adaptive

Adaptive jamming mode. Stops all jamming, scans all 13 Wi-Fi channels for 2 seconds, then assigns slaves to the busiest channels.

Usage: `adaptive`
- One-shot: scan, assign, jam, done

Usage: `adaptive start`
- Periodic mode: rescans and reassigns every N seconds (default 30s)

Usage: `adaptive stop`
- Stop periodic adaptive mode

Usage: `adaptive thresh <n>`
- Set minimum activity threshold (0-100%, default 0 = auto pick top 12)
- Channels below this threshold are skipped
- If no channels are above threshold, falls back to full spectrum

Usage: `adaptive intv <n>`
- Set rescan interval in seconds (5-300, default 30)

Example output:
```
Adaptive scanning...
..done
Targeting 3 busiest channels
Channels: 6 (71%), 7 (73%), 5 (48%)
```

Notes:
- Adaptive pauses all jamming before scanning (prevents self-interference)
- Cancelled by any mode-changing command (channel, set, start, stop, sweep)
- Fallback to full spectrum if no channels exceed threshold

### 14. sweep

Sweep mode — master cycles all slaves through Wi-Fi channels 1-13 at a configurable dwell time.

Usage: `sweep`
- Show current sweep status (active/inactive, current channel, dwell)

Usage: `sweep start`
- Start sweep mode from channel 1

Usage: `sweep stop`
- Stop sweep mode

Usage: `sweep <ms>`
- Set dwell time per channel (10-5000ms, default 200)

Notes:
- Uses fast `set_nrf_channel()` on slaves (~microseconds, not 100ms radio.init())
- Hardware switch position 3 enables hardware sweep mode
- Software sweep shares state with hardware sweep

### 15. pattern

Jamming burst pattern control. Pattern is sent to all slaves via I2C.

Usage: `pattern`
- Show current pattern type and timing

Usage: `pattern continuous`
- Default. Transmit continuously with no breaks.

Usage: `pattern pulsed <ms>`
- Alternating on/off periods of equal duration (5-5000ms).
- Master drives START/STOP cycles via I2C.
- Example: `pattern pulsed 100` -> 100ms jamming, 100ms silent

Usage: `pattern random`
- Random NRF frequency hop per packet (slave self-randomizes using LFSR).
- Frequency offset changes pseudo-randomly within the assigned channel.

Usage: `pattern burst <on_ms> <off_ms>`
- Custom on/off timing (5-5000ms each).
- Master drives START/STOP cycles via I2C.
- Example: `pattern burst 200 50` -> 200ms jamming, 50ms silent

### 16. profile save \<name\>

Save the current configuration (mode, channel, slave assignments) as a named profile in EEPROM.

Usage: `profile save <name>`
- name: up to 16 characters
- Max 16 profiles

### 17. profile load \<name\>

Load a previously saved profile.

Usage: `profile load <name>`
- Restores mode, channel, and slave slot configuration
- Does NOT automatically start jamming

### 18. profile list

List all saved profiles with their names and modes.

Example output:
```
Saved profiles:
 office  (ch6)
 outside (full)
 sweep_custom  (custom)
```

### 19. profile delete \<name\>

Delete a saved profile.

Usage: `profile delete <name>`

## MODES OVERVIEW

### SINGLE CHANNEL MODE (Fan-Out)

Slaves spread across the 22MHz channel width at 2MHz intervals.
Best for: Complete coverage of a single Wi-Fi channel

Usage: `channel <1-13>`
Example: `channel 6` -> Slaves spread 2426-2448 MHz (covering channel 6's full width)

Fan-out formula: `center_freq + (local_idx * 2) - (group_size - 1)`
- With 12 slaves (group_size=12): offsets -11 to +11 MHz from center
- local_idx 0: center - 11 MHz
- local_idx 11: center + 11 MHz

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
Example: `set 4@1,2@6,2@11,4@6`
  -> 4 slaves on ch1, 2 on ch6, 2 on ch11, 4 on ch6
  -> Each channel group uses independent fan-out for full width coverage

**Local Index Fan-Out**: Each channel group is independently centered using
the formula: `center_freq + (local_idx * 2) - (group_size - 1)`

Example: `set 4@1,4@6,4@11`
  -> Each group has 4 slaves (group_size=4)
  -> Offsets within each group: -3, -1, +1, +3 MHz from channel center
  -> Channel 1 group: 2409, 2411, 2413, 2415 MHz
  -> Channel 6 group: 2434, 2436, 2438, 2440 MHz
  -> Channel 11 group: 2459, 2461, 2463, 2465 MHz

### SWEEP MODE

Cycles all 12 slaves through Wi-Fi channels 1-13 at configurable dwell time.
Best for: Systematic channel-by-channel jamming

Usage: `sweep start` / `sweep stop` / `sweep <ms>`
- Dwell default: 200ms per channel
- Full cycle (13 channels): 2.6s at default dwell

### ADAPTIVE MODE

Scans spectrum, identifies busiest channels, assigns slaves dynamically.
Best for: Reacting to changing RF environment

Usage: `adaptive` (one-shot) / `adaptive start` (periodic)
- 2-second scan of all 13 channels
- Assigns slaves to channels sorted by activity
- Threshold-based filtering available
- Fallback to full spectrum if no channels above threshold

### PATTERN MODES

Control the transmission pattern for jamming.

| Pattern | Usage | Timing Source |
|---------|-------|---------------|
| continuous | `pattern continuous` | N/A |
| pulsed | `pattern pulsed <ms>` | Master (I2C START/STOP) |
| random | `pattern random` | Slave (LFSR per packet) |
| burst | `pattern burst <on> <off>` | Master (I2C START/STOP) |

## HARDWARE SWITCH

The 3-position ON-OFF-ON switch provides hardware control:

| Position | Pin | Action |
|----------|-----|--------|
| 1 (ON) | D2 to GND | Full spectrum jamming |
| Center | Neither | Off (software control) |
| 3 (ON) | D3 to GND | Sweep mode |

Behavior:
- Switch overrides software commands for all modes
- D2 position takes priority over D3 (if both active, D2 = full spectrum)
- Moving to center stops jamming
- Software commands are blocked while switch is ON
- Serial messages confirm switch state changes

## USAGE EXAMPLES

### Example 1: Single Channel Jamming (with Fan-Out)
1. Connect to master via USB serial (115200 baud)
2. Type: `channel 6`
3. Type: `status`
4. Type: `start`
5. All 12 slaves spread across channel 6 (2426-2448 MHz)

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

### Example 4: RF Site Survey
1. Type: `snapshot 10` — 10-second spectrum survey
2. Identify busiest channels from the bar graph
3. Type: `set 4@1,4@6,4@11` — assign slaves to busiest channels
4. Type: `start`

### Example 5: Adaptive Jamming
1. Type: `adaptive thresh 30`
2. Type: `adaptive start`
3. Master scans, assigns, and rescans every 30s
4. Type: `adaptive stop` to end

### Example 6: Sweep Mode
1. Type: `sweep 300` — 300ms per channel
2. Type: `sweep start`
3. Slaves cycle through channels 1-13
4. Type: `sweep stop` to end

### Example 7: Pattern Modes
1. Type: `channel 6`
2. Type: `pattern pulsed 100`
3. Type: `start`
4. Slaves alternate 100ms on/off on channel 6

### Example 8: Profile Presets
1. Setup a configuration: `set 4@1,4@6,4@11`
2. Save it: `profile save my_config`
3. Later load it: `profile load my_config`
4. Then: `start`

### Example 9: Stop and Restart
1. Type: `stop` (slaves idle, config retained)
2. Type: `channel 11` (change to channel 11)
3. Type: `start` (restart on new channel)

### Example 10: Custom Distribution
1. Type: `stop`
2. Type: `set 12@6`
3. Type: `start`
4. All 12 slaves on channel 6

### Example 11: Partial Deployment
1. Type: `set 6@1,6@6`
2. Type: `status`
3. Type: `start`
4. 6 slaves on ch1, 6 on ch6

## FREQUENCY TABLES

### Wi-Fi Channel Frequencies (Single Channel Mode with Fan-Out)
Channel | Center (MHz) | Fan-Out Range (MHz) | Notes
--------|--------------|---------------------|------------------
1       | 2412         | 2401-2423           | Common in US/EU
2       | 2417         | 2406-2428           |
3       | 2422         | 2411-2433           |
4       | 2427         | 2416-2438           |
5       | 2432         | 2421-2443           |
6       | 2437         | 2426-2448           | Non-overlapping with 1,11
7       | 2442         | 2431-2453           |
8       | 2447         | 2436-2458           |
9       | 2452         | 2441-2463           |
10      | 2457         | 2446-2468           |
11      | 2462         | 2451-2473           | Non-overlapping with 1,6
12      | 2467         | 2456-2478           |
13      | 2472         | 2461-2483           | EU only (US limit 11)

Fan-out formula: `center + (local_idx * 2) - (group_size - 1)` where local_idx is 0 to group_size-1
- Single channel mode: group_size=12, so offsets are -11 to +11 MHz

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
channel 0
start
```

### Pattern 6: Site Survey + Adaptive
```
snapshot 10
adaptive thresh 30
adaptive start
```

### Pattern 7: Sweep
```
sweep 250
sweep start
sweep stop
```

### Pattern 8: Profile Workflow
```
set 6@1,6@6
profile save balanced
stop
channel 0
profile save full_spectrum
profile load balanced
start
```

### Pattern 9: Pulsed Pattern
```
channel 6
pattern pulsed 200
start
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

Issue: NRF24L01+ RX Module Not Detected
Solution:
- Check wiring: CE=D9, CSN=D10, MOSI=D11, MISO=D12, SCK=D13
- VCC must be 3.3V (NOT 5V)
- Verify common ground between Nano and NRF module

Issue: Can't Send Software Commands
Solution:
- Check if hardware switch is ON (position 1 or 3)
- Move switch to center position for software control
- Switch overrides all software commands

## QUICK REFERENCE CARD

Command                 | Syntax                     | Effect
------------------------|----------------------------|------------------
help                    | help                       | Show commands
get                     | get or get 0,1,2           | Query slaves
set                     | set 4@1,2@6,2@11           | Custom dist
channel                 | channel 0-13               | Set mode
start                   | start                      | Begin TX
stop                    | stop                       | Halt TX
status                  | status                     | Show distribution & health
snapshot                | snapshot or snapshot 30    | RF spectrum snapshot
scan                    | scan                       | Threshold scan all ch
scan <ch>               | scan 6 30                  | Live waterfall
scan threshold          | scan threshold 50          | Set threshold %
power                   | power 1-4                  | Set TX power
adaptive                | adaptive                   | One-shot adaptive
adaptive start          | adaptive start             | Periodic adaptive
adaptive stop           | adaptive stop              | Stop adaptive
adaptive thresh         | adaptive thresh 30         | Adaptive min %
adaptive intv           | adaptive intv 60           | Rescan interval
sweep                   | sweep                      | Show sweep status
sweep start             | sweep start                | Start sweep
sweep stop              | sweep stop                 | Stop sweep
sweep <ms>              | sweep 300                  | Set dwell
pattern                 | pattern                    | Show pattern
pattern continuous      | pattern continuous         | Continuous TX
pattern pulsed          | pattern pulsed 100         | Pulsed on/off
pattern random          | pattern random             | Freq hop per packet
pattern burst           | pattern burst 200 50       | Custom on/off
profile save            | profile save my_cfg        | Save profile
profile load            | profile load my_cfg        | Load profile
profile list            | profile list               | List profiles
profile delete          | profile delete my_cfg      | Delete profile

### Mode Values
0 = N/A (used by channel 0)
1 = Single Channel (fan-out across 22MHz channel width at 2MHz intervals)
2 = Full Spectrum (spread 60MHz at 5MHz intervals)
3 = Custom Distribution (flexible, uses fan-out within each channel)
4 = Sweep (cycling through channels)

### Power Levels
Value | Command | NRF Register | Output Power
------|---------|-------------|-------------
1 | power 1 | 0 (RF24_PA_MIN) | -18 dBm
2 | power 2 | 1 (RF24_PA_LOW) | -12 dBm
3 | power 3 | 2 (RF24_PA_HIGH) | -6 dBm
4 | power 4 | 3 (RF24_PA_MAX) | 0 dBm

## NOTES

- All commands case-insensitive
- Configuration persists after `stop`
- Physical restart required to clear config
- Slaves numbered 1-12 in output (internal: 0-11)
- Max frequency: 2527 MHz (NRF24L01+ limit)
- Min frequency: 2400 MHz (NRF24L01+ limit)
- Power requirement: 1A+ supply for 13 nodes
- Hardware switch ON-OFF-ON on D2/D3 (active LOW, internal pull-up)
  - Pos 1 (D2=GND): Full spectrum jamming (takes priority)
  - Center: Off (software control)
  - Pos 3 (D3=GND): Sweep mode
- Adaptive mode pauses TX during scan to avoid self-interference
- Random pattern uses slave LFSR for per-packet frequency offset
- Profile EEPROM: up to 16 profiles, auto-initialized on first use
- Threshold-based scan only shows channels above `scan threshold %`
- Sweep uses per-channel dwell timing on master, direct RF_CH write on slave (~µs)
- I2C protocol: 6 bytes (mode, channel, cmd, packed fanout, power, pattern_type)

---

**Version**: 2.0
**Last Updated**: May 2026
**Author**: Ryon Sherman
