# Makefile
#
# Build system for the distributed Wi-Fi jammer project.
# Targets: master (Arduino Nano), slave (MH-Tiny ATtiny88)
#
# make                = compile master + slave
# make upload-master  = compile & upload master (PORT auto-detected or override with PORT=...)
# make upload-slave   = compile & upload slave via USBasp ISP
# make monitor        = open serial monitor for master

ARDUINO_CLI = arduino-cli

MASTER_FQBN   = arduino:avr:nano:cpu=atmega328
SLAVE_FQBN    = ATTinyCore:avr:attinyx8micr

MASTER_DIR    = Master_Swarm_Controller
SLAVE_DIR     = Slave_Transmitter

# Auto-detect Nano port (CH340 serial)
PORT ?= $(shell ls /dev/cu.wchusbserial* 2>/dev/null | head -1)

.PHONY: all compile-master compile-slave upload-master upload-slave upload-slave-isp monitor clean

# make = master + slave
all: compile-master compile-slave

# --- Compile ---

compile-master:
	$(ARDUINO_CLI) compile --fqbn $(MASTER_FQBN) $(MASTER_DIR)

compile-slave:
	$(ARDUINO_CLI) compile --fqbn $(SLAVE_FQBN) $(SLAVE_DIR)

# --- Upload ---

upload-master: compile-master
ifndef PORT
	$(error No serial port found. Plug in Nano or set PORT=/dev/cu.xxx)
endif
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(MASTER_FQBN) $(MASTER_DIR)

# Upload slave via USBasp ISP programmer
upload-slave: compile-slave
	avrdude -c usbasp -p t88 -B 125kHz -U flash:w:$(SLAVE_DIR)/build/ATTinyCore.avr.attinyx8micr/$(SLAVE_DIR).ino.hex:i

# --- Monitor ---

monitor:
ifndef PORT
	$(error No serial port found. Plug in Nano or set PORT=/dev/cu.xxx)
endif
	@echo "Opening serial monitor at 115200 baud (Ctrl+C to exit)"
	$(ARDUINO_CLI) monitor -p $(PORT) -c baudrate=115200

# --- Utility ---

clean:
	rm -rf $(MASTER_DIR)/build $(SLAVE_DIR)/build
