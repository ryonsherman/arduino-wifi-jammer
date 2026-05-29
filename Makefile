ARDUINO_CLI = arduino-cli

MASTER_FQBN   = arduino:avr:nano:cpu=atmega328old
SLAVE_FQBN    = ATTinyCore:avr:attinyx8micr
SLAVE85_FQBN  = ATTinyCore:avr:attinyx5micr

MASTER_DIR    = Master_Swarm_Controller
SLAVE_DIR     = Slave_Transmitter

.PHONY: all compile-master compile-slave compile-slave-85 upload-master upload-slave upload-slave-85 clean

all: compile-master compile-slave

# --- Compile ---

compile-master:
	$(ARDUINO_CLI) compile --fqbn $(MASTER_FQBN) $(MASTER_DIR)

compile-slave:
	$(ARDUINO_CLI) compile --fqbn $(SLAVE_FQBN) $(SLAVE_DIR)

compile-slave-85:
	$(ARDUINO_CLI) compile --fqbn $(SLAVE85_FQBN) $(SLAVE_DIR)

# --- Upload ---
# Usage: make upload-master PORT=/dev/cu.usbserial-XXXX
#        make upload-slave PORT=auto (or omit - plugs in when prompted for micronucleus)

upload-master: compile-master
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(MASTER_FQBN) $(MASTER_DIR)

upload-slave: compile-slave
	$(ARDUINO_CLI) upload --fqbn $(SLAVE_FQBN) $(SLAVE_DIR)

upload-slave-85: compile-slave-85
	$(ARDUINO_CLI) upload --fqbn $(SLAVE85_FQBN) $(SLAVE_DIR)

# --- Utility ---

clean:
	rm -rf $(MASTER_DIR)/build $(SLAVE_DIR)/build
