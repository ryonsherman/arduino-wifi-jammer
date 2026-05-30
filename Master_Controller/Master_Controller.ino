/**
 * Master_Controller.ino
 * 
 * Master node for distributed Wi-Fi jamming. Runs on Arduino Nano.
 * Handshakes with 12 slave units via I2C (address 0x70),
 * sends channel/mode commands, reports status over USB Serial.
 * 
 * Hardware switch on D2 enables full-spectrum jamming when ON.
 * 
 * Memory: ATmega328P has 2KB RAM, ~1.5KB used by this code + Serial buffer
 */

#include <Wire.h>
#include <SPI.h>
#include <EEPROM.h>

// --- Configuration ---
#define MASTER_ADDR 0x70
#define TOTAL_SLAVES 12

// --- Hardware Switch ---
#define HW_SWITCH_PIN 2  // D2 - connect switch between D2 and GND
#define HW_SWEEP_PIN 3   // D3 - sweep mode (position 3 of ON-OFF-ON)

// --- NRF24L01+ RX Scanner ---
#define NRF_CE_PIN 9
#define NRF_CSN_PIN 10

static uint8_t nrf_read_reg(uint8_t reg) {
  digitalWrite(NRF_CSN_PIN, LOW);
  SPI.transfer(reg);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(NRF_CSN_PIN, HIGH);
  return val;
}

static void nrf_write_reg(uint8_t reg, uint8_t val) {
  digitalWrite(NRF_CSN_PIN, LOW);
  SPI.transfer(0x20 | reg);
  SPI.transfer(val);
  digitalWrite(NRF_CSN_PIN, HIGH);
}

// Compile-time guard: local_idx and group_size are packed into 4-bit nibbles (max 15)
// See send_cmd() byte 4 packing and Slave_Transmitter.ino FANOUT_* macros
#if TOTAL_SLAVES > 15
#error "TOTAL_SLAVES exceeds 15 - nibble packing in send_cmd() byte 4 will overflow"
#endif
#define SLAVE_ADDR_START 0x01

// --- Modes ---
#define MODE_SINGLE_CHANNEL 1
#define MODE_FULL_SPECTRUM 2
#define MODE_CUSTOM 3
#define MODE_SWEEP 4

// --- Commands ---
#define CMD_START 1
#define CMD_STOP 0

// --- Status Polling ---
#define STATUS_INTERVAL_MS 5000

// --- State (optimized layout) ---
static uint8_t slave_count = 0;
static uint8_t selected_channel = 0;
static uint8_t current_mode = MODE_FULL_SPECTRUM;
static bool jamming_active = false;
static bool hw_jamming_active = false;  // Hardware switch state
static uint32_t last_status_ms = 0;
static uint8_t current_power = 3;  // 0=MIN, 1=LOW, 2=HIGH, 3=MAX (default)

// Slave health cache (populated by poll_slaves)
static bool slave_online[TOTAL_SLAVES];
static uint8_t slave_rtt_us[TOTAL_SLAVES];

// Adaptive jamming state
static bool adaptive_active = false;
static uint16_t adaptive_interval_sec = 30;
static uint8_t adaptive_threshold = 0;
static uint32_t last_adaptive_ms = 0;

// Sweep mode state
static bool hw_sweep_active = false;   // D3 LOW = sweep via HW switch
static bool sweep_active = false;      // sweep via software command
static uint8_t sweep_channel = 1;      // current sweep position
static uint16_t sweep_dwell_ms = 200;  // ms per channel
static uint32_t last_sweep_ms = 0;

// Burst pattern state
static uint8_t current_pattern = 0;    // 0=continuous, 1=pulsed, 2=random, 3=burst
static uint16_t pattern_on_ms = 50;    // on time (ms)
static uint16_t pattern_off_ms = 50;   // off time (ms)
static bool pattern_state = true;      // true=on, false=off
static uint32_t pattern_timer = 0;

// Scan threshold
static uint8_t scan_threshold = 50;  // default 50%

// Packed slave config: 4 bytes per slave instead of 4 (struct padding eliminated)
// Layout: [channel:4bits][active:1bit][unused:3bits] = 1 byte per slave
static uint8_t slave_cfg[TOTAL_SLAVES];  // Was 48 bytes (struct), now 12 bytes

// Bounds-checked config access macros (WARN-2 fix)
#define CFG_GET_CHANNEL(i)  ((i) < TOTAL_SLAVES ? (slave_cfg[i] >> 4) : 0)
#define CFG_GET_ACTIVE(i)   ((i) < TOTAL_SLAVES ? (slave_cfg[i] & 0x01) : 0)

// Profile presets (stored in EEPROM)
#define PROFILE_MAGIC      0x4A
#define OFFSET_PROF_COUNT  1
#define OFFSET_PROFILES    2
#define PROFILE_NAME_LEN   16
#define PROFILE_SIZE       (1 + 1 + TOTAL_SLAVES + PROFILE_NAME_LEN)  // mode, channel, cfg, name

static uint16_t profile_offset(uint8_t idx) {
  return OFFSET_PROFILES + idx * PROFILE_SIZE;
}
static void profile_init(void) {
  if (EEPROM.read(0) != PROFILE_MAGIC) { EEPROM.write(0, PROFILE_MAGIC); EEPROM.write(OFFSET_PROF_COUNT, 0); }
}
static uint8_t profile_count(void) { return EEPROM.read(OFFSET_PROF_COUNT); }
#define CFG_SET(i, ch, act) do { if ((i) < TOTAL_SLAVES) slave_cfg[i] = (((ch) & 0x0F) << 4) | ((act) ? 1 : 0); } while(0)

// ── Profile Presets (EEPROM-backed) ──────────────────────────────
static void trim_str(char* s) {
  char* p = s;
  while (*p == ' ') p++;
  if (p != s) { memmove(s, p, strlen(p) + 1); }
  for (int i = strlen(s) - 1; i >= 0 && s[i] == ' '; i--) s[i] = 0;
}
static bool profile_write(uint8_t idx, const char* name) {
  uint16_t off = profile_offset(idx);
  EEPROM.write(off++, current_mode);
  EEPROM.write(off++, selected_channel);
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) EEPROM.write(off++, slave_cfg[i]);
  for (uint8_t i = 0; i < PROFILE_NAME_LEN; i++) EEPROM.write(off++, name[i] ? name[i] : 0);
  return true;
}
static bool profile_name_match(uint8_t idx, const char* name) {
  uint16_t off = profile_offset(idx) + 2 + TOTAL_SLAVES;
  for (uint8_t i = 0; i < PROFILE_NAME_LEN; i++) {
    uint8_t c = EEPROM.read(off++);
    if (c != (uint8_t)name[i]) return false;
    if (c == 0 && name[i] == 0) return true;
  }
  return true;
}
static void profile_load(uint8_t idx) {
  uint16_t off = profile_offset(idx);
  current_mode = EEPROM.read(off++);
  selected_channel = EEPROM.read(off++);
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) slave_cfg[i] = EEPROM.read(off++);
}
static uint8_t find_profile(const char* name) {
  uint8_t cnt = profile_count();
  for (uint8_t i = 0; i < cnt; i++) if (profile_name_match(i, name)) return i;
  return 255;
}
static bool profile_save(const char* name) {
  profile_init();
  uint8_t cnt = profile_count();
  for (uint8_t i = 0; i < cnt; i++) { if (profile_name_match(i, name)) { profile_write(i, name); return true; } }
  if (cnt >= 16) return false;
  profile_write(cnt, name);
  EEPROM.write(OFFSET_PROF_COUNT, cnt + 1);
  return true;
}
static bool profile_delete_at(uint8_t idx) {
  uint8_t cnt = profile_count();
  if (idx >= cnt) return false;
  for (uint8_t i = idx; i < cnt - 1; i++) {
    uint16_t src = profile_offset(i + 1);
    uint16_t dst = profile_offset(i);
    for (uint8_t b = 0; b < PROFILE_SIZE; b++) EEPROM.write(dst++, EEPROM.read(src++));
  }
  EEPROM.write(OFFSET_PROF_COUNT, cnt - 1);
  return true;
}
static void cmd_profile(const char* rest) {
  char name[PROFILE_NAME_LEN];
  if (!rest || rest[0] == 0) { profile_list(); return; }
  if (strncmp_P(rest, PSTR("save "), 5) == 0) {
    if (rest[5] == 0) { Serial.println(F("Usage: profile save <name>")); return; }
    strncpy(name, rest + 5, PROFILE_NAME_LEN - 1); name[PROFILE_NAME_LEN - 1] = 0;
    trim_str(name);
    if (name[0] == 0) { Serial.println(F("Invalid name")); return; }
    if (profile_save(name)) { Serial.print(F("Profile '")); Serial.print(name); Serial.println(F("' saved")); }
    else { Serial.println(F("Max 16 profiles")); }
  } else if (strncmp_P(rest, PSTR("load "), 5) == 0) {
    if (rest[5] == 0) { Serial.println(F("Usage: profile load <name>")); return; }
    strncpy(name, rest + 5, PROFILE_NAME_LEN - 1); name[PROFILE_NAME_LEN - 1] = 0;
    trim_str(name);
    uint8_t idx = find_profile(name);
    if (idx == 255) { Serial.print(F("Profile '")); Serial.print(name); Serial.println(F("' not found")); return; }
    profile_load(idx);
    Serial.print(F("Profile '")); Serial.print(name); Serial.println(F("' loaded"));
  } else if (strncmp_P(rest, PSTR("delete "), 7) == 0) {
    if (rest[7] == 0) { Serial.println(F("Usage: profile delete <name>")); return; }
    strncpy(name, rest + 7, PROFILE_NAME_LEN - 1); name[PROFILE_NAME_LEN - 1] = 0;
    trim_str(name);
    uint8_t idx = find_profile(name);
    if (idx == 255) { Serial.print(F("Profile '")); Serial.print(name); Serial.println(F("' not found")); return; }
    profile_delete_at(idx);
    Serial.print(F("Profile '")); Serial.print(name); Serial.println(F("' deleted"));
  } else if (strcmp_P(rest, PSTR("list")) == 0) {
    profile_list();
  } else {
    Serial.println(F("profile [save|load|delete|list]"));
  }
}
static void profile_list(void) {
  profile_init();
  uint8_t cnt = profile_count();
  if (cnt == 0) { Serial.println(F("No saved profiles")); return; }
  Serial.println(F("Saved profiles:"));
  for (uint8_t i = 0; i < cnt; i++) {
    uint16_t off = profile_offset(i);
    uint8_t mode = EEPROM.read(off++);
    uint8_t ch = EEPROM.read(off++);
    Serial.write(' ');
    for (uint8_t j = 0; j < PROFILE_NAME_LEN; j++) {
      uint8_t c = EEPROM.read(off + TOTAL_SLAVES + j);
      if (c == 0) break;
      Serial.write(c);
    }
    Serial.print(F("  ("));
    if (mode == MODE_SINGLE_CHANNEL) { Serial.print(F("ch")); Serial.print(ch); }
    else if (mode == MODE_FULL_SPECTRUM) { Serial.print(F("full")); }
    else { Serial.print(F("custom")); }
    Serial.println(')');
  }
}

/**
 * Send I2C command to a specific slave
 * Byte 4 packs local_idx (high nibble) + group_size (low nibble) for fan-out
 * Byte 5: power level (0=MIN, 1=LOW, 2=HIGH, 3=MAX)
 * Byte 6: pattern_type (0=continuous, 1=pulsed, 2=random, 3=burst)
 */
static void send_cmd(uint8_t slave_id, uint8_t mode, uint8_t channel, uint8_t cmd, uint8_t local_idx, uint8_t group_size) {
  Wire.beginTransmission(SLAVE_ADDR_START + slave_id);
  Wire.write(mode);
  Wire.write(channel);
  Wire.write(cmd);
  Wire.write(((local_idx & 0x0F) << 4) | (group_size & 0x0F));
  Wire.write(current_power);
  Wire.write(current_pattern);
  Wire.endTransmission();
}

/**
 * Send I2C command to all slaves (single channel or full spectrum mode)
 */
static void send_cmd_all(uint8_t mode, uint8_t channel, uint8_t cmd) {
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    send_cmd(i, mode, channel, cmd, i, TOTAL_SLAVES);
  }
}

/**
 * Send custom commands to each slave based on their config
 * Uses local indexing within each channel group for balanced fan-out
 * 
 * One-pass algorithm: iterate by channel, then by slave.
 * For each channel, first count active slaves (group_size), then send
 * commands to each with incrementing local_idx. Only 2 bytes stack usage.
 * 
 * NOTE: This channel-grouping logic must stay in sync with print_freq_map().
 * Both functions compute (group_size, local_idx) per channel but use different
 * algorithms optimized for their use case:
 * - send_custom_cmds(): one-pass, sends immediately (no array storage)
 * - print_freq_map(): two-pass with arrays (channels must print in slave order)
 * If you modify the grouping logic here, update print_freq_map() to match.
 */
static void send_custom_cmds(uint8_t cmd) {
  for (uint8_t ch = 1; ch <= 13; ch++) {
    // Count slaves on this channel
    uint8_t group_size = 0;
    for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
      if (CFG_GET_ACTIVE(i) && CFG_GET_CHANNEL(i) == ch) {
        group_size++;
      }
    }
    if (group_size == 0) continue;
    
    // Send commands with local index
    uint8_t local_idx = 0;
    for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
      if (CFG_GET_ACTIVE(i) && CFG_GET_CHANNEL(i) == ch) {
        send_cmd(i, MODE_CUSTOM, ch, cmd, local_idx++, group_size);
      }
    }
  }
}

/**
 * Scan all slaves via I2C handshake
 * @return: Number of responsive slaves (0-12)
 */
static uint8_t scan_slaves() {
  uint8_t count = 0;
  
  Serial.println(F("\n=== Scanning Slaves ==="));
  
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    Wire.beginTransmission(SLAVE_ADDR_START + i);
    uint8_t status = Wire.endTransmission();
    
    Serial.print(status == 0 ? F("[OK] ") : F("[--] "));
    Serial.print(F("Slave "));
    Serial.println(i);
    
    if (status == 0) count++;
  }
  
  return count;
}

/**
 * Calculate frequency offset for display
 * Uses local fan-out formula: center + (local_idx * 2) - (group_size - 1)
 */
static uint8_t calc_freq(uint8_t local_idx, uint8_t group_size, uint8_t mode, uint8_t channel) {
  if (mode == MODE_SINGLE_CHANNEL || mode == MODE_CUSTOM || mode == MODE_SWEEP) {
    // center = 12 + (ch-1)*5 = 7 + ch*5
    // offset = (local_idx * 2) - (group_size - 1)
    // freq = center + offset = 7 + ch*5 + local_idx*2 - group_size + 1 = 8 + ch*5 + local_idx*2 - group_size
    int8_t offset = (local_idx << 1) - (group_size - 1);
    uint8_t center = 7 + (channel << 2) + channel;  // 7 + ch*5
    return center + offset;
  }
  // Full spectrum: 14 + local_idx*5  (staggered between channels for max coverage)
  return 14 + (local_idx << 2) + local_idx;
}

/**
 * Check if NRF24L01+ is responding by verifying register read/write
 * Returns true if module responds correctly
 */
static bool nrf_check() {
  pinMode(NRF_CE_PIN, OUTPUT);
  pinMode(NRF_CSN_PIN, OUTPUT);
  digitalWrite(NRF_CE_PIN, LOW);
  digitalWrite(NRF_CSN_PIN, HIGH);
  SPI.begin();
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  
  // Test 1: Read CONFIG register - should have sane default (0x08 after reset)
  uint8_t cfg = nrf_read_reg(0x00);
  if (cfg == 0xFF || cfg == 0x00) {
    // 0xFF = no device (MISO pulled high), 0x00 = stuck low
    SPI.endTransaction();
    SPI.end();
    return false;
  }
  
  // Test 2: Write/read RF_CH register with test value
  uint8_t test_ch = 0x4C;  // 76 decimal - arbitrary test value
  nrf_write_reg(0x05, test_ch);
  delayMicroseconds(10);
  uint8_t readback = nrf_read_reg(0x05);
  
  // Reset RF_CH to 0 before returning
  nrf_write_reg(0x05, 0x00);
  
  SPI.endTransaction();
  SPI.end();
  
  return (readback == test_ch);
}

/**
 * Initialize NRF24L01+ in RX scan mode using direct register access
 */
static bool nrf_scan_init() {
  pinMode(NRF_CE_PIN, OUTPUT);
  pinMode(NRF_CSN_PIN, OUTPUT);
  digitalWrite(NRF_CE_PIN, LOW);
  digitalWrite(NRF_CSN_PIN, HIGH);
  SPI.begin();
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  nrf_write_reg(0x00, 0x0B);  // CONFIG: PWR_UP=1, PRIM_RX=1, CRC=0
  delay(5);
  return true;
}

/**
 * Scan a single NRF channel for carrier detect (RPD)
 * Returns 1 if signal > ~-64 dBm detected
 */
static uint8_t nrf_scan_channel(uint8_t ch) {
  digitalWrite(NRF_CE_PIN, LOW);
  delayMicroseconds(4);
  nrf_write_reg(0x05, ch);
  delayMicroseconds(4);
  digitalWrite(NRF_CE_PIN, HIGH);
  delayMicroseconds(130);
  return nrf_read_reg(0x09) & 0x01;
}

/**
 * Single RF spectrum snapshot
 * Collects data for 5 seconds, then displays aggregated results
 */
static void cmd_snapshot(uint8_t seconds) {
  if (!nrf_scan_init()) return;

  Serial.println(F("\n======= RF Snapshot ======="));
  Serial.print(F("Collecting data"));

  uint16_t counts[14] = {0};  // channels 1-13
  uint16_t passes = 0;
  uint32_t start = millis();
  uint32_t duration = (uint32_t)seconds * 1000;
  uint8_t last_dot = 0;

  while (millis() - start < duration) {
    for (uint8_t ch = 1; ch <= 13; ch++) {
      uint8_t nrf_center = 7 + ch * 5;
      if (nrf_scan_channel(nrf_center)) counts[ch]++;
    }
    passes++;
    
    // Print dot every 3 seconds
    uint8_t elapsed_dots = (millis() - start) / 3000;
    if (elapsed_dots > last_dot) {
      Serial.print('.');
      last_dot = elapsed_dots;
    }
  }

  digitalWrite(NRF_CE_PIN, LOW);
  SPI.endTransaction();
  SPI.end();

  Serial.println(F(" done"));
  Serial.println(F("---------------------------"));
  Serial.println(F("Ch  Freq        Signal"));
  for (uint8_t ch = 1; ch <= 13; ch++) {
    uint8_t pct = (uint32_t)counts[ch] * 100 / passes;

    if (ch < 10) Serial.print(' ');
    Serial.print(ch);
    Serial.print(F("  "));
    Serial.print(2400 + 7 + ch * 5);
    Serial.print(F(" MHz "));

    for (uint8_t i = 0; i < 10; i++) {
      Serial.print(i * 10 < pct ? '#' : '.');
    }
    Serial.print(F(" "));
    if (pct < 10) Serial.print(' ');
    Serial.print(pct);
    Serial.println('%');
  }
  
  Serial.println(F("==========================="));
}

/**
 * Continuous RF waterfall scan for a single Wi-Fi channel
 * Shows signal intensity over time with a bar graph per second
 */
static void cmd_scan_channel(uint8_t wifi_ch, uint8_t seconds) {
  if (!nrf_scan_init()) return;

  uint8_t nrf_center = 7 + wifi_ch * 5;
  uint16_t freq = 2400 + nrf_center;

  Serial.println(F("\n======== RF Live Scan ========"));
  Serial.print(F("Channel "));
  Serial.print(wifi_ch);
  Serial.print(F(" ("));
  Serial.print(freq);
  Serial.print(F(" MHz) for "));
  Serial.print(seconds);
  Serial.println(F("s"));
  Serial.println(F("------------------------------"));

  uint32_t start = millis();
  uint8_t last_sec = 255;
  uint16_t hits = 0;
  uint16_t samples = 0;

  while (millis() - start < (uint32_t)seconds * 1000) {
    uint8_t cur_sec = (millis() - start) / 1000;
    
    if (nrf_scan_channel(nrf_center)) hits++;
    samples++;
    
    // Print once per second
    if (cur_sec != last_sec && last_sec != 255) {
      uint8_t pct = samples > 0 ? (uint32_t)hits * 100 / samples : 0;
      
      // 20-char bar
      for (uint8_t i = 0; i < 20; i++) {
        Serial.print(i * 5 < pct ? '#' : '.');
      }
      Serial.print(F(" "));
      if (pct < 10) Serial.print(' ');
      if (pct < 100) Serial.print(' ');
      Serial.print(pct);
      Serial.print(F("% "));
      if (cur_sec < 10) Serial.print(' ');
      Serial.print(cur_sec);
      Serial.println('s');
      
      hits = 0;
      samples = 0;
    }
    last_sec = cur_sec;
  }

  digitalWrite(NRF_CE_PIN, LOW);
  SPI.endTransaction();
  SPI.end();
  
  Serial.println(F("=============================="));
}

/**
 * Print frequency distribution for all slaves
 * 
 * Two-pass algorithm for Custom mode (14 bytes stack):
 * - Pass 1: count slaves per channel (populates group_size[])
 * - Pass 2: print with incrementing local_idx per channel
 * 
 * Trade-off: 14 bytes stack vs O(n²) recomputation per slave.
 * On Nano with 2KB RAM, this is acceptable for display-only code.
 * 
 * NOTE: This channel-grouping logic must stay in sync with send_custom_cmds().
 * Both functions compute (group_size, local_idx) per channel but use different
 * algorithms optimized for their use case:
 * - print_freq_map(): two-pass with arrays (channels must print in slave order)
 * - send_custom_cmds(): one-pass, sends immediately (no array storage)
 * If you modify the grouping logic here, update send_custom_cmds() to match.
 */
static void print_freq_map() {
  Serial.println(F("\n=== Frequency Map ==="));
  
  if (current_mode == MODE_SINGLE_CHANNEL) {
    Serial.print(F("Mode: Single Ch "));
    Serial.println(selected_channel);
  } else if (current_mode == MODE_FULL_SPECTRUM) {
    Serial.println(F("Mode: Full Spectrum"));
  } else if (current_mode == MODE_SWEEP) {
    Serial.print(F("Mode: Sweep ch"));
    Serial.print(sweep_channel);
    Serial.print(F(", dwell "));
    Serial.print(sweep_dwell_ms);
    Serial.println(F("ms"));
  } else {
    Serial.println(F("Mode: Custom"));
  }
  
  if (current_mode == MODE_SWEEP) {
    Serial.print(F("All slaves sweeping ch1-13, currently ch"));
    Serial.println(sweep_channel);
  } else if (current_mode == MODE_CUSTOM) {
    // Two-pass for custom mode: count then print
    uint8_t group_size[14] = {0};  // 14 bytes stack (channels 1-13, index 0 unused)
    
    // Pass 1: count slaves per channel
    for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
      if (CFG_GET_ACTIVE(i)) {
        uint8_t ch = CFG_GET_CHANNEL(i);
        if (ch <= 13) group_size[ch]++;
      }
    }
    
    // Pass 2: print with local indices
    uint8_t local_idx[14] = {0};  // 14 bytes stack
    for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
      Serial.print(F("S"));
      Serial.print(i);
      Serial.print(F(": "));
      
      if (!CFG_GET_ACTIVE(i)) {
        Serial.println(F("IDLE"));
        continue;
      }
      
      uint8_t ch = CFG_GET_CHANNEL(i);
      uint8_t freq = calc_freq(local_idx[ch], group_size[ch], MODE_CUSTOM, ch);
      local_idx[ch]++;
      Serial.print(2400 + freq);
      Serial.println(F(" MHz"));
    }
  } else {
    // Single channel or full spectrum: simple iteration
    for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
      Serial.print(F("S"));
      Serial.print(i);
      Serial.print(F(": "));
      uint8_t freq = calc_freq(i, TOTAL_SLAVES, current_mode, selected_channel);
      Serial.print(2400 + freq);
      Serial.println(F(" MHz"));
    }
  }
}

/**
 * Print status summary
 */
static void print_status() {
  Serial.println(F("\n=== Status ==="));
  
  uint8_t active = 0;
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    if (current_mode != MODE_CUSTOM || CFG_GET_ACTIVE(i)) active++;
  }
  
  Serial.print(F("Active: "));
  Serial.print(current_mode == MODE_CUSTOM ? active : TOTAL_SLAVES);
  Serial.println(F("/12"));
  
  if (current_mode == MODE_SINGLE_CHANNEL) {
    Serial.print(F("Channel: "));
    Serial.println(selected_channel);
  } else if (current_mode == MODE_FULL_SPECTRUM) {
    Serial.println(F("Channel: All (spectrum)"));
  } else if (current_mode == MODE_SWEEP) {
    Serial.print(F("Sweep ch"));
    Serial.println(sweep_channel);
  } else {
    Serial.print(F("Channels: "));
    uint16_t seen = 0;
    bool first = true;
    for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
      if (CFG_GET_ACTIVE(i)) {
        uint8_t ch = CFG_GET_CHANNEL(i);
        if (!(seen & (1 << ch))) {
          seen |= (1 << ch);
          if (!first) Serial.print(',');
          first = false;
          Serial.print(ch);
        }
      }
    }
    Serial.println();
  }
  
  // Show sweep/switch status
  if (hw_sweep_active) {
    Serial.println(F("Switch: SWEEP (position 3)"));
  } else if (sweep_active) {
    Serial.println(F("Mode: Sweep"));
  }
  
  // Show current pattern
  if (current_pattern == 0) Serial.println(F("Pattern: continuous"));
  else if (current_pattern == 1) { Serial.print(F("Pattern: pulsed ")); Serial.print(pattern_on_ms); Serial.println(F("ms")); }
  else if (current_pattern == 2) Serial.println(F("Pattern: random"));
  else if (current_pattern == 3) { Serial.print(F("Pattern: burst ")); Serial.print(pattern_on_ms); Serial.print(F("/")); Serial.print(pattern_off_ms); Serial.println(F("ms")); }
  
  // Per-slave health
  Serial.println(F("\n-- Slaves --"));
  uint8_t online = 0;
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    uint8_t ch;
    if (current_mode == MODE_FULL_SPECTRUM) {
      ch = 14 + i * 5;
    } else if (current_mode == MODE_SINGLE_CHANNEL || current_mode == MODE_SWEEP) {
      ch = selected_channel;
    } else {
      ch = CFG_GET_CHANNEL(i);
    }
    
    Serial.print(F("  #"));
    Serial.print(i + 1);
    Serial.print(F(" 0x"));
    if (SLAVE_ADDR_START + i < 0x10) Serial.write('0');
    Serial.print(SLAVE_ADDR_START + i, HEX);
    Serial.write(' ');
    
    if (slave_online[i]) {
      Serial.print(F("CH "));
      if (ch < 10) Serial.write(' ');
      Serial.print(ch);
      Serial.print(F("  [OK] "));
      Serial.print(slave_rtt_us[i]);
      Serial.println(F("us"));
      online++;
    } else {
      Serial.println(F("--  [OFFLINE]"));
    }
  }
  Serial.print(online);
  Serial.println(F("/12 online"));
}

/**
 * Poll all slaves and print status
 */
static void poll_slaves() {
  uint8_t active = 0;
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    uint32_t t0 = micros();
    Wire.requestFrom((uint8_t)(SLAVE_ADDR_START + i), (uint8_t)1);
    uint16_t elapsed = (uint16_t)(micros() - t0);
    if (Wire.available()) {
      slave_online[i] = true;
      slave_rtt_us[i] = elapsed > 255 ? 255 : (uint8_t)elapsed;
      if (Wire.read()) active++;
    } else {
      slave_online[i] = false;
      slave_rtt_us[i] = 255;
    }
  }
  
  Serial.print(F("[STATUS] "));
  Serial.print(active);
  Serial.print(F("/12 jamming"));
  
  if (current_mode == MODE_FULL_SPECTRUM) {
    Serial.println(F(" (spectrum)"));
  } else if (current_mode == MODE_SINGLE_CHANNEL) {
    Serial.print(F(" ch"));
    Serial.println(selected_channel);
  } else {
    Serial.println(F(" (custom)"));
  }
}

/**
 * Cancel adaptive jamming mode (called by mode-changing commands)
 */
static void stop_sweep(void);
static void cancel_adaptive() {
  if (adaptive_active) {
    adaptive_active = false;
    Serial.println(F("Adaptive mode cancelled"));
  }
  if (sweep_active) {
    stop_sweep();
  }
}

/**
 * Quick scan of all 13 Wi-Fi channels, reports those above threshold
 */
static void cmd_scan_threshold(void) {
  if (!nrf_scan_init()) return;
  Serial.println(F("\nScanning all channels..."));
  uint16_t counts[14] = {0};
  uint16_t passes = 0;
  uint32_t start = millis();
  while (millis() - start < 2000) {
    for (uint8_t ch = 1; ch <= 13; ch++) {
      if (nrf_scan_channel(7 + ch * 5)) counts[ch]++;
    }
    passes++;
  }
  digitalWrite(NRF_CE_PIN, LOW);
  SPI.endTransaction();
  SPI.end();
  Serial.println(F("Channels above threshold:"));
  uint8_t found = 0;
  for (uint8_t ch = 1; ch <= 13; ch++) {
    uint8_t pct = passes > 0 ? (uint32_t)counts[ch] * 100 / passes : 0;
    if (pct >= scan_threshold) {
      if (ch < 10) Serial.write(' ');
      Serial.print(ch);
      Serial.print(F("  "));
      Serial.print(2400 + 7 + ch * 5);
      Serial.print(F(" MHz "));
      for (uint8_t i = 0; i < 10; i++) Serial.print(i * 10 < pct ? '#' : '.');
      Serial.print(F(" "));
      Serial.print(pct);
      Serial.println(F("%"));
      found++;
    }
  }
  if (found == 0) Serial.println(F("  None"));
  Serial.print(F("Threshold: "));
  Serial.print(scan_threshold);
  Serial.println(F("% (scan threshold <n> to change)"));
}

/**
 * Run one adaptive cycle: stop jamming, scan all 13 Wi-Fi channels,
 * assign slaves to busiest channels, resume jamming.
 */
static void cmd_adaptive() {
  // Stop jamming first so slave NRFs don't pollute the scan
  if (jamming_active) {
    send_cmd_all(current_mode, selected_channel, CMD_STOP);
    jamming_active = false;
    delay(50);
  }

  if (!nrf_scan_init()) {
    Serial.println(F("NRF24L01+ not available"));
    return;
  }

  Serial.println(F("\nAdaptive scanning..."));

  uint16_t counts[14] = {0};
  uint16_t passes = 0;
  uint32_t start = millis();
  uint8_t last_dot = 0;

  while (millis() - start < 2000) {
    for (uint8_t ch = 1; ch <= 13; ch++) {
      if (nrf_scan_channel(7 + ch * 5)) counts[ch]++;
    }
    passes++;

    uint8_t s = (millis() - start) / 1000;
    if (s > last_dot) { Serial.print('.'); last_dot = s; }
  }

  digitalWrite(NRF_CE_PIN, LOW);
  SPI.endTransaction();
  SPI.end();
  Serial.println(F(" done"));

  // Convert to percentages in-place
  for (uint8_t ch = 1; ch <= 13; ch++) {
    counts[ch] = passes > 0 ? (uint32_t)counts[ch] * 100 / passes : 0;
  }

  // Sort channels descending by activity (simple insertion)
  uint8_t sorted[13] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
  for (uint8_t i = 0; i < 12; i++) {
    for (uint8_t j = i + 1; j < 13; j++) {
      if (counts[sorted[j]] > counts[sorted[i]]) {
        uint8_t t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t;
      }
    }
  }

  // Select channels: above threshold, or top N
  uint8_t selected[12];
  uint8_t num = 0;

  if (adaptive_threshold > 0) {
    for (uint8_t i = 0; i < 13 && num < 12; i++) {
      if (counts[sorted[i]] >= adaptive_threshold) {
        selected[num++] = sorted[i];
      }
    }
    if (num == 0) {
      Serial.println(F("No channels above threshold, switching to full spectrum"));
      current_mode = MODE_FULL_SPECTRUM;
      send_cmd_all(MODE_FULL_SPECTRUM, 0, CMD_START);
      jamming_active = true;
      if (adaptive_active) {
        last_adaptive_ms = millis();
        Serial.print(F("Adaptive jamming active (rescan every "));
        Serial.print(adaptive_interval_sec);
        Serial.println(F("s)"));
      }
      return;
    }
  } else {
    num = 12;
    for (uint8_t i = 0; i < 12; i++) selected[i] = sorted[i];
  }

  // Assign slaves
  memset(slave_cfg, 0, TOTAL_SLAVES);
  for (uint8_t i = 0; i < num; i++) CFG_SET(i, selected[i], true);

  current_mode = MODE_CUSTOM;

  // Print results
  if (adaptive_threshold > 0) {
    Serial.print(F("Targeting "));
    Serial.print(num);
    Serial.print(F(" channels >= "));
    Serial.print(adaptive_threshold);
    Serial.println(F("%"));
  } else {
    Serial.print(F("Targeting "));
    Serial.print(num);
    Serial.println(F(" busiest channels"));
  }
  Serial.print(F("Channels: "));
  for (uint8_t i = 0; i < num; i++) {
    if (i > 0) Serial.print(F(", "));
    Serial.print(selected[i]);
    Serial.print(F(" ("));
    Serial.print(counts[selected[i]]);
    Serial.print(F("%)"));
  }
  Serial.println();

  send_custom_cmds(CMD_START);
  jamming_active = true;

  if (adaptive_active) {
    last_adaptive_ms = millis();
    Serial.print(F("Adaptive jamming active (rescan every "));
    Serial.print(adaptive_interval_sec);
    Serial.println(F("s)"));
  } else {
    Serial.println(F("Adaptive jamming started (one-shot)"));
  }
}

// ── Sweep Mode ──────────────────────────────
static void advance_sweep(void) {
  sweep_channel++;
  if (sweep_channel > 13) sweep_channel = 1;
  selected_channel = sweep_channel;
  send_cmd_all(MODE_SWEEP, sweep_channel, CMD_START);
  Serial.print(F("Sweep ch"));
  Serial.println(sweep_channel);
}

static void start_sweep(void) {
  cancel_adaptive();
  sweep_active = true;
  current_mode = MODE_SWEEP;
  sweep_channel = 1;
  selected_channel = 1;
  last_sweep_ms = millis();
  send_cmd_all(MODE_SWEEP, 1, CMD_START);
  jamming_active = true;
  Serial.println(F("Sweep mode started"));
}

static void stop_sweep(void) {
  if (sweep_active) {
    sweep_active = false;
    jamming_active = false;
    send_cmd_all(MODE_SWEEP, 0, CMD_STOP);
    Serial.println(F("Sweep stopped"));
  }
}



/**
 * Print help menu
 */
static void print_help() {
  Serial.println(F("\n============== Commands =============="));
  Serial.println(F("help        - Show commands"));
  Serial.println(F("get         - Get slave config (get all, get 0,1,2)"));
  Serial.println(F("set         - Custom dist (set 4@1,2@6,2@11)"));
  Serial.println(F("channel     - Set channel (1-13) or 0=spectrum"));
  Serial.println(F("start       - Begin jamming"));
  Serial.println(F("stop        - Stop jamming"));
  Serial.println(F("status      - Show status & freq map"));
  Serial.println(F("snapshot    - RF snapshot 5s (default)"));
  Serial.println(F("snapshot 30 - RF snapshot 30s collection"));
  Serial.println(F("scan             - Scan all ch, show above threshold"));
  Serial.println(F("scan threshold N - Set scan threshold %"));
  Serial.println(F("scan 6           - Live scan ch 6, 10s (default)"));
  Serial.println(F("scan 6 30        - Live scan ch 6 for 30s"));
  Serial.println(F("power       - Show current power"));
  Serial.println(F("adaptive          - One-shot: scan & target busiest channels"));
  Serial.println(F("adaptive start    - Periodic adaptive jamming"));
  Serial.println(F("adaptive stop     - Stop adaptive jamming"));
  Serial.println(F("adaptive thresh N - Min activity % (0=auto)"));
  Serial.println(F("adaptive intv N   - Rescan interval (sec)"));
  Serial.println(F("profile list      - List saved profiles"));
  Serial.println(F("profile save <n>  - Save current config as profile"));
  Serial.println(F("profile load <n>  - Load a saved profile"));
  Serial.println(F("profile delete <n>- Delete a profile"));
  Serial.println(F("sweep             - Show sweep status"));
  Serial.println(F("sweep start       - Start sweep mode"));
  Serial.println(F("sweep stop        - Stop sweep mode"));
  Serial.println(F("sweep 500         - Set dwell (10-5000ms)"));
  Serial.println(F("pattern           - Show/set jamming pattern"));
  Serial.println(F("pattern continuous - Continuous (default)"));
  Serial.println(F("pattern pulsed 50 - Alternating on/off"));
  Serial.println(F("pattern random    - Random freq hop"));
  Serial.println(F("pattern burst 100 20 - Custom on/off"));
  Serial.println(F("======================================"));
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== MASTER CONTROLLER ==="));
  
  // Initialize hardware switch pins (active LOW with internal pull-up)
  pinMode(HW_SWITCH_PIN, INPUT_PULLUP);
  pinMode(HW_SWEEP_PIN, INPUT_PULLUP);
  
  // Check NRF24L01+ RX module
  Serial.print(F("NRF24L01+: "));
  if (nrf_check()) {
    Serial.println(F("OK"));
  } else {
    Serial.println(F("FAIL - check wiring"));
    Serial.println(F("  CE=D9, CSN=D10, MOSI=D11, MISO=D12, SCK=D13"));
    Serial.println(F("  VCC=3.3V (not 5V!), GND"));
  }
  
  // Check hardware switch state
  Serial.print(F("HW Switch: "));
  if (digitalRead(HW_SWITCH_PIN) == LOW) {
    Serial.println(F("ON - jamming enabled"));
  } else {
    Serial.println(F("OFF"));
  }
  
  Wire.begin(MASTER_ADDR);
  
  slave_count = scan_slaves();
  
  Serial.print(F("Found "));
  Serial.print(slave_count);
  Serial.println(F("/12 slaves"));
  
  print_help();
}

// ── Duplicate string helpers (B1 optimization) ────────────────────
static void print_hw_switch_on() {
    Serial.println(F("HW switch is ON - turn off to use software control"));
}
static void print_hw_sweep_on() {
    Serial.println(F("HW sweep is ON - turn switch off to use software control"));
}
static void print_use_5_5000() {
    Serial.println(F("Use 5-5000ms"));
}
static void print_usage_pattern_burst() {
    Serial.println(F("Usage: pattern burst <on_ms> <off_ms>"));
}

/**
 * Parse and execute command
 * Optimized: C-string parsing (A1), no String heap allocations, deduped F() literals (B1)
 */
void executeCommand(String &cmdLine) {
  cmdLine.trim();
  if (cmdLine.length() == 0) return;

  // Work on a mutable C-string copy (stack) — avoids String heap allocations
  char buf[48];
  cmdLine.toCharArray(buf, sizeof(buf));

  // Split into command and args at first space
  char *cmd = buf;
  char *sp = strchr(buf, ' ');
  char *args = NULL;
  if (sp) {
    *sp = '\0';
    args = sp + 1;
    // Skip leading spaces to match original substring(sp+1) behavior
    while (*args == ' ') args++;
    if (*args == '\0') args = NULL;
  }

  // Use strcmp_P to compare RAM-based cmd/args with flash-resident string literals
  if (strcmp_P(cmd, PSTR("help")) == 0) {
    print_help();

  } else if (strcmp_P(cmd, PSTR("get")) == 0) {
    if (!args || strcmp_P(args, PSTR("all")) == 0) {
      print_status();
      print_freq_map();
    } else {
      // Parse comma-separated slave IDs using strtok_r (reentrant)
      char *save;
      char *token = strtok_r(args, ",", &save);
      while (token) {
        uint8_t id = atoi(token);
        if (id < TOTAL_SLAVES) {
          Serial.print(F("S"));
          Serial.print(id);
          Serial.print(F(": "));
          if (current_mode == MODE_CUSTOM && !CFG_GET_ACTIVE(id)) {
            Serial.println(F("IDLE"));
          } else {
            Serial.print(F("ch"));
            Serial.println(CFG_GET_CHANNEL(id));
          }
        }
        token = strtok_r(NULL, ",", &save);
      }
    }

  } else if (strcmp_P(cmd, PSTR("set")) == 0) {
    if (hw_jamming_active) { print_hw_switch_on(); return; }
    if (hw_sweep_active) { print_hw_sweep_on(); return; }
    cancel_adaptive();
    // Parse distribution: "4@1,2@6,2@11"
    if (!args) return;
    uint8_t idx = 0;
    char *save;
    char *token = strtok_r(args, ",", &save);
    while (idx < TOTAL_SLAVES && token) {
      char *at = strchr(token, '@');
      if (at) {
        *at = '\0';
        uint8_t count = atoi(token);
        uint8_t ch = atoi(at + 1);

        if (ch < 1 || ch > 13) {
          Serial.print(F("Invalid ch: "));
          Serial.println(ch);
          return;
        }

        for (uint8_t i = 0; i < count && idx < TOTAL_SLAVES; i++) {
          CFG_SET(idx, ch, true);
          idx++;
        }
      }
      token = strtok_r(NULL, ",", &save);
    }

    // Mark remaining as idle
    for (uint8_t i = idx; i < TOTAL_SLAVES; i++) {
      CFG_SET(i, 0, false);
    }

    current_mode = MODE_CUSTOM;
    Serial.println(F("Custom dist set"));
    print_status();

  } else if (strcmp_P(cmd, PSTR("channel")) == 0) {
    cancel_adaptive();
    if (hw_jamming_active) { print_hw_switch_on(); return; }
    if (hw_sweep_active) { print_hw_sweep_on(); return; }
    uint8_t ch = args ? atoi(args) : 0;

    if (ch >= 1 && ch <= 13) {
      current_mode = MODE_SINGLE_CHANNEL;
      selected_channel = ch;
      for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
        CFG_SET(i, ch, true);
      }
      Serial.print(F("All -> ch"));
      Serial.println(ch);
      print_freq_map();
    } else if (ch == 0) {
      current_mode = MODE_FULL_SPECTRUM;
      Serial.println(F("Full spectrum mode"));
      print_freq_map();
    } else {
      Serial.println(F("Use 0-13"));
    }

  } else if (strcmp_P(cmd, PSTR("start")) == 0) {
    cancel_adaptive();
    if (hw_jamming_active) { print_hw_switch_on(); return; }
    if (hw_sweep_active) { print_hw_sweep_on(); return; }
    if (slave_count == 0) {
      Serial.println(F("No slaves"));
      return;
    }

    Serial.println(F("Starting..."));
    if (current_mode == MODE_CUSTOM) {
      send_custom_cmds(CMD_START);
    } else {
      send_cmd_all(current_mode, selected_channel, CMD_START);
    }
    jamming_active = true;
    print_freq_map();

  } else if (strcmp_P(cmd, PSTR("stop")) == 0) {
    cancel_adaptive();
    if (hw_jamming_active) { print_hw_switch_on(); return; }
    if (hw_sweep_active) { print_hw_sweep_on(); return; }
    if (!jamming_active) {
      Serial.println(F("Already stopped"));
      return;
    }

    Serial.println(F("Stopping..."));
    if (current_mode == MODE_CUSTOM) {
      send_custom_cmds(CMD_STOP);
    } else {
      send_cmd_all(current_mode, selected_channel, CMD_STOP);
    }
    jamming_active = false;

  } else if (strcmp_P(cmd, PSTR("status")) == 0) {
    print_status();
    print_freq_map();

  } else if (strcmp_P(cmd, PSTR("snapshot")) == 0) {
    uint8_t sec = 5;  // Default 5 seconds
    if (args) {
      sec = atoi(args);
      if (sec < 1) sec = 1;
      if (sec > 60) sec = 60;
    }
    cmd_snapshot(sec);

  } else if (strcmp_P(cmd, PSTR("scan")) == 0) {
    if (!args) {
      cmd_scan_threshold();
    } else if (strncmp_P(args, PSTR("threshold "), 10) == 0) {
      uint8_t t = atoi(args + 10);
      if (t > 100) t = 100;
      scan_threshold = t;
      Serial.print(F("Scan threshold set to "));
      Serial.print(scan_threshold);
      Serial.println(F("%"));
    } else if (strcmp_P(args, PSTR("threshold")) == 0) {
      Serial.print(F("Scan threshold: "));
      Serial.println(scan_threshold);
    } else {
      // scan <channel> [seconds]
      uint8_t ch = atoi(args);
      uint8_t sec = 10;
      char *sp2 = strchr(args, ' ');
      if (sp2) {
        sec = atoi(sp2 + 1);
      }
      if (ch < 1 || ch > 13) {
        Serial.println(F("Channel must be 1-13"));
        return;
      }
      if (sec < 1) sec = 1;
      if (sec > 60) sec = 60;
      cmd_scan_channel(ch, sec);
    }

  } else if (strcmp_P(cmd, PSTR("power")) == 0) {
    if (!args || args[0] == '\0') {
      Serial.print(F("Power: "));
      Serial.print(current_power + 1);
      Serial.print(F(" ("));
      if (current_power == 0) Serial.print(F("MIN"));
      else if (current_power == 1) Serial.print(F("LOW"));
      else if (current_power == 2) Serial.print(F("HIGH"));
      else Serial.print(F("MAX"));
      Serial.println(F(")"));
    } else {
      uint8_t p = atoi(args);
      if (p < 1 || p > 4) {
        Serial.println(F("Use 1-4 (1=MIN, 2=LOW, 3=HIGH, 4=MAX)"));
      } else {
        current_power = p - 1;
        Serial.print(F("Power set to "));
        Serial.println(p);
      }
    }

  } else if (strcmp_P(cmd, PSTR("adaptive")) == 0) {
    if (hw_jamming_active) {
      Serial.println(F("HW switch is ON - turn off to use adaptive"));
      return;
    }
    if (hw_sweep_active) {
      Serial.println(F("HW sweep is ON - turn switch off to use adaptive"));
      return;
    }
    if (!args || args[0] == '\0') {
      cancel_adaptive();
      cmd_adaptive();
    } else if (strcmp_P(args, PSTR("start")) == 0) {
      cancel_adaptive();
      adaptive_active = true;
      cmd_adaptive();
    } else if (strcmp_P(args, PSTR("stop")) == 0) {
      if (adaptive_active) {
        adaptive_active = false;
        if (jamming_active) {
          send_cmd_all(current_mode, selected_channel, CMD_STOP);
          jamming_active = false;
        }
        Serial.println(F("Adaptive jamming stopped"));
      } else {
        Serial.println(F("Adaptive mode not active"));
      }
    } else if (strncmp_P(args, PSTR("thresh"), 6) == 0) {
      char *sp2 = strchr(args, ' ');
      if (sp2) {
        uint8_t t = atoi(sp2 + 1);
        if (t > 100) t = 100;
        adaptive_threshold = t;
        Serial.print(F("Adaptive threshold set to "));
        Serial.print(adaptive_threshold);
        Serial.println(F("% (0=auto, pick top N)"));
      } else {
        Serial.print(F("Threshold: "));
        Serial.println(adaptive_threshold);
      }
    } else if (strncmp_P(args, PSTR("intv"), 4) == 0) {
      char *sp2 = strchr(args, ' ');
      if (sp2) {
        uint16_t s = atoi(sp2 + 1);
        if (s < 5) s = 5;
        if (s > 300) s = 300;
        adaptive_interval_sec = s;
        Serial.print(F("Rescan interval set to "));
        Serial.print(adaptive_interval_sec);
        Serial.println(F("s"));
      } else {
        Serial.print(F("Interval: "));
        Serial.print(adaptive_interval_sec);
        Serial.println(F("s"));
      }
    } else {
      Serial.println(F("adaptive [start|stop|thresh N|intv N]"));
    }

  } else if (strcmp_P(cmd, PSTR("sweep")) == 0) {
    if (!args || args[0] == '\0') {
      if (sweep_active || hw_sweep_active) {
        Serial.print(F("Sweep: "));
        if (hw_sweep_active) Serial.print(F("HW "));
        Serial.print(F("active, ch"));
        Serial.print((hw_sweep_active || sweep_active) ? sweep_channel : 1);
        Serial.print(F(", dwell "));
        Serial.print(sweep_dwell_ms);
        Serial.println(F("ms"));
      } else {
        Serial.println(F("Sweep inactive"));
      }
    } else if (strcmp_P(args, PSTR("stop")) == 0) {
      if (hw_sweep_active) { Serial.println(F("HW sweep is ON - turn switch off")); return; }
      stop_sweep();
    } else if (strcmp_P(args, PSTR("start")) == 0) {
      if (hw_sweep_active) { Serial.println(F("HW sweep is ON - turn switch off")); return; }
      if (hw_jamming_active) { Serial.println(F("HW switch is ON - turn off")); return; }
      cancel_adaptive();
      start_sweep();
    } else {
      uint16_t ms = atoi(args);
      if (ms >= 10 && ms <= 5000) {
        sweep_dwell_ms = ms;
        Serial.print(F("Sweep dwell set to "));
        Serial.print(ms);
        Serial.println(F("ms"));
      } else {
        Serial.println(F("Usage: sweep [start|stop|<dwell_ms>]"));
      }
    }

  } else if (strcmp_P(cmd, PSTR("pattern")) == 0) {
    if (!args || args[0] == '\0') {
      Serial.print(F("Pattern: "));
      if (current_pattern == 0) Serial.println(F("continuous"));
      else if (current_pattern == 1) { Serial.print(F("pulsed ")); Serial.print(pattern_on_ms); Serial.println(F("ms")); }
      else if (current_pattern == 2) Serial.println(F("random"));
      else if (current_pattern == 3) { Serial.print(F("burst ")); Serial.print(pattern_on_ms); Serial.print(F("/")); Serial.print(pattern_off_ms); Serial.println(F("ms")); }
    } else if (strcmp_P(args, PSTR("continuous")) == 0) {
      current_pattern = 0;
      Serial.println(F("Pattern: continuous"));
    } else if (strncmp_P(args, PSTR("pulsed"), 6) == 0) {
      char *sp2 = strchr(args, ' ');
      if (sp2) {
        uint16_t ms = atoi(sp2 + 1);
        if (ms >= 5 && ms <= 5000) {
          current_pattern = 1;
          pattern_on_ms = ms;
          pattern_off_ms = ms;
          pattern_state = true;
          pattern_timer = millis();
          Serial.print(F("Pattern: pulsed ")); Serial.print(ms); Serial.println(F("ms"));
        } else print_use_5_5000();
      } else Serial.println(F("Usage: pattern pulsed <ms>"));
    } else if (strcmp_P(args, PSTR("random")) == 0) {
      current_pattern = 2;
      Serial.println(F("Pattern: random"));
    } else if (strncmp_P(args, PSTR("burst"), 5) == 0) {
      char *sp2 = strchr(args, ' ');
      if (sp2) {
        char *sp3 = strchr(sp2 + 1, ' ');
        if (sp3) {
          uint16_t on = atoi(sp2 + 1);
          uint16_t off = atoi(sp3 + 1);
          if (on >= 5 && on <= 5000 && off >= 5 && off <= 5000) {
            current_pattern = 3;
            pattern_on_ms = on;
            pattern_off_ms = off;
            pattern_state = true;
            pattern_timer = millis();
            Serial.print(F("Pattern: burst ")); Serial.print(on); Serial.print(F("/")); Serial.print(off); Serial.println(F("ms"));
          } else print_use_5_5000();
        } else print_usage_pattern_burst();
      } else print_usage_pattern_burst();
    } else {
      Serial.println(F("Usage: pattern [continuous|pulsed <ms>|random|burst <on> <off>]"));
    }

  } else if (strcmp_P(cmd, PSTR("profile")) == 0) {
    // Trim leading spaces from args (or pass NULL if empty)
    if (args) {
      while (*args == ' ') args++;
      if (*args == '\0') args = NULL;
    }
    cmd_profile(args);

  } else {
    Serial.print(F("Unknown: "));
    Serial.println(cmd);
    Serial.println(F("Type 'help'"));
  }
}

/**
 * Main loop
 */
void loop() {
  // Check 3-position hardware switch state
  bool switch_pos1 = (digitalRead(HW_SWITCH_PIN) == LOW);  // D2 LOW = position 1
  bool switch_pos3 = (digitalRead(HW_SWEEP_PIN) == LOW);   // D3 LOW = position 3
  
  // --- Position 1: Full spectrum ---
  if (switch_pos1 && !hw_jamming_active) {
    cancel_adaptive();
    if (hw_sweep_active) {
      hw_sweep_active = false;
      sweep_active = false;
      send_cmd_all(MODE_SWEEP, 0, CMD_STOP);
    }
    hw_jamming_active = true;
    current_mode = MODE_FULL_SPECTRUM;
    selected_channel = 0;
    send_cmd_all(MODE_FULL_SPECTRUM, 0, CMD_START);
    jamming_active = true;
    Serial.println(F("\n[HW] Pos 1 - full spectrum jamming"));
  } else if (!switch_pos1 && hw_jamming_active) {
    hw_jamming_active = false;
    send_cmd_all(current_mode, selected_channel, CMD_STOP);
    jamming_active = false;
    Serial.println(F("\n[HW] Switch OFF"));
  }
  
  // --- Position 3: Sweep (only if position 1 is not active) ---
  if (!hw_jamming_active) {
    if (switch_pos3 && !hw_sweep_active) {
      cancel_adaptive();
      hw_sweep_active = true;
      current_mode = MODE_SWEEP;
      sweep_channel = 1;
      selected_channel = 1;
      last_sweep_ms = millis();
      send_cmd_all(MODE_SWEEP, 1, CMD_START);
      jamming_active = true;
      Serial.println(F("\n[HW] Pos 3 - sweep mode"));
    } else if (!switch_pos3 && hw_sweep_active) {
      hw_sweep_active = false;
      sweep_active = false;
      send_cmd_all(MODE_SWEEP, 0, CMD_STOP);
      jamming_active = false;
      Serial.println(F("\n[HW] Switch OFF"));
    }
  }
  
  // Sweep timer: advance channel periodically
  if ((sweep_active || hw_sweep_active) && jamming_active) {
    if (millis() - last_sweep_ms >= sweep_dwell_ms) {
      last_sweep_ms = millis();
      advance_sweep();
    }
  }
  
  // Pattern timing for pulsed/burst (master-driven START/STOP)
  if (jamming_active && !hw_jamming_active && !hw_sweep_active && !sweep_active && (current_pattern == 1 || current_pattern == 3)) {
    uint32_t now = millis();
    if (pattern_state && now - pattern_timer >= pattern_on_ms) {
      if (current_mode == MODE_CUSTOM) send_custom_cmds(CMD_STOP);
      else send_cmd_all(current_mode, selected_channel, CMD_STOP);
      pattern_state = false;
      pattern_timer = now;
    } else if (!pattern_state && now - pattern_timer >= pattern_off_ms) {
      if (current_mode == MODE_CUSTOM) send_custom_cmds(CMD_START);
      else send_cmd_all(current_mode, selected_channel, CMD_START);
      pattern_state = true;
      pattern_timer = now;
    }
  }
  
  if (Serial.available()) {
    String cmdLine = Serial.readStringUntil('\n');
    executeCommand(cmdLine);
    delay(10);  // Small delay after command processing (WARN-1 fix)
  }
  
  if (jamming_active && millis() - last_status_ms >= STATUS_INTERVAL_MS) {
    last_status_ms = millis();
    poll_slaves();
  }

  if (adaptive_active && jamming_active && millis() - last_adaptive_ms >= (uint32_t)adaptive_interval_sec * 1000) {
    cmd_adaptive();
  }
}


