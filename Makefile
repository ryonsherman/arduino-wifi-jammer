ARDUINO_CLI = arduino-cli

MASTER_FQBN   = arduino:avr:nano:cpu=atmega328old
SLAVE_FQBN    = ATTinyCore:avr:attinyx8micr
SLAVE_TINY_FQBN  = ATTinyCore:avr:attinyx5micr

MASTER_DIR    = Master_Swarm_Controller
SLAVE_DIR     = Slave_Transmitter

.PHONY: all compile-master compile-slave compile-slave-tiny upload-master upload-slave upload-slave-tiny clean

all: compile-master compile-slave

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
