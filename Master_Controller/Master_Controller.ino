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

// --- Configuration ---
#define MASTER_ADDR 0x70
#define TOTAL_SLAVES 12

// --- Hardware Switch ---
#define HW_SWITCH_PIN 2  // D2 - connect switch between D2 and GND

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

// Packed slave config: 4 bytes per slave instead of 4 (struct padding eliminated)
// Layout: [channel:4bits][active:1bit][unused:3bits] = 1 byte per slave
static uint8_t slave_cfg[TOTAL_SLAVES];  // Was 48 bytes (struct), now 12 bytes

// Bounds-checked config access macros (WARN-2 fix)
#define CFG_GET_CHANNEL(i)  ((i) < TOTAL_SLAVES ? (slave_cfg[i] >> 4) : 0)
#define CFG_GET_ACTIVE(i)   ((i) < TOTAL_SLAVES ? (slave_cfg[i] & 0x01) : 0)
#define CFG_SET(i, ch, act) do { if ((i) < TOTAL_SLAVES) slave_cfg[i] = (((ch) & 0x0F) << 4) | ((act) ? 1 : 0); } while(0)

/**
 * Send I2C command to a specific slave
 * Byte 4 packs local_idx (high nibble) + group_size (low nibble) for fan-out
 */
static void send_cmd(uint8_t slave_id, uint8_t mode, uint8_t channel, uint8_t cmd, uint8_t local_idx, uint8_t group_size) {
  Wire.beginTransmission(SLAVE_ADDR_START + slave_id);
  Wire.write(mode);
  Wire.write(channel);
  Wire.write(cmd);
  Wire.write(((local_idx & 0x0F) << 4) | (group_size & 0x0F));
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
  if (mode == MODE_SINGLE_CHANNEL || mode == MODE_CUSTOM) {
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
  } else {
    Serial.println(F("Mode: Custom"));
  }
  
  if (current_mode == MODE_CUSTOM) {
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
  } else {
    // Custom - list unique channels
    Serial.print(F("Channels: "));
    uint16_t seen = 0;  // Bitmask for channels 1-13
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
}

/**
 * Poll all slaves and print status
 */
static void poll_slaves() {
  uint8_t active = 0;
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    Wire.requestFrom((uint8_t)(SLAVE_ADDR_START + i), (uint8_t)1);
    if (Wire.available() && Wire.read()) active++;
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
  Serial.println(F("scan 6      - Live scan ch 6, 10s (default)"));
  Serial.println(F("scan 6 30   - Live scan ch 6 for 30s"));
  Serial.println(F("======================================"));
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== MASTER CONTROLLER ==="));
  
  // Initialize hardware switch pin (active LOW with internal pull-up)
  pinMode(HW_SWITCH_PIN, INPUT_PULLUP);
  
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

/**
 * Parse and execute command
 * Optimized: avoid String concatenation, use F() macros
 */
void executeCommand(String &cmdLine) {
  cmdLine.trim();
  if (cmdLine.length() == 0) return;
  
  // Extract command (first word)
  int sp = cmdLine.indexOf(' ');
  String cmd = (sp != -1) ? cmdLine.substring(0, sp) : cmdLine;
  
  if (cmd == F("help")) {
    print_help();
    
  } else if (cmd == F("get")) {
    String args = (sp != -1) ? cmdLine.substring(sp + 1) : "";
    if (args == F("all") || args.length() == 0) {
      print_status();
      print_freq_map();
    } else {
      // Parse comma-separated slave IDs
      int pos = 0;
      while (pos < (int)args.length()) {
        int comma = args.indexOf(',', pos);
        String s = (comma != -1) ? args.substring(pos, comma) : args.substring(pos);
        uint8_t id = s.toInt();
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
        pos = (comma != -1) ? comma + 1 : args.length();
      }
    }
    
  } else if (cmd == F("set")) {
    if (hw_jamming_active) {
      Serial.println(F("HW switch is ON - turn off to use software control"));
      return;
    }
    // Parse distribution: "4@1,2@6,2@11"
    String args = (sp != -1) ? cmdLine.substring(sp + 1) : "";
    uint8_t idx = 0;
    int pos = 0;
    
    while (idx < TOTAL_SLAVES && pos < (int)args.length()) {
      int comma = args.indexOf(',', pos);
      String seg = (comma != -1) ? args.substring(pos, comma) : args.substring(pos);
      int at = seg.indexOf('@');
      
      if (at != -1) {
        uint8_t count = seg.substring(0, at).toInt();
        uint8_t ch = seg.substring(at + 1).toInt();
        
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
      pos = (comma != -1) ? comma + 1 : args.length();
    }
    
    // Mark remaining as idle
    for (uint8_t i = idx; i < TOTAL_SLAVES; i++) {
      CFG_SET(i, 0, false);
    }
    
    current_mode = MODE_CUSTOM;
    Serial.println(F("Custom dist set"));
    print_status();
    
  } else if (cmd == F("channel")) {
    if (hw_jamming_active) {
      Serial.println(F("HW switch is ON - turn off to use software control"));
      return;
    }
    String args = (sp != -1) ? cmdLine.substring(sp + 1) : "";
    uint8_t ch = args.toInt();
    
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
    
  } else if (cmd == F("start")) {
    if (hw_jamming_active) {
      Serial.println(F("HW switch is ON - turn off to use software control"));
      return;
    }
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
    
  } else if (cmd == F("stop")) {
    if (hw_jamming_active) {
      Serial.println(F("HW switch is ON - turn off to use software control"));
      return;
    }
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
    
  } else if (cmd == F("status")) {
    print_status();
    print_freq_map();
    
  } else if (cmd == F("snapshot")) {
    String args = (sp != -1) ? cmdLine.substring(sp + 1) : "";
    uint8_t sec = 5;  // Default 5 seconds
    if (args.length() > 0) {
      sec = args.toInt();
      if (sec < 1) sec = 1;
      if (sec > 60) sec = 60;
    }
    cmd_snapshot(sec);
    
  } else if (cmd == F("scan")) {
    // scan <channel> [seconds]
    String args = (sp != -1) ? cmdLine.substring(sp + 1) : "";
    args.trim();
    
    if (args.length() == 0) {
      Serial.println(F("Usage: scan <channel> [seconds]"));
      Serial.println(F("  channel: 1-13"));
      Serial.println(F("  seconds: 1-60 (default 10)"));
      return;
    }
    
    // Parse channel and optional seconds
    int sp2 = args.indexOf(' ');
    uint8_t ch = args.toInt();
    uint8_t sec = 10;  // Default
    
    if (sp2 != -1) {
      sec = args.substring(sp2 + 1).toInt();
    }
    
    if (ch < 1 || ch > 13) {
      Serial.println(F("Channel must be 1-13"));
      return;
    }
    if (sec < 1) sec = 1;
    if (sec > 60) sec = 60;
    
    cmd_scan_channel(ch, sec);
    
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
  // Check hardware switch state
  bool switch_on = (digitalRead(HW_SWITCH_PIN) == LOW);
  
  if (switch_on && !hw_jamming_active) {
    // Switch just turned ON - start full spectrum jamming
    hw_jamming_active = true;
    current_mode = MODE_FULL_SPECTRUM;
    selected_channel = 0;
    send_cmd_all(MODE_FULL_SPECTRUM, 0, CMD_START);
    jamming_active = true;
    Serial.println(F("\n[HW] Switch ON - full spectrum jamming started"));
  } else if (!switch_on && hw_jamming_active) {
    // Switch just turned OFF - stop jamming
    hw_jamming_active = false;
    send_cmd_all(current_mode, selected_channel, CMD_STOP);
    jamming_active = false;
    Serial.println(F("\n[HW] Switch OFF - jamming stopped"));
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
}


