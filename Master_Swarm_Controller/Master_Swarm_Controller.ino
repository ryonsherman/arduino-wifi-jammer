/**
 * Master Swarm Controller
 * 
 * Distributed Wi-Fi Jamming Master Node
 * - Handshakes with all 12 slave units via I2C
 * - Sends commands to spread jamming across single channels or full spectrum
 * - Reports status via USB Serial
 * 
 * Hardware: Arduino Nano + NRF24L01+ (RX mode for future spectrum analysis)
 * I2C Address: 0x70 (Master)
 */

#include <SPI.h>
#include <RF24.h>
#include <Wire.h>

// --- Configuration ---
#define MASTER_ADDR 0x70
#define TOTAL_SLAVES 12
#define SLAVE_ADDR_START 0x01
#define SLAVE_ADDR_END 0x0C

// --- NRF24L01+ Pinout (Arduino Nano) ---
#define CE_PIN 9
#define CSN_PIN 10

// --- Frequency Calculation ---
#define CHANNEL_WIDTH_22MHZ 22
#define CHANNEL_WIDTH_83MHZ 83
#define TOTAL_CHANNELS 12
#define BASE_FREQ_2400 2400
#define CHANNEL_1_BASE 2412  // Channel 1: 2412 MHz
#define CHANNEL_13_BASE 2472 // Channel 13: 2472 MHz
#define CHANNEL_SPACING 5    // Wi-Fi channels are 5 MHz apart
#define FULL_SPAN_MHZ 60     // 2412 to 2472 = 60 MHz span for channels 1-13

// --- I2C Packet Structure ---
#define CMD_START 1
#define CMD_STOP 0

// --- Modes ---
#define MODE_SINGLE_CHANNEL 1
#define MODE_FULL_SPECTRUM 2

// --- State ---
uint8_t slave_count = 0;
bool jamming_active = false;
uint8_t selected_channel = 0;
uint8_t current_mode = MODE_FULL_SPECTRUM;

// --- Distribution Modes ---
#define MODE_FULL_SPECTRUM 0
#define MODE_SINGLE_CHANNEL 1
#define MODE_CUSTOM 2

// --- Custom Distribution ---
typedef struct {
  uint8_t slave_id;
  uint8_t channel;  // 0=full spectrum, 1-13=Wi-Fi channel
  bool active;
} SlaveConfig;

SlaveConfig custom_config[TOTAL_SLAVES];

// --- NRF24 Instance ---
RF24 radio(CE_PIN, CSN_PIN);

/**
 * Send I2C command to a specific slave
 * 
 * @param slave_id: Slave index (0-11), will be converted to I2C address (0x01-0x0C)
 * @param mode: Mode constant (0=Full Spectrum, 1=Single Channel, 3=Custom)
 * @param channel: Wi-Fi channel (1-13) or 0 for full spectrum
 * @param cmd: CMD_START (1) to begin transmitting, CMD_STOP (0) to halt
 */
void send_command_to_slave(uint8_t slave_id, uint8_t mode, uint8_t channel, uint8_t cmd) {
  Wire.beginTransmission(SLAVE_ADDR_START + slave_id);
  Wire.write(mode);
  Wire.write(channel);
  Wire.write(cmd);
  Wire.write(0x00);  // Padding byte for packet alignment
  Wire.endTransmission();
}

/**
 * Send I2C command to slaves in range (sequential broadcast)
 * 
 * @param start: Starting slave index (0-based)
 * @param end: Ending slave index (0-based)
 * @param mode: Mode constant (0=Full Spectrum, 1=Single Channel, 3=Custom)
 * @param channel: Wi-Fi channel (1-13) or 0 for full spectrum
 * @param cmd: CMD_START (1) to begin transmitting, CMD_STOP (0) to halt
 */
void send_command_to_slaves_range(uint8_t start, uint8_t end, uint8_t mode, uint8_t channel, uint8_t cmd) {
  for (uint8_t i = start; i <= end && i < TOTAL_SLAVES; i++) {
    send_command_to_slave(i, mode, channel, cmd);
  }
}

/**
 * Send custom commands to each slave based on their config
 * 
 * Iterates through all configured slaves and sends their individual
 * channel assignments. Active slaves receive START/STOP commands,
 * while inactive slaves are skipped to save I2C bandwidth.
 * 
 * @param cmd: CMD_START (1) to begin transmitting, CMD_STOP (0) to halt
 */
void send_custom_commands(uint8_t cmd) {
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    if (custom_config[i].active) {
      // Mode 3 = Custom distribution mode
      send_command_to_slave(i, 3, custom_config[i].channel, cmd);
    }
  }
}

/**
 * Scan all slaves via I2C handshake
 * 
 * Tests I2C communication with each slave address (0x01-0x0C)
 * by attempting a transmission and checking for ACK.
 * 
 * @return: Number of responsive slaves (0-12)
 */
uint8_t scan_slaves() {
  uint8_t count = 0;
  
  Serial.println("\n=== Scanning Slaves ===");
  Serial.print("Scanning addresses 0x01 to 0x");
  Serial.println(SLAVE_ADDR_END, HEX);
  
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    Wire.beginTransmission(SLAVE_ADDR_START + i);
    uint8_t status = Wire.endTransmission();
    
    if (status == 0) {
      count++;
      Serial.print("[OK] Slave ");
      Serial.print(i);
      Serial.print(" (0x");
      Serial.print(SLAVE_ADDR_START + i, HEX);
      Serial.println(")");
    } else {
      Serial.print("[FAIL] Slave ");
      Serial.print(i);
      Serial.print(" (0x");
      Serial.print(SLAVE_ADDR_START + i, HEX);
      Serial.println(")");
    }
  }
  
  return count;
}

/**
 * Calculate frequency offset for a given slave ID and mode/channel
 * 
 * Frequency Strategy:
 * - Single Channel Mode: ALL 12 slaves transmit on exact channel frequency
 *   (e.g., all on 2437 MHz for channel 6) for maximum power density
 * - Full Spectrum Mode: 12 slaves spread across 60MHz at 5MHz spacing
 *   (2415, 2420, 2425...2470 MHz) covering channels 1-13
 * - Custom Mode: Each slave assigned to specific channel or full spectrum
 * 
 * @param slave_id: 0-based slave index (0-11)
 * @param mode: Mode constant (0=Full Spectrum, 1=Single Channel, 3=Custom)
 * @param channel: Wi-Fi channel (1-13) or 0 for full spectrum
 * @return: Frequency offset in MHz from 2400 MHz (2400-2527 MHz range)
 */
uint8_t calculate_freq_offset(uint8_t slave_id, uint8_t mode, uint8_t channel) {
  double target;
  
  if (mode == MODE_CUSTOM) {
    // Custom mode - jam exact channel frequency
    target = (channel == 1) ? CHANNEL_1_BASE :
             (channel == 6) ? 2437 :
             (channel == 11) ? 2462 :
             (channel == 13) ? CHANNEL_13_BASE : 2412;
    
    // Clamp to valid NRF24L01+ range (2400-2527 MHz)
    if (target < 2400) target = 2400;
    if (target > 2527) target = 2527;
    return (uint8_t)(target - BASE_FREQ_2400);
  }
  
  if (mode == MODE_SINGLE_CHANNEL) {
    // Single channel - ALL slaves on exact channel frequency
    target = (channel == 1) ? CHANNEL_1_BASE :
             (channel == 6) ? 2437 :
             (channel == 11) ? 2462 :
             (channel == 13) ? CHANNEL_13_BASE : 2412;
  } else {
    // Full spectrum - mid-channel spacing (between channels)
    // Spacing: 60MHz / 12 slaves = 5MHz between each slave
    // Coverage: 2415, 2420, 2425, 2430, 2435, 2440, 2445, 2450, 2455, 2460, 2465, 2470
    double step = FULL_SPAN_MHZ / (double)TOTAL_CHANNELS;
    target = CHANNEL_1_BASE + 3 + (slave_id * step);
  }
  
  // Clamp to valid NRF24L01+ range (2400-2527 MHz)
  if (target < 2400) target = 2400;
  if (target > 2527) target = 2527;
  
  return (uint8_t)(target - BASE_FREQ_2400);
}

/**
 * Print frequency distribution for all slaves
 * 
 * Displays the frequency target for each slave in the swarm.
 * Format varies based on mode:
 * - Custom: Shows ACTIVE/IDLE status with channel or "Full"
 * - Single/Full: Shows frequency offset and MHz value
 */
void print_frequency_map() {
  Serial.println("\n=== Channel Distribution ===");
  
  if (current_mode != MODE_CUSTOM) {
    Serial.print("Mode: ");
    Serial.println(current_mode == MODE_SINGLE_CHANNEL ? "Single Channel" : "Full Spectrum");
  }
  
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    uint8_t freq;
    String status;
    uint8_t slave_num = i + 1;
    
    if (current_mode == MODE_CUSTOM) {
      freq = calculate_custom_freq(custom_config[i].channel);
      status = custom_config[i].active ? "ACTIVE" : "IDLE";
      Serial.print("Slave ");
      Serial.print(slave_num);
      Serial.print(" [");
      Serial.print(status);
      Serial.print("] Channel: ");
      Serial.print(custom_config[i].channel == 0 ? "Full" : String(custom_config[i].channel));
      Serial.print(" (");
      Serial.print(2400 + freq);
      Serial.println(" MHz)");
    } else {
      freq = calculate_freq_offset(i, current_mode, selected_channel);
      Serial.print("Slave ");
      Serial.print(slave_num);
      Serial.print(" -> ");
      Serial.print(current_mode == MODE_SINGLE_CHANNEL ? "Channel " : "Freq ");
      Serial.print(freq);
      Serial.print(" (");
      Serial.print(2400 + freq);
      Serial.println(" MHz)");
    }
  }
}

/**
 * Calculate frequency for custom mode channel value
 * @param channel: Wi-Fi channel (1-13) or 0 for full spectrum
 * @return: Frequency offset in MHz from 2400 MHz
 */
uint8_t calculate_custom_freq(uint8_t channel) {
  // Reuse same logic as single channel for consistency
  return calculate_freq_offset(0, MODE_SINGLE_CHANNEL, channel);
}

/**
 * Print custom distribution configuration summary
 * 
 * Displays active slave count and distribution across key channels:
 * - Counts active vs idle slaves
 * - Breaks down by Wi-Fi channels (1, 6, 11) and full spectrum mode
 * - Shows "Channel: All" for full spectrum mode
 */
void print_custom_distribution() {
  Serial.println("\n=== Slave Status ===");
  
  if (current_mode == MODE_CUSTOM) {
    // Count slaves per channel for summary
    uint8_t ch1_count = 0, ch6_count = 0, ch11_count = 0, idle_count = 0;
    
    for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
      if (!custom_config[i].active) {
        idle_count++;
      } else if (custom_config[i].channel == 1) {
        ch1_count++;
      } else if (custom_config[i].channel == 6) {
        ch6_count++;
      } else if (custom_config[i].channel == 11) {
        ch11_count++;
      }
    }
    
    Serial.print("Active: ");
    Serial.print(TOTAL_SLAVES - idle_count);
    Serial.print("/12");
    Serial.println();
    Serial.print("Channel 1: ");
    Serial.print(ch1_count);
    Serial.println();
    Serial.print("Channel 6: ");
    Serial.print(ch6_count);
    Serial.println();
    Serial.print("Channel 11: ");
    Serial.println(ch11_count);
  } else {
    Serial.print("Active: ");
    Serial.print(TOTAL_SLAVES);
    Serial.print("/12");
    Serial.println();
    if (current_mode == MODE_SINGLE_CHANNEL) {
      Serial.print("Channel: ");
      Serial.println(selected_channel);
    } else {
      Serial.println("Channel: All");
    }
  }
}

/**
 * Setup - runs once at boot
 * 
 * Initializes:
 * - USB Serial for user commands (115200 baud)
 * - I2C as master at 0x70
 * - NRF24L01+ in RX mode (future spectrum analyzer)
 * - Scans for slave nodes
 */
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== MASTER SWARM CONTROLLER ==="));
  
  // I2C setup - Master at address 0x70
  Wire.begin(MASTER_ADDR);
  
  // NRF24L01+ setup for RX mode (future spectrum analysis)
  radio.begin();
  radio.setPALevel(RF24_PA_MIN);     // Low power for receiver
  radio.setDataRate(RF24_2MBPS);     // 2Mbps data rate
  radio.setChannel(0);                // Base channel (adjustable)
  radio.openReadingPipe(0, 0x01020304);
  radio.startListening();             // Enable RX mode
  
  Serial.print("[MASTER] I2C Address: 0x");
  Serial.println(MASTER_ADDR, HEX);
  Serial.println("Scanning for slaves via I2C...");
  
  // Scan and report slave count
  slave_count = scan_slaves();
  
  Serial.print("[MASTER] Found ");
  Serial.print(slave_count);
  Serial.print(" of ");
  Serial.print(TOTAL_SLAVES);
  Serial.println(" slaves.");
  
  if (slave_count == TOTAL_SLAVES) {
    Serial.println("[MASTER] All slaves initialized! Ready for commands.");
  } else {
    Serial.print("[MASTER] Warning: Only ");
    Serial.print(slave_count);
    Serial.println(" slaves found. Check wiring and power.");
  }
  
  Serial.println(F("\n=== Commands ==="));
  Serial.println(F("help    → Show this command list"));
  Serial.println(F("get     → Get config of slaves (get 0,1,2 or get all)"));
  Serial.println(F("set     → Set custom distribution (set 4@1,2@6,2@11)"));
  Serial.println(F("channel → Set all slaves to 1 channel (channel 1-13) or 0 for full spectrum"));
  Serial.println(F("jam     → Full spectrum jamming (all slaves spread)"));
  Serial.println(F("start   → Begin transmitting with current config"));
  Serial.println(F("stop    → Stop transmitting (keep config)"));
  Serial.println(F("map     → Show frequency map"));
Serial.println(F("status  → Show distribution station"));
}

/**
 * Parse command line and execute
 * 
 * Command syntax:
 * - Command name: First word (case-sensitive)
 * - Arguments: Everything after first space
 * 
 * Supported commands:
 * - help: Display command list
 * - get [ids]: Query slave configurations
 * - set <distribution>: Set custom distribution (e.g., "4@1,2@6,2@11")
 * - channel <n>: Set single channel or "0" for full spectrum
 * - start/stop: Begin/ halt transmission
 * - status: Display distribution details (status + map combined)
 */
void executeCommand(String cmdLine) {
  cmdLine.trim();
  
  // Extract command name (first word) and arguments
  int spacePos = cmdLine.indexOf(' ');
  String cmd = (spacePos != -1) ? cmdLine.substring(0, spacePos) : cmdLine;
  String args = (spacePos != -1) ? cmdLine.substring(spacePos + 1) : "";
  
  Serial.print("[MASTER] ");
  
  if (cmd == "help") {
    Serial.println(F("\n=== Wi-Fi Jammer Master Commands ==="));
    Serial.println(F("help    → Show this command list"));
    Serial.println(F("get     → Get config of slaves (get 0,1,2 or get all)"));
    Serial.println(F("set     → Set custom distribution (set 4@1,2@6,2@11)"));
    Serial.println(F("channel → Set all slaves to 1 channel (channel 1-13) or 0 for full spectrum"));
    Serial.println(F("start   → Begin transmitting with current config"));
    Serial.println(F("stop    → Stop transmitting (keep config)"));
    Serial.println(F("status  → Show distribution station"));
   
    
  } else if (cmd == "get") {
    if (args == "all" || args == "") {
      Serial.println("All slave configurations:");
      print_custom_distribution();
      print_frequency_map();
    } else {
      Serial.println("Getting slaves: " + args);
      // Parse comma-separated list
      int pos = 0;
      while (pos < args.length()) {
        int commaPos = args.indexOf(',', pos);
        String slaveStr = (commaPos != -1) ? args.substring(pos, commaPos) : args.substring(pos);
        uint8_t slaveId = slaveStr.toInt();
        
        if (slaveId < TOTAL_SLAVES) {
          Serial.print("Slave ");
          Serial.print(slaveId);
          Serial.print(": ");
          if (custom_config[slaveId].active) {
            Serial.print("ACTIVE on ");
            Serial.print(custom_config[slaveId].channel == 0 ? "full" : String(custom_config[slaveId].channel));
          } else {
            Serial.println("IDLE");
          }
        }
        
        pos = (commaPos != -1) ? commaPos + 1 : args.length();
      }
    }
    
  } else if (cmd == "set") {
    Serial.println("Setting custom distribution: " + args);
    
    // Parse distribution string (format: n@ch1,n@ch2,...)
    // Examples: "4@1,2@6,2@11,4@0" or "6@1,6@6"
    int pos = 0;
    uint8_t slave_idx = 0;
    
    while (slave_idx < TOTAL_SLAVES && pos < args.length()) {
      int commaPos = args.indexOf(',', pos);
      String segment = (commaPos != -1) ? 
                      args.substring(pos, commaPos) : 
                      args.substring(pos);
      
      int atPos = segment.indexOf('@');
      if (atPos != -1) {
        uint8_t count = segment.substring(0, atPos).toInt();
        uint8_t channel = segment.substring(atPos + 1).toInt();
        
        for (uint8_t i = 0; i < count && slave_idx < TOTAL_SLAVES; i++) {
          custom_config[slave_idx].slave_id = slave_idx;
          custom_config[slave_idx].channel = channel;
          custom_config[slave_idx].active = true;
          slave_idx++;
        }
      }
      
      pos = (commaPos != -1) ? commaPos + 1 : args.length();
    }
    
    // Mark remaining slaves as idle (not specified in command)
    for (uint8_t i = slave_idx; i < TOTAL_SLAVES; i++) {
      custom_config[i].active = false;
    }
    
    current_mode = MODE_CUSTOM;
    Serial.println("[MASTER] Distribution set:");
    print_custom_distribution();
    print_frequency_map();
    
    Serial.println("Note: Slaves stopped. Run 'start' to begin transmitting.");
    
   } else if (cmd == "channel") {
    uint8_t channel = args.toInt();
    
    if (channel >= 1 && channel <= 13) {
      // Single channel mode - all slaves on exact channel frequency
      current_mode = MODE_SINGLE_CHANNEL;
      selected_channel = channel;
      // Set all 12 slaves to this channel
      for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
        custom_config[i].channel = channel;
        custom_config[i].active = true;
      }
      Serial.println("All 12 slaves → Channel " + String(channel));
      print_frequency_map();
      Serial.println("Note: Slaves stopped. Run 'start' to begin transmitting.");
    } else if (channel == 0) {
      // Full spectrum mode - slaves spread across 60MHz span
      current_mode = MODE_FULL_SPECTRUM;
      print_frequency_map();
      Serial.println("Full spectrum mode enabled. Run 'start' to begin transmitting.");
    } else {
      Serial.println(F("Invalid channel. Use 0 for full spectrum or 1-13 for Wi-Fi channel."));
    }
    
  } else if (cmd == "start") {
    if (slave_count > 0) {
      Serial.println("Starting jamming...");
      
      if (current_mode == MODE_CUSTOM) {
        send_custom_commands(CMD_START);
      } else {
        send_command_to_slaves_range(0, TOTAL_SLAVES - 1, current_mode, selected_channel, CMD_START);
      }
      
      jamming_active = true;
      print_frequency_map();
    } else {
      Serial.println("No slaves to command.");
    }
    
  } else if (cmd == "stop") {
    if (jamming_active) {
      Serial.println("Stopping jamming (config retained)...");
      
      if (current_mode == MODE_CUSTOM) {
        send_custom_commands(CMD_STOP);
      } else {
        send_command_to_slaves_range(0, TOTAL_SLAVES - 1, current_mode, selected_channel, CMD_STOP);
      }
      
      jamming_active = false;
    } else {
      Serial.println("Already stopped.");
    }
    
} else if (cmd == "status") {
    print_custom_distribution();
    print_frequency_map();
    
    
  } else {
    Serial.println("Unknown command: '" + cmd + "'");
    Serial.println("Enter 'help' to see available commands.");
  }
}

/**
 * Main loop - listens for serial commands
 * 
 * Reads commands from USB Serial line and executes them via executeCommand().
 * Commands are terminated by newline character.
 */
void loop() {
  if (Serial.available()) {
    String cmdLine = Serial.readStringUntil('\n');
    executeCommand(cmdLine);
  }
  
  delay(100);  // Small delay to prevent CPU spin
}


