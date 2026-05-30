# Arduino Wi-Fi Jammer - Feature TODO

## High Value / Low Effort

- [x] **Adaptive Jamming** — Instead of fixed channel assignments, slaves dynamically target the busiest channels detected by master's scanner. Serial commands:
  ```
  adaptive                 One-shot: scan and target busiest channels
  adaptive start           Periodic adaptive jamming (rescan every N seconds)
  adaptive stop            Stop adaptive jamming, return to idle
  adaptive thresh <n>      Set activity threshold % (0=auto, pick top 12)
  adaptive intv <n>        Set rescan interval in seconds (default: 30)
  ```
  Examples:
  ```
  > adaptive
  Adaptive scanning...
  Targeting 6 busiest channels
  Channels: 1 (85%), 6 (72%), 11 (68%), 7 (12%), 13 (8%), 3 (5%)
  Adaptive jamming started (one-shot)

  > adaptive start
  Adaptive scanning...
  Targeting 6 busiest channels
  ...
  Adaptive jamming active (rescan every 30s)

  > adaptive thresh 50
  Adaptive threshold set to 50%

  > adaptive
  Adaptive scanning...
  Targeting 3 channels >= 50%
  Channels: 1 (85%), 6 (72%), 11 (68%)
  Adaptive jamming started (one-shot)

  > adaptive stop
  Adaptive jamming stopped

  > adaptive thresh 0
  Adaptive threshold set to 0% (0=auto, pick top N)
  ```

- [x] **Power Levels** — Adjust NRF24L01+ TX power. Serial commands:
  ```
  power <1-4>     Set TX power level (1=MIN, 2=LOW, 3=HIGH, 4=MAX)
  power           Show current power level
  ```
  Examples:
  ```
  > power 2
  Power set to 2 (LOW)

  > power
  Power: 2 (LOW)

  > power 5
  Error: Invalid power level (use 1-4)
  ```

- [x] **Slave Health Monitoring** — `status` command pings each slave via I2C and reports online/offline state with response time (µs):
  ```
  > status
  Mode: Full spectrum
  Switch: ON (position 1)
  Power: 4 (MAX)
  Pattern: continuous

  Slaves: 11/12 online
    #1  0x10: CH 14  [OK] 2ms
    #2  0x11: CH 19  [OK] 1ms
    #3  0x12: CH 24  [OK] 2ms
    #4  0x13: --     [OFFLINE]
    #5  0x14: CH 34  [OK] 1ms
    ...
  ```

## Medium Value / Medium Effort

- [x] **Sweep Mode** — Master cycles all 12 slaves through Wi-Fi channels 1-13 for intermittent coverage of the full 2.4GHz band. Controlled via ON-OFF-ON toggle switch:
  - Position 1 (D2 LOW): Full spectrum jamming (fixed channels)
  - Position 2 (center): Off (software control via serial)
  - Position 3 (D3 LOW): Sweep mode
  Serial commands:
  ```
  sweep              Show current sweep status
  sweep start        Start sweep mode
  sweep stop         Stop sweep mode  
  sweep <ms>         Set dwell time per channel (10-5000ms)
  ```
  Examples:
  ```
  > sweep start
  Sweep mode started

  > sweep 500
  Sweep dwell set to 500ms

  > sweep
  Sweep: active, ch3, dwell 200ms

  > sweep stop
  Sweep stopped
  ```

- [x] **Burst Patterns** — Different jamming patterns via serial commands. I2C protocol extended from 5 to 6 bytes (byte 6 = pattern_type). Master handles pulsed/burst START/STOP timing; slave handles random frequency hopping:
  ```
  pattern                      Show current pattern
  pattern continuous           Continuous (default)
  pattern pulsed <ms>          Alternating on/off (e.g., 50ms on, 50ms off)
  pattern random               Random freq hop within assigned range
  pattern burst <on_ms> <off_ms>  Custom on/off intervals
  ```
  Examples:
  ```
  > pattern continuous
  Pattern: continuous

  > pattern pulsed 50
  Pattern: pulsed 50ms

  > pattern random
  Pattern: random

  > pattern burst 100 20
  Pattern: burst 100/20ms

  > pattern
  Pattern: pulsed 50ms
  ```

- [x] **Activity Threshold** — `scan` command with no args scans all 13 Wi-Fi channels for 2 seconds and reports those above a threshold percentage. Includes bar graph visualization:
  ```
  scan                 Scan all channels, show those above threshold
  scan threshold N     Set activity threshold % (default: 50)
  scan threshold       Show current threshold setting
  scan 6               Live scan channel 6 for 10s (legacy)
  scan 6 30            Live scan channel 6 for 30s (legacy)
  ```
  Examples:
  ```
  > scan threshold 60
  Scan threshold set to 60%

  > scan
  Scanning all channels...
  Channels above threshold:
   1  2412 MHz #######.. 68%
   6  2437 MHz #####... 54%
  11  2462 MHz ######## 75%
  Threshold: 60% (scan threshold <n> to change)
  ```

- [x] **LED Status Indicators** — Onboard LED on D13 (SCK) naturally flickers during SPI/NRF activity, providing visual indication of jamming state

## Nice to Have / Higher Effort

- [x] **Profile Presets** — Save/load different channel configurations (EEPROM-backed). Serial commands:
  ```
  profile save <name>    Save current channel config
  profile load <name>    Load a saved profile
  profile list           List all saved profiles
  profile delete <name>  Delete a profile
  ```
  Examples:
  ```
  > profile save home
  Profile 'home' saved

  > profile list
  Saved profiles:
   home  (ch6)

  > profile load office
  Profile 'office' loaded

  > profile delete office
  Profile 'office' deleted
  ```
