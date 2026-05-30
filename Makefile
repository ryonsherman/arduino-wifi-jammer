# Makefile
#
# Build system for the distributed Wi-Fi jammer project.
# Targets: master (Arduino Nano), slave (MH-Tiny ATtiny88),
#          slave-tiny (Digispark ATtiny85).
# Uses arduino-cli with ATTinyCore and NRFLite.
#
# make       = compile master + ATtiny88 slave
# make tiny  = compile master + ATtiny85 slave
# make upload-master PORT=/dev/cu.usbserial-XXXX
# make upload-slave   (micronucleus — plug when prompted)
# make upload-slave-tiny (micronucleus)

ARDUINO_CLI = arduino-cli

MASTER_FQBN   = arduino:avr:nano:cpu=atmega328old
SLAVE_FQBN    = ATTinyCore:avr:attinyx8micr
SLAVE_TINY_FQBN  = ATTinyCore:avr:attinyx5micr

MASTER_DIR    = Master_Swarm_Controller
SLAVE_DIR     = Slave_Transmitter

.PHONY: all tiny compile-master compile-slave compile-slave-tiny upload-master upload-slave upload-slave-tiny clean

# make        = master + slave (ATtiny88)
# make tiny   = master + slave-tiny (ATtiny85)
all: compile-master compile-slave

tiny: compile-master compile-slave-tiny

# --- Compile ---

compile-master:
	$(ARDUINO_CLI) compile --fqbn $(MASTER_FQBN) $(MASTER_DIR)

compile-slave:
	$(ARDUINO_CLI) compile --fqbn $(SLAVE_FQBN) $(SLAVE_DIR)

compile-slave-tiny:
	$(ARDUINO_CLI) compile --fqbn $(SLAVE_TINY_FQBN) $(SLAVE_DIR)

# --- Upload ---
# Usage: make upload-master PORT=/dev/cu.usbserial-XXXX
#        make upload-slave PORT=auto (or omit - plugs in when prompted for micronucleus)

upload-master: compile-master
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(MASTER_FQBN) $(MASTER_DIR)

upload-slave: compile-slave
	$(ARDUINO_CLI) upload --fqbn $(SLAVE_FQBN) $(SLAVE_DIR)

upload-slave-tiny: compile-slave-tiny
	$(ARDUINO_CLI) upload --fqbn $(SLAVE_TINY_FQBN) $(SLAVE_DIR)

# --- Utility ---

clean:
	rm -rf $(MASTER_DIR)/build $(SLAVE_DIR)/build
