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
 * 
 * Serial debugging via SoftwareSerial on PD0(RX)/PD1(TX) - connect FTDI adapter
 */
#include <NRFLite.h>
#include <SPI.h>
#include <Wire.h>

#define SLAVE_ID 0
#define I2C_ADDR (0x01 + SLAVE_ID)

// Mode constants (must match Master_Controller.ino)
#define MODE_SINGLE_CHANNEL 1
#define MODE_FULL_SPECTRUM 2
#define MODE_CUSTOM 3
#define MODE_SWEEP 4
#define CMD_START 1
#define CMD_STOP 0

#define CE_PIN  9
#define CSN_PIN 10
#define LED_PIN 0   // Red RX LED on MH-Tiny (D0/PD0)

// Protocol constants
#define PACKET_SIZE 6
#define MAX_FREQ_OFFSET 83
#define TOTAL_SLAVES 12

// Compile-time guard: local_idx and group_size are packed into 4-bit nibbles (max 15)
// See FANOUT_* macros and Master_Controller.ino send_cmd() byte 4 packing
#if TOTAL_SLAVES > 15
#error "TOTAL_SLAVES exceeds 15 - nibble packing in fanout byte will overflow"
#endif

// Separate current/pending state to avoid race conditions (CRIT-2 fix)
// ISR writes to pending_*, loop() copies to current_* under cli() protection
static volatile uint8_t pending_flags = 0;    // bit 0: jamming, bit 1: has_pending
static volatile uint8_t pending_mode = 0;
static volatile uint8_t pending_channel = 0;
static volatile uint8_t pending_fanout = (TOTAL_SLAVES);  // packed: local_idx<<4 | group_size
static volatile uint8_t pending_power = 3;  // 0=MIN, 1=LOW, 2=HIGH, 3=MAX

static uint8_t current_mode = 0;
static uint8_t current_channel = 0;
static uint8_t current_fanout = (TOTAL_SLAVES);  // packed: local_idx<<4 | group_size
static uint8_t current_power = 3;  // 0=MIN, 1=LOW, 2=HIGH, 3=MAX

static volatile uint8_t pending_pattern = 0;
static uint8_t current_pattern = 0;

static uint8_t noise_buf[32];

// Unpack fanout: local_idx from high nibble, group_size from low nibble
#define FANOUT_LOCAL_IDX(f)  ((f) >> 4)
#define FANOUT_GROUP_SIZE(f) ((f) & 0x0F)

#define FLAG_JAMMING    0x01
#define FLAG_PENDING    0x02

NRFLite radio;

// Fast 8-bit LFSR for noise generation (faster than random())
static uint8_t lfsr = 0xAC;  // Non-zero seed
#define LFSR_NEXT() (lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB8))

static void slave_set_power(uint8_t level) {
  if (level > 3) level = 3;
  digitalWrite(CSN_PIN, LOW);
  SPI.transfer(0x20 | 0x06);
  SPI.transfer(0x09 | (level << 1));
  digitalWrite(CSN_PIN, HIGH);
}

/**
 * Fast RF channel change - writes only RF_CH register without full radio init.
 * Used by sweep mode (mode=4) for rapid channel cycling (~microseconds vs ~100ms).
 */
static void set_nrf_channel(uint8_t freq) {
  if (freq > MAX_FREQ_OFFSET) freq = MAX_FREQ_OFFSET;
  digitalWrite(CSN_PIN, LOW);
  SPI.transfer(0x20 | 0x05);
  SPI.transfer(freq);
  digitalWrite(CSN_PIN, HIGH);
}

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
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t local_idx, uint8_t group_size) {
  uint8_t freq;
  if (mode == MODE_SINGLE_CHANNEL || mode == MODE_CUSTOM || mode == MODE_SWEEP) {
    // center = 12 + (ch-1)*5 = 7 + ch*5
    // offset = (local_idx * 2) - (group_size - 1)
    // freq = center + offset
    int8_t offset = (local_idx << 1) - (group_size - 1);
    uint8_t center = 7 + (ch << 2) + ch;  // 7 + ch*5
    freq = center + offset;
  } else {
    // Full spectrum: 14 + local_idx*5  (staggered between channels for max coverage)
    freq = 14 + (local_idx << 2) + local_idx;
  }
  return (freq > MAX_FREQ_OFFSET) ? MAX_FREQ_OFFSET : freq;
}



/**
 * Transmit 32-byte noise burst
 * Uses static buffer to avoid stack overflow risk on 512B MCU (CRIT-3 fix)
 */
static void transmit_noise() {
  uint8_t *buf = noise_buf;
  uint8_t *p = buf;
  for (uint8_t i = 0; i < 16; i++) {
    *p++ = LFSR_NEXT();
    *p++ = LFSR_NEXT();
  }
  if (current_pattern == 2) {
    uint8_t base = calc_freq(current_mode, current_channel,
                             FANOUT_LOCAL_IDX(current_fanout),
                             FANOUT_GROUP_SIZE(current_fanout));
    uint8_t r = lfsr & 0x03;  // 0-3
    int8_t offset = (int8_t)(r & 0x02 ? (int8_t)r - 3 : (int8_t)r);  // 0, 1, -1, 0 → avg 0
    uint8_t freq = base + offset;
    if (freq > MAX_FREQ_OFFSET) freq = MAX_FREQ_OFFSET;
    set_nrf_channel(freq);
  }
  radio.send(255, buf, 32, NRFLite::NO_ACK);
}

/**
 * I2C receive handler - stores pending config for loop() to process
 * Validates packet size and channel bounds before accepting
 * Byte 4 format: local_idx (high nibble) | group_size (low nibble)
 * Byte 5: power level (0=MIN, 1=LOW, 2=HIGH, 3=MAX)
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

  // Validate channel (1-13) for single channel, custom, and sweep modes
  if ((mode == MODE_SINGLE_CHANNEL || mode == MODE_CUSTOM || mode == MODE_SWEEP) && (channel < 1 || channel > 13)) {
    return;
  }

  // Store in pending state (ISR only writes pending_*, loop() reads)
  pending_mode = mode;
  pending_channel = channel;
  pending_fanout = (local_idx << 4) | group_size;
  if (byteCount >= 5) {
    pending_power = Wire.read();
  }
  if (byteCount >= 6) {
    pending_pattern = Wire.read();
  }
  
  // Set pending flag and jamming state
  if (cmd == CMD_START) {
    pending_flags = FLAG_PENDING | FLAG_JAMMING;
  } else {
    pending_flags = FLAG_PENDING;  // clears jamming
  }
}

void requestI2C() {
  Wire.write(current_mode ? 1 : 0);
}

static void blink(uint8_t count, uint16_t ms) {
  pinMode(LED_PIN, OUTPUT);
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
    digitalWrite(LED_PIN, LOW);
    if (i < count - 1) delay(ms);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Wire.begin(I2C_ADDR);
  Wire.onReceive(receiveI2C);
  Wire.onRequest(requestI2C);

  if (!radio.init(SLAVE_ID, CE_PIN, CSN_PIN, NRFLite::BITRATE2MBPS, 0)) {
    blink(5, 100);
    digitalWrite(LED_PIN, LOW);
  } else {
    digitalWrite(LED_PIN, HIGH);
    delay(3000);
    digitalWrite(LED_PIN, LOW);
  }
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
    current_power = pending_power;
    current_pattern = pending_pattern;
  }
  SREG = sreg;
  
  if (flags & FLAG_PENDING) {
    if (flags & FLAG_JAMMING) {
      uint8_t freq = calc_freq(current_mode, current_channel, 
                               FANOUT_LOCAL_IDX(current_fanout), 
                               FANOUT_GROUP_SIZE(current_fanout));
      if (current_mode == MODE_SWEEP) {
        set_nrf_channel(freq);
      } else {
        radio.init(SLAVE_ID, CE_PIN, CSN_PIN, NRFLite::BITRATE2MBPS, freq);
        slave_set_power(current_power);
      }
    } else {
      current_mode = 0;
    }
  }
  
  if (current_mode) {
    static uint32_t last_led = 0;
    static bool led_on = true;
    uint32_t now = millis();
    uint16_t interval = led_on ? 100 : 1900;
    if (now - last_led >= interval) {
      last_led = now;
      led_on = !led_on;
      digitalWrite(LED_PIN, led_on);
    }
    transmit_noise();
  }
}
