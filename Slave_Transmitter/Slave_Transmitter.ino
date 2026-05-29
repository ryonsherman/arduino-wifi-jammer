/**
 * Slave Transmitter
 * 
 * Distributed Wi-Fi Jamming Slave Node
 * - Self-tests SPI/NRF24L01+ via USB Serial on boot
 * - Listens for I2C commands from Master
 * - Spreads across subchannels within specified Wi-Fi channel
 * - Or covers full 2.4GHz spectrum for wideband jamming
 * 
 * Hardware: Arduino Nano + NRF24L01+
 * I2C Address: 0x01 to 0x0C (set by SLAVE_ID)
 */

#include <SPI.h>
#include <RF24.h>
#include <Wire.h>

// --- Configuration ---
#define SLAVE_ID 0  // Set to 0-11 for each slave (0x01-0x0C I2C address)
#define I2C_ADDR (0x01 + SLAVE_ID)

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
#define PACKET_SIZE 4

// --- State ---
volatile bool jamming = false;
volatile uint8_t current_mode = 0;
volatile uint8_t current_channel = 0;

// --- NRF24 Instance ---
RF24 radio(CE_PIN, CSN_PIN);

/**
 * Print slave info and self-test result via USB Serial
 */
void print_self_test() {
  Serial.print("[SLAVE ");
  Serial.print(SLAVE_ID);
  Serial.print("] Self-Test: ");
  
  // Initialize SPI
  SPI.begin();
  
  if (!radio.begin()) {
    Serial.println("[FAIL] NRF24L01+ Not Found!");
    return;
  }
  
  // Read STATUS register (SPI test)
  uint8_t status = radio.STATUS;
  Serial.print("STATUS=0x");
  Serial.print(status, HEX);
  Serial.println(" [OK]");
  
  // Print configuration
  Serial.print("[SLAVE ");
  Serial.print(SLAVE_ID);
  Serial.print("] Address: 0x");
  Serial.print(I2C_ADDR, HEX);
  Serial.print(" | Frequency Offset: ");
  Serial.print(SLAVE_ID);
  Serial.println(" channels");
}

/**
 * Calculate target frequency based on mode and slave ID
 * @param mode: 1=Single Channel, 2=Full Spectrum, 3=Custom Channel
 * @param channel: Wi-Fi channel (1-13) or 0 for full spectrum
 * @return: Target frequency in MHz offset from 2400
 * 
 * Strategy:
 * - Single Channel: All 12 slaves sit ON the specified channel (same freq)
 * - Full Spectrum: 12 slaves cover 60MHz span (channels 1-13) at 5MHz spacing (mid-channels)
 * - Custom: Exact channel frequency for that slave
 */
uint8_t calculate_target_freq(uint8_t mode, uint8_t channel) {
  double target;
  
  if (mode == 3) {
    // Custom mode - jam exact channel frequency
    target = (channel == 1) ? CHANNEL_1_BASE :
             (channel == 6) ? 2437 :
             (channel == 11) ? 2462 :
             (channel == 13) ? CHANNEL_13_BASE : 2412;
    
    // Clamp to valid range
    if (target < 2400) target = 2400;
    if (target > 2527) target = 2527;
    return (uint8_t)(target - BASE_FREQ_2400);
  }
  
  if (mode == 1) {
    // Single channel mode - ALL slaves on exact channel frequency (no spreading)
    target = (channel == 1) ? CHANNEL_1_BASE :
             (channel == 6) ? 2437 :
             (channel == 11) ? 2462 :
             (channel == 13) ? CHANNEL_13_BASE : 2412;
  } else {
    // Full spectrum mode - mid-channel spacing (between channels)
    // Slaves hit: 2415, 2420, 2425, 2430, 2435, 2440, 2445, 2450, 2455, 2460, 2465, 2470
    double step = FULL_SPAN_MHZ / (double)TOTAL_CHANNELS;
    target = CHANNEL_1_BASE + 3 + (SLAVE_ID * step);
  }
  
  // Clamp to valid range (2400-2527 MHz for NRF24)
  if (target < 2400) target = 2400;
  if (target > 2527) target = 2527;
  
  return (uint8_t)(target - BASE_FREQ_2400);
}

/**
 * Generate random noise and transmit continuously
 */
void transmit_noise() {
  uint8_t payload[32];
  
  // Generate random noise payload
  for (int i = 0; i < 32; i++) {
    payload[i] = random(0, 255);
  }
  
  // Set frequency and transmit
  radio.setPayloadSize(32);
  radio.write(&payload, 32, false); // No ACK for continuous jamming
}

/**
 * I2C Receive handler - called when Master sends command
 * Receives 4-byte packet: mode, channel, cmd, pad
 * Note: byteCount indicates total bytes received in this transaction
 */
void receiveI2C(int byteCount) {
  if (byteCount == 4) {
    uint8_t mode = Wire.read();
    uint8_t channel = Wire.read();
    uint8_t cmd = Wire.read();
    uint8_t pad = Wire.read();
    
    current_mode = mode;
    current_channel = channel;
    
    if (cmd == CMD_START) {
      jamming = true;
      Serial.print("[SLAVE ");
      Serial.print(SLAVE_ID);
      Serial.print("] STARTED JAMMING Mode=");
      Serial.print(mode);
      Serial.print(" Channel=");
      Serial.println(channel);
    } else if (cmd == CMD_STOP) {
      jamming = false;
      Serial.print("[SLAVE ");
      Serial.print(SLAVE_ID);
      Serial.print("] STOPPED JAMMING");
      Serial.println();
    }
  } else {
    Serial.print("[SLAVE ");
    Serial.print(SLAVE_ID);
    Serial.print("] WARNING: Expected 4 bytes, got ");
    Serial.println(byteCount);
  }
}

/**
 * I2C Request handler - called when Master queries status
 */
void requestI2C() {
  Wire.write(jamming ? 1 : 0); // Report jamming state
}

/**
 * Setup - runs once at boot
 */
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== SLAVE TRANSMITTER ===");
  
  // I2C setup
  Wire.begin(I2C_ADDR);
  Wire.onReceive(receiveI2C);
  Wire.onRequest(requestI2C);
  
  // USB Serial self-test
  print_self_test();
  
  // Configure NRF24L01+ for continuous TX (no ACK)
  radio.begin();
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_2MBPS);
  radio.setChannel(0); // Will be updated by calculate_target_freq
  radio.openWritingPipe(0x01020304);
  radio.setPALevel(RF24_PA_MAX);
  radio.startListening(); // Keep in TX mode
  radio.stopListening();  // Switch to TX for jamming
  
  Serial.print("[SLAVE ");
  Serial.print(SLAVE_ID);
  Serial.println("] Ready. Waiting for I2C commands...");
}

/**
 * Main loop - checks for I2C commands and jams if active
 */
void loop() {
  if (jamming) {
    uint8_t freq = calculate_target_freq(current_mode, current_channel);
    radio.setChannel(freq);
    transmit_noise();
  }
  
  // Small delay to prevent busyness
  delay(1);
}
