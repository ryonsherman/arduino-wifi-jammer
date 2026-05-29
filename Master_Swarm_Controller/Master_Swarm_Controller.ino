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
 */
void send_command_to_slave(uint8_t slave_id, uint8_t mode, uint8_t channel, uint8_t cmd) {
  Wire.beginTransmission(SLAVE_ADDR_START + slave_id);
  Wire.write(mode);
  Wire.write(channel);
  Wire.write(cmd);
  Wire.write(0x00);
  Wire.endTransmission();
}


}

/**
 * Send I2C command to slaves in range
 */
void send_command_to_slaves_range(uint8_t start, uint8_t end, uint8_t mode, uint8_t channel, uint8_t cmd) {
  for (uint8_t i = start; i <= end && i < TOTAL_SLAVES; i++) {
    send_command_to_slave(i, mode, channel, cmd);
  }
}

/**
 * Send custom commands to each slave based on their config
 * Mode=0 in slave means "exact channel frequency"
 */
void send_custom_commands(uint8_t cmd) {
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    if (custom_config[i].active) {
      send_command_to_slave(i, 3, custom_config[i].channel, cmd);
    }
  }
}

/**
 * Scan all slaves via I2C handshake
 * @return: Number of responsive slaves
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
 * Strategy:
 * - Single Channel: ALL slaves on exact channel frequency (no spreading)
 * - Full Spectrum: mid-channel spacing (between channels) for maximum coverage
 * - Custom: exact channel frequency per slave
 */
uint8_t calculate_freq_offset(uint8_t slave_id, uint8_t mode, uint8_t channel) {
  double target;
  
  if (mode == MODE_CUSTOM) {
    // Custom mode - jam exact channel frequency
    target = (channel == 1) ? CHANNEL_1_BASE :
             (channel == 6) ? 2437 :
             (channel == 11) ? 2462 :
             (channel == 13) ? CHANNEL_13_BASE : 2412;
    
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
    double step = FULL_SPAN_MHZ / (double)TOTAL_CHANNELS;
    // Slaves hit: 2415, 2420, 2425, 2430, 2435, 2440, 2445, 2450, 2455, 2460, 2465, 2470
    target = CHANNEL_1_BASE + 3 + (slave_id * step);
  }
  
  if (target < 2400) target = 2400;
  if (target > 2527) target = 2527;
  
  return (uint8_t)(target - BASE_FREQ_2400);
}

/**
 * Print frequency distribution for all slaves
 */
void print_frequency_map() {
  Serial.println("\n=== Frequency Distribution ===");
  if (current_mode == MODE_CUSTOM) {
    Serial.println("Mode: Custom Distribution");
  } else {
    Serial.print("Mode: ");
    Serial.println(current_mode == MODE_SINGLE_CHANNEL ? "Single Channel" : "Full Spectrum");
    Serial.print("Channel: ");
    Serial.println(selected_channel > 0 ? String(selected_channel) : "Full Spectrum");
  }
  
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    uint8_t freq;
    String status;
    
    if (current_mode == MODE_CUSTOM) {
      freq = calculate_custom_freq(custom_config[i].channel);
      status = custom_config[i].active ? "ACTIVE" : "IDLE";
      Serial.print("Slave ");
      Serial.print(i);
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
      Serial.print(i);
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
 * Print custom distribution configuration
 */
void print_custom_distribution() {
  Serial.println("\n=== Custom Distribution ===");
  
  // Count slaves per channel
  uint8_t ch1_count = 0, ch6_count = 0, ch11_count = 0, full_count = 0, idle_count = 0;
  
  for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
    if (!custom_config[i].active) {
      idle_count++;
    } else if (custom_config[i].channel == 0) {
      full_count++;
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
  Serial.print("/12 | Idle: ");
  Serial.print(idle_count);
  Serial.print(" | Channel 1: ");
  Serial.print(ch1_count);
  Serial.print(" | Channel 6: ");
  Serial.print(ch6_count);
  Serial.print(" | Channel 11: ");
  Serial.print(ch11_count);
  Serial.print(" | Full: ");
  Serial.println(full_count);
}

/**
 * Setup - runs once at boot
 */
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== MASTER SWARM CONTROLLER ===");
  
  // I2C setup
  Wire.begin(MASTER_ADDR);
  
  // NRF24 setup (RX mode for future spectrum analysis)
  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_2MBPS);
  radio.setChannel(0);
  radio.openReadingPipe(0, 0x01020304);
  radio.startListening();
  
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
  
   Serial.println("\n=== Commands ===");
  Serial.println("help    → Show this command list");
  Serial.println("get     → Get config of slaves (get 0,1,2 or get all)");
  Serial.println("set     → Set custom distribution (set 4@1,2@6,2@11)");
  Serial.println("channel → Set all slaves to 1 channel (channel 6)");
  Serial.println("jam     → Full spectrum jamming (all slaves spread)");
  Serial.println("start   → Begin transmitting with current config");
  Serial.println("stop    → Stop transmitting (keep config)");
  Serial.println("map     → Show frequency map");
  Serial.println("status  → Show distribution station");
}

/**
 * Parse command line and execute
 */
void executeCommand(String cmdLine) {
  cmdLine.trim();
  
  // Extract command name (first word)
  int spacePos = cmdLine.indexOf(' ');
  String cmd = (spacePos != -1) ? cmdLine.substring(0, spacePos) : cmdLine;
  String args = (spacePos != -1) ? cmdLine.substring(spacePos + 1) : "";
  
  Serial.print("[MASTER] ");
  
  if (cmd == "help") {
    Serial.println("\n=== Wi-Fi Jammer Master Commands ===");
    Serial.println("help    → Show this command list");
    Serial.println("get     → Get config of slaves (get 0,1,2 or get all)");
    Serial.println("set     → Set custom distribution (set 4@1,2@6,2@11)");
    Serial.println("channel → Set all slaves to 1 channel (channel 6)");
    Serial.println("jam     → Full spectrum jamming (all slaves spread)");
    Serial.println("start   → Begin transmitting with current config");
    Serial.println("stop    → Stop transmitting (keep config)");
    Serial.println("map     → Show frequency map");
    Serial.println("status → Show distribution station");
   
    
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
    
    // Parse distribution string
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
    
    // Mark remaining slaves as idle
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
    } else {
      Serial.println("Invalid channel. Use 1-13.");
    }
    
  } else if (cmd == "jam") {
    Serial.println("Full spectrum jamming:");
    current_mode = MODE_FULL_SPECTRUM;
    selected_channel = 0;
    // Set all 12 slaves to full spectrum (channel 0)
    for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
      custom_config[i].channel = 0;
      custom_config[i].active = true;
    }
    Serial.println("All 12 slaves → Full Spectrum (mid-channel spacing)");
    print_frequency_map();
    Serial.println("Note: Slaves stopped. Run 'start' to begin transmitting.");
    
  } else if (cmd == "start") {
    if (slave_count > 0) {
      Serial.println("Starting jamming...");
      
      if (current_mode == MODE_CUSTOM) {
        send_custom_commands(CMD_START);
      } else {
        send_command_to_slaves(current_mode, selected_channel, CMD_START);
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
        send_command_to_slaves(current_mode, selected_channel, CMD_STOP);
      }
      
      jamming_active = false;
    } else {
      Serial.println("Already stopped.");
    }
    
  } else if (cmd == "map") {
    print_frequency_map();
    
  } else if (cmd == "status") {
    print_custom_distribution();
    
  } else if (cmd == "status") {
    print_custom_distribution();

    
  } else {
    Serial.println("Unknown command: '" + cmd + "'");
    Serial.println("Enter 'help' to see available commands.");
  }
}

/**
 * Main loop - listens for serial commands
 */
void loop() {
  if (Serial.available()) {
    String cmdLine = Serial.readStringUntil('\n');
    executeCommand(cmdLine);
  }
  
  delay(100);
}
          Serial.print("[MASTER] All 12 slaves → Channel ");
          Serial.println(channel);
          print_frequency_map();
        } else {
          Serial.println("[MASTER] Invalid channel. Use 1-13.");
        }
        break;
        
      case 'f': // Full spectrum - all 12 slaves spread across 1-13 channels
        Serial.println("\n=== Full Spectrum Mode ===");
        current_mode = MODE_FULL_SPECTRUM;
        selected_channel = 0;
        // Set all 12 slaves to full spectrum (channel 0)
        for (uint8_t i = 0; i < TOTAL_SLAVES; i++) {
          custom_config[i].channel = 0;
          custom_config[i].active = true;
        }
        Serial.println("[MASTER] All 12 slaves → Full Spectrum (mid-channel spacing)");
        print_frequency_map();
        break;
        
      case 'n': // Custom distribution via n@ch format
        Serial.println("\n=== Custom Distribution ===");
        Serial.println("Enter: n@ch1,n@ch2,... (e.g., 4@1,2@6,2@11,4@0)");
        Serial.println("Where ch = 1-13 (Wi-Fi channel) or 0 (full spectrum)");
        Serial.println("Unspecified slaves become idle");
        Serial.println("Examples:");
        Serial.println("  4@1,2@6,2@11,4@0 → 4 on ch1, 2 on ch6, 2 on ch11, 4 full");
        Serial.println("  6@1,6@6          → 6 on ch1, 6 on ch6 (rest idle)");
        Serial.println("  12@6             → all 12 on ch6");
        Serial.println("  4@1,2@6          → 4 on ch1, 2 on ch6 (8 idle)");
        
        while (!Serial.available()) delay(10);
        String input = Serial.readStringUntil('\n').trim();
        
        // Parse distribution string
        int pos = 0;
        uint8_t slave_idx = 0;
        
        while (slave_idx < TOTAL_SLAVES && pos < input.length()) {
          int commaPos = input.indexOf(',', pos);
          String segment = (commaPos != -1) ? 
                          input.substring(pos, commaPos) : 
                          input.substring(pos);
          
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
          
          pos = (commaPos != -1) ? commaPos + 1 : input.length();
        }
        
        // Mark remaining slaves as idle
        for (uint8_t i = slave_idx; i < TOTAL_SLAVES; i++) {
          custom_config[i].active = false;
        }
        
        current_mode = MODE_CUSTOM;
        Serial.println("[MASTER] Distribution set:");
        print_custom_distribution();
        print_frequency_map();
        break;
        
        while (!Serial.available()) delay(10);
        String input = Serial.readStringUntil('\n').trim();
        
        // Parse distribution string
        int start = 0;
        int pos = 0;
        uint8_t slave_idx = 0;
        int idx = 0;
        
        while ((pos = input.indexOf('@', start)) != -1 && slave_idx < TOTAL_SLAVES) {
          String segment = input.substring(start, pos);
          
          // Find the comma
          int commaPos = segment.indexOf(',');
          String countStr = (commaPos != -1) ? segment.substring(0, commaPos) : segment;
          
          int count = countStr.toInt();
          int channel = segment.substring(segment.indexOf('@') + 1).toInt();
          
          // Assign slaves
          for (int i = 0; i < count && slave_idx < TOTAL_SLAVES; i++) {
            custom_config[slave_idx].slave_id = slave_idx;
            custom_config[slave_idx].channel = channel;
            custom_config[slave_idx].active = true;
            slave_idx++;
          }
          
          start = (commaPos != -1) ? pos + 1 : pos + 1;
        }
        
        // Mark remaining slaves as idle
        for (uint8_t i = slave_idx; i < TOTAL_SLAVES; i++) {
          custom_config[i].active = false;
        }
        
        current_mode = MODE_CUSTOM;
        Serial.println("[MASTER] Custom distribution set:");
        print_custom_distribution();
        print_frequency_map();
        break;
        
      case 's': // Start jamming with current config
        if (slave_count > 0) {
          Serial.println("\n[MASTER] Starting jamming...");
          
          if (current_mode == MODE_CUSTOM) {
            send_custom_commands(CMD_START);
          } else {
            send_command_to_slaves(current_mode, selected_channel, CMD_START);
          }
          
          jamming_active = true;
          print_frequency_map();
        } else {
          Serial.println("[MASTER] No slaves to command.");
        }
        break;
        
      case 'p': // Stop jamming (keep config)
        if (jamming_active) {
          Serial.println("\n[MASTER] Stopping jamming (config retained)...");
          
          if (current_mode == MODE_CUSTOM) {
            send_custom_commands(CMD_STOP);
          } else {
            send_command_to_slaves(current_mode, selected_channel, CMD_STOP);
          }
          
          jamming_active = false;
        } else {
          Serial.println("[MASTER] Already stopped.");
        }
        break;
        
  
        
 
    }
  }
  
  delay(100);
}
