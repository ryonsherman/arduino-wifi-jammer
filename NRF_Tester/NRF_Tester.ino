/**
 * NRF_Tester.ino
 * 
 * Simple NRF24L01+ module tester for Arduino Nano.
 * Connect an NRF module via SPI, power on, and check Serial Monitor.
 * 
 * Wiring (Nano to NRF24L01+):
 *   D9  -> CE
 *   D10 -> CSN
 *   D11 -> MOSI
 *   D12 -> MISO
 *   D13 -> SCK
 *   3.3V -> VCC  (IMPORTANT: 3.3V not 5V!)
 *   GND -> GND
 * 
 * Open Serial Monitor at 115200 baud.
 */

#include <SPI.h>
#include <NRFLite.h>

#define CE_PIN  9
#define CSN_PIN 10

NRFLite radio;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println(F("=== NRF24L01+ Tester ==="));
  Serial.println(F("Testing module..."));
  Serial.println();
  
  if (radio.init(0, CE_PIN, CSN_PIN, NRFLite::BITRATE2MBPS, 76)) {
    Serial.println(F("*** PASS - Radio OK! ***"));
    Serial.println();
    Serial.println(F("Module is working. You can swap to the next one."));
  } else {
    Serial.println(F("*** FAIL - Radio not responding ***"));
    Serial.println();
    Serial.println(F("Check:"));
    Serial.println(F("  - Wiring (CE=D9, CSN=D10, MOSI=D11, MISO=D12, SCK=D13)"));
    Serial.println(F("  - Power (must be 3.3V, not 5V)"));
    Serial.println(F("  - GND connection"));
    Serial.println(F("  - Module not damaged"));
  }
  
  Serial.println();
  Serial.println(F("Press reset button to test again."));
}

void loop() {
  // Nothing to do - just reset to test another module
}
