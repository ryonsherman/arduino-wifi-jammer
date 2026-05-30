# Code Review: Firmware Issues

## FIXED — Bug 1 [MEDIUM] — print_freq_map() during MODE_SWEEP

**File**: `Master_Controller/Master_Controller.ino`
**Status**: Fixed. Added MODE_SWEEP branch to `print_freq_map()` and `calc_freq()`.

## FIXED — Bug 2 [LOW] — Slave LFSR random hop offset biased

**File**: `Slave_Transmitter/Slave_Transmitter.ino`
**Status**: Fixed. Offset mapping changed to `0, 1, -1, 0` (avg 0.0).

## FIXED — Bug 3 [LOW] — Slave serial channel command resets pattern

**File**: `Slave_Transmitter/Slave_Transmitter.ino`
**Status**: Moot — serial parsing removed entirely.

## FIXED — Bug 4 [LOW] — Double "turn switch off" message

**File**: `Master_Controller/Master_Controller.ino`
**Status**: Fixed. Removed redundant print from `cancel_adaptive()`.

## FIXED — Bug 5 [CRITICAL] — Slave never stops on CMD_STOP

**File**: `Slave_Transmitter/Slave_Transmitter.ino:270`
**Problem**: `FLAG_JAMMING` was set/cleared in I2C handler but never checked in `loop()`. The transmission gate checked only `current_mode` which remained non-zero on CMD_STOP. Slaves kept transmitting indefinitely regardless of stop commands.

**Fix**: Changed the pending-processing block in `loop()` to check `flags & FLAG_JAMMING` instead of `current_mode`. On CMD_STOP, `current_mode` is set to 0, blocking the transmission gate.

## FIXED — Bug 6 [LOW] — Dead code and missing HW sweep checks

**File**: `Master_Controller/Master_Controller.ino`
- Removed dead `cancel_sweep()` forward declaration and definition
- Added `cancel_adaptive()` call to `adaptive start` path (was missing)  
- Added `hw_sweep_active` check to `adaptive` command block (was missing)
