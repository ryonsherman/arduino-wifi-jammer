/**
 * Slave_Transmitter.ino
 * 
 * Slave NRF24L01+ transmitter for distributed Wi-Fi jamming.
 * Receives channel/mode/start-stop commands via I2C and jams
 * using NRFLite with NO_ACK packets on the specified frequency.
 * Targets: MH-Tiny ATtiny88 (ATTinyCore attinyx8micr) or
 *          Digispark ATtiny85 (ATTinyCore attinyx5micr).
 * 
 * Memory budget (ATtiny88): 512B RAM, 6780B Flash
 * Optimization target: <256B RAM to leave headroom for stack
 */

#include <NRFLite.h>
#include <Wire.h>

// Uncomment for debug output (adds ~100 bytes RAM for Serial buffer)
// #define DEBUG_SERIAL

#define SLAVE_ID 0
#define I2C_ADDR (0x01 + SLAVE_ID)

#if defined(__AVR_ATtiny85__) || defined(__AVR_ATtinyX5__)
#define CE_PIN  3
#define CSN_PIN 4
#else
#define CE_PIN  9
#define CSN_PIN 10
#endif

// Protocol constants
#define PACKET_SIZE 4
#define MAX_FREQ_OFFSET 83

// Separate current/pending state to avoid race conditions (CRIT-2 fix)
// ISR writes to pending_*, loop() copies to current_* under cli() protection
static volatile uint8_t pending_flags = 0;    // bit 0: jamming, bit 1: has_pending
static volatile uint8_t pending_mode = 0;
static volatile uint8_t pending_channel = 0;
static volatile uint8_t pending_slave_id = SLAVE_ID;

static uint8_t current_jamming = 0;
static uint8_t current_mode = 0;
static uint8_t current_channel = 0;
static uint8_t current_slave_id = SLAVE_ID;

#define FLAG_JAMMING    0x01
#define FLAG_PENDING    0x02

NRFLite radio;

// Fast 8-bit LFSR for noise generation (faster than random())
static uint8_t lfsr = 0xAC;  // Non-zero seed
#define LFSR_NEXT() (lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB8))

/**
 * Calculate NRF24 channel (frequency offset from 2400 MHz)
 * Optimized: uses shift instead of multiply for *2, *5
 * 
 * @param mode: 1/3 = Single/Custom (fan-out), 2 = Full Spectrum
 * @param ch: Wi-Fi channel (1-13)
 * @param sid: Slave ID (0-11)
 * @return: NRF24 channel (0-83)
 */
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t sid) __attribute__((noinline));
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t sid) {
  uint8_t freq;
  if (mode == 1 || mode == 3) {
    // center = 12 + (ch-1)*5 = 7 + ch*5
    // fan-out = center + sid*2 - 11 = ch*5 + sid*2 - 4
    freq = (ch << 2) + ch + (sid << 1) - 4;
  } else {
    // Full spectrum: 15 + sid*5
    freq = 15 + (sid << 2) + sid;
  }
  return (freq > MAX_FREQ_OFFSET) ? MAX_FREQ_OFFSET : freq;
}

/**
 * Transmit 32-byte noise burst
 * Uses static buffer to avoid stack overflow risk on 512B MCU (CRIT-3 fix)
 */
static void transmit_noise() {
  static uint8_t buf[32];
  // Unrolled 4x for speed (8 iterations of 4 bytes each)
  uint8_t *p = buf;
  for (uint8_t i = 0; i < 8; i++) {
    *p++ = LFSR_NEXT();
    *p++ = LFSR_NEXT();
    *p++ = LFSR_NEXT();
    *p++ = LFSR_NEXT();
  }
  radio.send(255, buf, 32, NRFLite::NO_ACK);
}

/**
 * I2C receive handler - stores pending config for loop() to process
 * Validates packet size and channel bounds before accepting
 */
void receiveI2C(int byteCount) {
  if (byteCount != PACKET_SIZE) return;

  uint8_t mode = Wire.read();
  uint8_t channel = Wire.read();
  uint8_t cmd = Wire.read();
  uint8_t slave_id = Wire.read();

  // Validate channel (1-13) for single channel and custom modes
  if ((mode == 1 || mode == 3) && (channel < 1 || channel > 13)) {
    return;
  }

  // Store in pending state (ISR only writes pending_*, loop() reads)
  pending_mode = mode;
  pending_channel = channel;
  pending_slave_id = slave_id;
  
  // Set pending flag and jamming state
  if (cmd == 1) {
    pending_flags = FLAG_PENDING | FLAG_JAMMING;
  } else {
    pending_flags = FLAG_PENDING;  // clears jamming
  }
}

void requestI2C() {
  Wire.write(current_jamming ? 1 : 0);
}

void setup() {
#ifdef DEBUG_SERIAL
  Serial.begin(115200);
#endif
  Wire.begin(I2C_ADDR);
  Wire.onReceive(receiveI2C);
  Wire.onRequest(requestI2C);

  if (!radio.init(SLAVE_ID, CE_PIN, CSN_PIN, NRFLite::BITRATE2MBPS, 0)) {
#ifdef DEBUG_SERIAL
    Serial.println(F("Radio FAIL"));
#endif
  }
#ifdef DEBUG_SERIAL
  else {
    Serial.println(F("Radio OK"));
  }
#endif
}

void loop() {
  // Atomically read and clear pending flag (CRIT-1 fix)
  uint8_t sreg = SREG;
  cli();
  uint8_t flags = pending_flags;
  if (flags & FLAG_PENDING) {
    pending_flags = 0;  // Clear pending flag
    // Copy pending to current while interrupts disabled (CRIT-2 fix)
    current_mode = pending_mode;
    current_channel = pending_channel;
    current_slave_id = pending_slave_id;
    current_jamming = (flags & FLAG_JAMMING) ? 1 : 0;
  }
  SREG = sreg;
  
  if (flags & FLAG_PENDING) {
    if (current_jamming) {
      uint8_t freq = calc_freq(current_mode, current_channel, current_slave_id);
      radio.init(SLAVE_ID, CE_PIN, CSN_PIN, NRFLite::BITRATE2MBPS, freq);
#ifdef DEBUG_SERIAL
      Serial.print(F("@ "));
      Serial.println(2400 + freq);
#endif
    }
#ifdef DEBUG_SERIAL
    else {
      Serial.println(F("STOP"));
    }
#endif
  }
  
  if (current_jamming) {
    transmit_noise();
  }
}
