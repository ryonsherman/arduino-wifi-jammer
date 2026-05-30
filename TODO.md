# WiFi Jammer - Feature TODO

## High Value / Low Effort

- [ ] **Adaptive Jamming** — Instead of fixed channel assignments, slaves dynamically target the busiest channels detected by master's scanner. Serial commands:
  ```
  adaptive                 Start adaptive jamming (scan and target busiest channels)
  adaptive stop            Stop adaptive jamming, return to idle
  adaptive threshold <n>   Set activity threshold for channel targeting (default: 50)
  adaptive interval <sec>  Set rescan interval in seconds (default: 30)
  ```
  Examples:
  ```
  > adaptive
  Scanning channels...
  Targeting 6 busiest channels: 1, 6, 7, 11, 12, 13
  Adaptive jamming started (rescan in 30s)

  > adaptive threshold 75
  Threshold set to 75

  > adaptive
  Scanning channels...
  Targeting 3 channels above threshold: 6, 11, 13
  Adaptive jamming started (rescan in 30s)

  > adaptive stop
  Adaptive jamming stopped
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

- [ ] **Slave Health Monitoring** — `status` command pings each slave via I2C and reports online/offline state with response time:
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

- [ ] **Sweep Mode** — Instead of parking on one channel, slaves continuously sweep across a range for broader coverage. Controlled via ON-OFF-ON toggle switch:
  - Position 1 (D2 LOW): Full spectrum jamming (fixed channels)
  - Position 2 (center): Off (software control via serial)
  - Position 3 (D3 LOW): Sweep mode

- [ ] **Burst Patterns** — Different jamming patterns via serial commands:
  ```
  pattern continuous           Default, non-stop transmission
  pattern pulsed <ms>          Alternating on/off (e.g., 50ms on, 50ms off)
  pattern random               Random channel hopping within assigned range
  pattern burst <on> <off>     Burst mode (e.g., 100ms burst, 20ms gap)
  pattern                      Show current pattern
  ```
  Examples:
  ```
  > pattern continuous
  Pattern set to continuous

  > pattern pulsed 50
  Pattern set to pulsed (50ms on, 50ms off)

  > pattern
  Pattern: pulsed (50ms on, 50ms off)
  ```

- [ ] **Activity Threshold** — `scan` command auto-identifies channels above a threshold and suggests optimal slave assignments. Serial commands:
  ```
  scan threshold <n>   Set activity threshold (default: 50)
  scan                 Scan and show channels above threshold
  ```
  Examples:
  ```
  > scan threshold 60
  Threshold set to 60

  > scan
  Scanning...
  Channels above threshold (60): 1, 6, 11
  ```

- [x] **LED Status Indicators** — Onboard LED on D13 (SCK) naturally flickers during SPI/NRF activity, providing visual indication of jamming state

## Nice to Have / Higher Effort

- [ ] **Profile Presets** — Save/load different channel configurations. Serial commands:
  ```
  profile save <name>    Save current channel config
  profile load <name>    Load a saved profile
  profile list           List all saved profiles
  profile delete <name>  Delete a profile
  ```
  Examples:
  ```
  > profile save home
  Profile 'home' saved (channels: 1, 6, 11)

  > profile list
  Profiles: home, office, full

  > profile load office
  Loaded 'office' (channels: 6, 11, 13)

  > profile delete office
  Profile 'office' deleted
  ```
