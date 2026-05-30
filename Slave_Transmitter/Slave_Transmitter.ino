/**
 * Slave_Transmitter.ino
 * 
 * Slave NRF24L01+ transmitter for distributed Wi-Fi jamming.
 * Receives channel/mode/start-stop commands via I2C and jams
 * using NRFLite with NO_ACK packets on the specified frequency.
 * Targets: MH-Tiny ATtiny88 (ATTinyCore attinyx8micr) or
 *          Digispark ATtiny85 (ATTinyCore attinyx5micr).
 */

#include <NRFLite.h>
#include <Wire.h>

#define SLAVE_ID 0
#define I2C_ADDR (0x01 + SLAVE_ID)

#if defined(__AVR_ATtiny85__) || defined(__AVR_ATtinyX5__)
#define CE_PIN  3
#define CSN_PIN 4
#else
#define CE_PIN  9
#define CSN_PIN 10
#endif

#define PACKET_SIZE 4

volatile bool jamming = false;
volatile uint8_t current_mode = 0;
volatile uint8_t current_channel = 0;

NRFLite radio;

uint8_t calc_freq(uint8_t mode, uint8_t ch) {
  if (mode == 1 || mode == 3)
    return 12 + (ch - 1) * 5;
  return 15 + SLAVE_ID * 5;
}

void transmit_noise() {
  uint8_t payload[32];
  for (uint8_t i = 0; i < 32; i++)
    payload[i] = random(256);
  radio.send(255, payload, 32, NRFLite::NO_ACK);
}

void receiveI2C(int byteCount) {
  if (byteCount != PACKET_SIZE) return;

  uint8_t mode = Wire.read();
  uint8_t ch = Wire.read();
  uint8_t cmd = Wire.read();
  Wire.read();

  current_mode = mode;
  current_channel = ch;

  if (cmd == 1) {
    jamming = true;
    uint8_t freq = calc_freq(mode, ch);
    radio.init(SLAVE_ID, CE_PIN, CSN_PIN, NRFLite::BITRATE2MBPS, freq);
    Serial.println(F("[SLAVE] STARTED"));
  } else {
    jamming = false;
    Serial.println(F("[SLAVE] STOPPED"));
  }
}

void requestI2C() {
  Wire.write(jamming ? 1 : 0);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_ADDR);
  Wire.onReceive(receiveI2C);
  Wire.onRequest(requestI2C);

  if (radio.init(SLAVE_ID, CE_PIN, CSN_PIN, NRFLite::BITRATE2MBPS, 0))
    Serial.println(F("[SLAVE] Radio OK"));
  else
    Serial.println(F("[SLAVE] Radio FAIL"));
}

void loop() {
  if (jamming) transmit_noise();
}
