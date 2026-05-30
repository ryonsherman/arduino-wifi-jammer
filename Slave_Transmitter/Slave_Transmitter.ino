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
#define TOTAL_SLAVES 12

// Compile-time guard: local_idx and group_size are packed into 4-bit nibbles (max 15)
// See FANOUT_* macros and Master_Swarm_Controller.ino send_cmd() byte 4 packing
#if TOTAL_SLAVES > 15
#error "TOTAL_SLAVES exceeds 15 - nibble packing in fanout byte will overflow"
#endif

// Separate current/pending state to avoid race conditions (CRIT-2 fix)
// ISR writes to pending_*, loop() copies to current_* under cli() protection
static volatile uint8_t pending_flags = 0;    // bit 0: jamming, bit 1: has_pending
static volatile uint8_t pending_mode = 0;
static volatile uint8_t pending_channel = 0;
static volatile uint8_t pending_fanout = (TOTAL_SLAVES);  // packed: local_idx<<4 | group_size

static uint8_t current_jamming = 0;
static uint8_t current_mode = 0;
static uint8_t current_channel = 0;
static uint8_t current_fanout = (TOTAL_SLAVES);  // packed: local_idx<<4 | group_size

// Unpack fanout: local_idx from high nibble, group_size from low nibble
#define FANOUT_LOCAL_IDX(f)  ((f) >> 4)
#define FANOUT_GROUP_SIZE(f) ((f) & 0x0F)

#define FLAG_JAMMING    0x01
#define FLAG_PENDING    0x02

NRFLite radio;

// Fast 8-bit LFSR for noise generation (faster than random())
static uint8_t lfsr = 0xAC;  // Non-zero seed
#define LFSR_NEXT() (lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB8))

/**
 * Calculate NRF24 channel (frequency offset from 2400 MHz)
 * Uses local fan-out formula: center + (local_idx * 2) - (group_size - 1)
 * 
 * @param mode: 1/3 = Single/Custom (fan-out), 2 = Full Spectrum
 * @param ch: Wi-Fi channel (1-13)
 * @param local_idx: Local index within channel group (0 to group_size-1)
 * @param group_size: Number of slaves in this channel group
 * @return: NRF24 channel (0-83)
 * 
 * Worked example (Single Channel mode, ch=6, 3 slaves on channel):
 *   center = 7 + ch*5 = 7 + 30 = 37  (Wi-Fi ch6 center is 2437 MHz)
 *   For slave with local_idx=0, group_size=3:
 *     offset = (0 * 2) - (3 - 1) = 0 - 2 = -2
 *     freq = 37 + (-2) = 35  => 2435 MHz
 *   For slave with local_idx=1, group_size=3:
 *     offset = (1 * 2) - (3 - 1) = 2 - 2 = 0
 *     freq = 37 + 0 = 37  => 2437 MHz (center)
 *   For slave with local_idx=2, group_size=3:
 *     offset = (2 * 2) - (3 - 1) = 4 - 2 = 2
 *     freq = 37 + 2 = 39  => 2439 MHz
 *   Result: 3 slaves spread evenly across 4 MHz band centered on ch6
 */
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t local_idx, uint8_t group_size) __attribute__((noinline));
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t local_idx, uint8_t group_size) {
  uint8_t freq;
  if (mode == 1 || mode == 3) {
    // center = 12 + (ch-1)*5 = 7 + ch*5
    // offset = (local_idx * 2) - (group_size - 1)
    // freq = center + offset
    int8_t offset = (local_idx << 1) - (group_size - 1);
    uint8_t center = 7 + (ch << 2) + ch;  // 7 + ch*5
    freq = center + offset;
  } else {
    // Full spectrum: 15 + local_idx*5
    freq = 15 + (local_idx << 2) + local_idx;
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
 * Byte 4 format: local_idx (high nibble) | group_size (low nibble)
 */
void receiveI2C(int byteCount) {
  if (byteCount != PACKET_SIZE) return;

  uint8_t mode = Wire.read();
  uint8_t channel = Wire.read();
  uint8_t cmd = Wire.read();
  uint8_t packed = Wire.read();
  
  // Extract and validate local_idx and group_size from packed byte
  uint8_t local_idx = packed >> 4;
  uint8_t group_size = packed & 0x0F;
  
  // Clamp group_size to valid range (1-12)
  if (group_size == 0) group_size = 1;
  if (group_size > TOTAL_SLAVES) group_size = TOTAL_SLAVES;

  // Clamp local_idx to valid range (0 to group_size-1) (CRIT-1 fix)
  if (local_idx >= group_size) local_idx = group_size - 1;

  // Validate channel (1-13) for single channel and custom modes
  if ((mode == 1 || mode == 3) && (channel < 1 || channel > 13)) {
    return;
  }

  // Store in pending state (ISR only writes pending_*, loop() reads)
  pending_mode = mode;
  pending_channel = channel;
  pending_fanout = (local_idx << 4) | group_size;
  
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
    current_fanout = pending_fanout;
    current_jamming = (flags & FLAG_JAMMING) ? 1 : 0;
  }
  SREG = sreg;
  
  if (flags & FLAG_PENDING) {
    if (current_jamming) {
      uint8_t freq = calc_freq(current_mode, current_channel, 
                               FANOUT_LOCAL_IDX(current_fanout), 
                               FANOUT_GROUP_SIZE(current_fanout));
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
