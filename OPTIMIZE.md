# Optimizations Report

**Date**: 2026-05-29
**Scope**: Full review of `Master_Controller.ino` + `Slave_Transmitter.ino`

---

## 1. Overview

### Target comparison

| Metric | Master_Controller (ATmega328P) | Slave_Transmitter (ATtiny88) |
|--------|-------------------------------|------------------------------|
| Flash total | 30,720 B | 6,780 B |
| Flash used | **23,476 B (76%)** | **3,636 B (53%)** |
| Flash free | 7,244 B (24%) | 3,144 B (47%) |
| RAM total | 2,048 B | 512 B |
| RAM (static) | **560 B (27%)** | **258 B (50%)** |
| RAM (stack+heap) | 1,488 B (73%) | **254 B (50%)** |

**Key takeaway**: The Slave is the constrained target — at 50% static RAM with only 254 B left for stack + heap + malloc, every byte matters. The Master has more headroom but the `String` class usage creates a real heap-fragmentation risk over long-running operation.

---

## 2. Optimization Inventory

Optimizations are grouped by category and ordered by priority within each group.

---

### GROUP A: RAM OPTIMIZATIONS

---

#### [A1] Replace `String` class with C-string parsing in `executeCommand()`

| Field | Value |
|-------|-------|
| **Priority** | **CRITICAL** |
| **File** | `Master_Controller.ino` |
| **Lines** | 1023–1413 |
| **Difficulty** | Medium |
| **Risk** | Medium (logic must be preserved exactly) |
| **Category** | RAM / Heap fragmentation |

**Analysis**:

`executeCommand(String &cmdLine)` is the heart of the CLI. It is called on every serial input and performs multiple `String` allocations per invocation:

| Operation | Heap alloc per call | Typical size |
|-----------|-------------------|--------------|
| `cmdLine.substring(0, sp)` | 1×String + char[] | ~8 B |
| `cmdLine.substring(sp + 1)` | 1×String + char[] | ~20 B |
| Per-segment `args.substring()` in loop | 1×String + char[] per iteration | ~8 B each |
| `args.toCharArray(buf, 64)` | Copies to stack | 64 B on stack |
| `args == ""` | Temporary `String("")` | ~7 B |

On a system with 2 KB RAM and 1,488 B shared heap+stack, repeated `malloc`/`free` from `String` operations will fragment the heap over hours/days of continuous operation.

**Change**:

Replace all `String` operations with direct `char*` manipulation on the input buffer:

```cpp
void executeCommand(String &cmdLine) {
    cmdLine.trim();
    if (cmdLine.length() == 0) return;

    // Work on a mutable C-string copy (stack)
    char buf[48];  // max command length
    cmdLine.toCharArray(buf, sizeof(buf));

    // Extract first word (command)
    char *cmd = strtok(buf, " ");
    char *args = strtok(NULL, "");  // rest of line

    if (strcmp_P(cmd, PSTR("help")) == 0) {
        print_help();
    } else if (strcmp_P(cmd, PSTR("get")) == 0) {
        // ... parse args with strtok
    }
    // ... etc
}
```

**Expected savings**:
- **RAM**: Eliminates temporary `String` heap allocations per command (0 extra heap per call instead of ~30–60 B)
- **Flash**: Net ~100–300 B savings (eliminates String library vtables/code, adds manual parser code)
- **Risk eliminated**: Heap fragmentation from repeated String operations

**Additional benefit**: Eliminates the `char buf[64]` on line 1404 (the `.toCharArray()` for profile), replacing it with the single `buf[48]` at the top of the function.

---

#### [A2] Reduce stack buffer in `cmd_profile`

| Field | Value |
|-------|-------|
| **Priority** | High |
| **File** | `Master_Controller.ino` |
| **Lines** | 184 |
| **Difficulty** | Very Low |
| **Risk** | None |
| **Category** | RAM / Stack |

**Analysis**:

```cpp
char name[PROFILE_NAME_LEN];  // line 184 — 16 bytes on stack
```

This is fine at 16 B. However, `cmd_profile` is called from `executeCommand` which already has the command buffer on stack — so stack depth is: `loop()` → `executeCommand()` (48 B buf) → `cmd_profile()` (16 B name) = ~80 B peak. Acceptable.

**Change**: No change needed — already sized to `PROFILE_NAME_LEN` (16). Keep as-is.

**Expected savings**: None (already optimal).

---

#### [A3] Reduce `buf` size in `executeCommand` from 64 to 20

| Field | Value |
|-------|-------|
| **Priority** | Medium |
| **File** | `Master_Controller.ino` |
| **Lines** | 1404 |
| **Difficulty** | Low |
| **Risk** | Low |
| **Category** | RAM / Stack |

**Analysis**:

```cpp
char buf[64];  // line 1404 — 64 bytes on stack, ONLY for profile command
args.toCharArray(buf, sizeof(buf));
cmd_profile(buf);
```

This 64-byte buffer sits on the stack for every single command, but is only used for the `profile` subcommand. The profile name is limited to `PROFILE_NAME_LEN` (16).

**Change**: 
- If [A1] is implemented: the unified buffer handles this (function-global `buf[48]`)
- If [A1] is deferred: reduce to `buf[20]` and only allocate when needed, or move to file-scope static

```cpp
} else if (cmd == F("profile")) {
    char buf[20];  // Just enough for 16-char name + prefix
    args.toCharArray(buf, sizeof(buf));
    cmd_profile(buf);
}
```

**Expected savings**:
- **Stack RAM**: 44 B saved during non-profile commands (most commands)
- **Risk**: Zero — 20 > PROFILE_NAME_LEN + margin

---

#### [A4] Slave Wire/RX buffer sizing — confirm adequacy

| Field | Value |
|-------|-------|
| **Priority** | Medium |
| **File** | `Slave_Transmitter.ino` |
| **Lines** | 16–17 |
| **Difficulty** | Very Low |
| **Risk** | Low |
| **Category** | RAM / Library buffers |

**Analysis**:

```cpp
#define BUFFER_LENGTH 8    // Wire buffer
#define _SS_MAX_RX_BUFF_SIZE 16  // SoftwareSerial RX buffer
```

- `BUFFER_LENGTH 8`: Master sends 6 bytes (mode, channel, cmd, packed, power, pattern). 8 is tight but correct. **Keep**.
- `_SS_MAX_RX_BUFF_SIZE 16`: Only applies when `DEBUG_ENABLE=1`. The slave is a debug receiver (RX pin 0), but primarily transmits debug. 16 bytes is conservative.

**Change**:
- `BUFFER_LENGTH`: Keep at 8 (minimum for 6-byte packets)
- `_SS_MAX_RX_BUFF_SIZE`: Reduce to 8 for debug builds (the `parseSerial()` function reads character-by-character, no buffering needed)

```cpp
#define _SS_MAX_RX_BUFF_SIZE 8   // Was 16 — parseSerial reads char-by-char
```

**Expected savings**:
- **RAM**: 8 B (only when `DEBUG_ENABLE=1`)
- **Risk**: Low — `parseSerial()` reads line-by-line; 8 bytes is enough for "channel N\n" + margin

---

### GROUP B: FLASH OPTIMIZATIONS

---

#### [B1] Consolidate duplicated `F()` string literals

| Field | Value |
|-------|-------|
| **Priority** | **HIGH** |
| **File** | `Master_Controller.ino` |
| **Lines** | 1063, 1067, 1111, 1115, 1141, 1145, 1165, 1169, 1261 |
| **Difficulty** | Low |
| **Risk** | Low |
| **Category** | Flash / String dedup |

**Analysis**:

The `F()` macro creates a unique PROGMEM entry for each instance. The AVR linker does NOT merge identical `F()` strings (unlike regular `.rodata` strings). Several strings are repeated many times, wasting hundreds of bytes of the precious 7 KB free flash:

| String | Occurrences | Char count | Waste |
|--------|:-----------:|:----------:|:-----:|
| `"HW switch is ON - turn off to use software control"` | 5× (L1063,1111,1141,1165,1261) | 47 | 4×47 = **188 B** |
| `"HW sweep is ON - turn switch off to use software control"` | 4× (L1067,1115,1145,1169) | 49 | 3×49 = **147 B** |
| `"Use 5-5000ms"` | 2× (L1375, L1394) | 11 | 11 B |
| `"Usage: pattern burst <on_ms> <off_ms>"` | 2× (L1395, L1396) | 34 | 34 B |
| `" active, ch"` | ~2× (L1321, L1323) | 10 | 10 B |
| Various other minor duplicates | ~ | ~ | ~20 B |

**Total wasted flash: ~410 B** (5.7% of available 7,244 B free)

**Change**:

Replace repeated strings with a single PROGMEM reference:

```cpp
// At top of file or near hw switch handling
static const char STR_HW_ON[] PROGMEM = "HW switch is ON - turn off to use software control";
static const char STR_SWEEP_ON[] PROGMEM = "HW sweep is ON - turn switch off to use software control";

// Usage:
Serial.println(reinterpret_cast<const __FlashStringHelper*>(STR_HW_ON));
```

Or, more practically, use a helper macro/function:

```cpp
static void print_hw_switch_on() {
    Serial.println(F("HW switch is ON - turn off to use software control"));
}
static void print_hw_sweep_on() {
    Serial.println(F("HW sweep is ON - turn switch off to use software control"));
}
```

Then replace all 9 occurrences with the function call. The function call adds ~4 B per call site (rcall) but eliminates the duplicate strings.

**Expected savings**:
- **Flash**: ~400 B net (eliminates 4 redundant copies of each string, minus call overhead)
- **RAM**: No change
- **Risk**: None — pure mechanical replacement

---

#### [B2] Help text compression / PROGMEM efficiency

| Field | Value |
|-------|-------|
| **Priority** | Medium |
| **File** | `Master_Controller.ino` |
| **Lines** | 946–979 |
| **Difficulty** | Low |
| **Risk** | Low |
| **Category** | Flash |

**Analysis**:

The help text at `print_help()` is 34 lines of `Serial.println(F("..."));` calls, totaling approximately 1,200–1,400 bytes of flash. Each `Serial.println(F("s"))` call generates unique PROGMEM storage for each string plus the call overhead.

**Change**:

Option A: Keep as-is — help text is already in F(), flash is acceptable at 76%.

Option B: If more flash is needed later, consolidate common prefixes:

```cpp
// Common prefix pattern detected:
// "adaptive [start|stop|thresh N|intv N]"
// "profile [save|load|delete|list]"
```

Not recommended for immediate action — the help text is valuable for usability.

**Expected savings**: Skip for now (low value).

---

#### [B3] Missing `F()` wrappers on some String comparisons

| Field | Value |
|-------|-------|
| **Priority | Medium |
| **File** | `Master_Controller.ino` |
| **Lines** | 1204, 1217, 1284, 1297, 1364, 1380 |
| **Difficulty** | Very Low |
| **Risk** | Low |
| **Category** | Flash / Correctness |

**Analysis**:

These `String` method calls pass plain string literals without `F()`:

| Line | Code | Issue |
|:----:|------|-------|
| 1204 | `args.startsWith("threshold ")` | Literal used without F() |
| 1217 | `args == "threshold"` | Literal used without F() |
| 1284 | `args.startsWith("thresh")` | Literal used without F() |
| 1297 | `args.startsWith("intv")` | Literal used without F() |
| 1364 | `args.startsWith("pulsed")` | Literal used without F() |
| 1380 | `args.startsWith("burst")` | Literal used without F() |

While these work in practice (on AVR, `String::startsWith(const char*)` reads from the flash-resident literal), using `F()` in `String::operator==()` is explicitly supported by the Arduino core and avoids any ambiguity about whether the literal stays in flash or gets copied.

**Change**:

```cpp
// Before (line 1217):
if (args == "threshold") {
// After:
if (args == F("threshold")) {

// Before (line 1204):
if (args.startsWith("threshold ")) {
// After (cast to __FlashStringHelper*):
if (args.startsWith(F("threshold "))) {
```

Note: `String::startsWith(const __FlashStringHelper*)` IS available in Arduino's String implementation.

**Expected savings**:
- **Correctness**: Ensures string literals are never inadvertently copied to RAM
- **Flash**: ~negligible (same literal, same storage)
- **RAM**: Potential minor saving if compiler was generating runtime copies

---

### GROUP C: CODE STRUCTURE / MAINTAINABILITY

---

#### [C1] Replace magic numbers with named constants in Slave

| Field | Value |
|-------|-------|
| **Priority** | Medium |
| **File** | `Slave_Transmitter.ino` |
| **Lines** | 140, 148, 170, 178, 183, 258, 317, 321, 324 |
| **Difficulty** | Very Low |
| **Risk** | Very Low |
| **Category** | Maintainability |

**Analysis**:

The Slave uses raw integer literals `1, 2, 3, 4` for modes instead of named constants. The Master has well-named `#define` constants (`MODE_SINGLE_CHANNEL`, `MODE_FULL_SPECTRUM`, etc.) but the Slave duplicates the logic without the names. This is a maintenance hazard if mode numbers ever change.

| Magic # | Meaning | Occurrences |
|:-------:|---------|:-----------:|
| 1 | SINGLE_CHANNEL | L140, L170, L178, L183, L258, L317, L324 |
| 2 | FULL_SPECTRUM | L140, L148, L258 |
| 3 | CUSTOM | L140, L258 |
| 4 | SWEEP | L140, L258, L321 |

**Change**:

Add the same `#define` constants from the Master to the Slave:

```cpp
// Add near line 42 of Slave_Transmitter.ino
#define MODE_SINGLE_CHANNEL 1
#define MODE_FULL_SPECTRUM 2
#define MODE_CUSTOM 3
#define MODE_SWEEP 4
#define CMD_START 1
#define CMD_STOP 0
```

Then replace all magic numbers. No code-size change (preprocessor replaces them at compile time).

**Expected savings**: None (zero-cost abstraction), but prevents future bugs.

---

#### [C2] `calc_freq()` — mark as `static inline` or let compiler decide

| Field | Value |
|-------|-------|
| **Priority** | Low |
| **File** | Both files |
| **Lines** | Master L327, Slave L137 |
| **Difficulty** | Very Low |
| **Risk** | None |
| **Category** | Code structure |

**Analysis**:

Both files define `calc_freq()` as a small pure function called from hot paths. On AVR, `inline` is just a hint but combined with `-Os`, the compiler may still inline it.

**Change**: The current `static` is sufficient — the compiler already inlines small `static` functions under `-Os` when profitable. No change needed.

**Expected savings**: None.

---

#### [C3] Remove dead/vacuous forward declarations

| Field | Value |
|-------|-------|
| **Priority** | Low |
| **File** | `Master_Controller.ino` |
| **Lines** | 122–125, 724–725 |
| **Difficulty** | Very Low |
| **Risk** | Very Low |
| **Category** | Code structure |

**Analysis**:

Several functions have forward declarations that are never used or duplicate existing declarations:

```cpp
// Line 122-125: Forward declarations
static bool profile_write(uint8_t idx, const char* name);   // Defined at L135
static bool profile_name_match(uint8_t idx, const char* name); // Defined at L143
static void profile_load(uint8_t idx);                       // Defined at L152
static uint8_t find_profile(const char* name);               // Defined at L158
```

These are all defined in the same file, before they're first used via `cmd_profile()`. Actually, checking more carefully: `profile_write` is called at line 168 from `profile_save()`, which is at line 163, which is BEFORE the `profile_write` definition at line 135. Wait...

Let me re-read the order:
- `profile_save()` at line 163 calls `profile_write()` at line 168
- `profile_write()` is defined at line 135

Actually, `profile_save()` at line 163 comes AFTER `profile_write()` at line 135? No — line 163 > line 135, so `profile_save()` is after `profile_write()`. So the forward declaration at line 122 is NOT needed because `profile_write` is defined at line 135 which is before `profile_save` at line 163.

Wait, let me check the line ordering more carefully:

- L122: `static bool profile_write(uint8_t idx, const char* name);` — forward decl
- L135: `static bool profile_write(uint8_t idx, const char* name) { ... }` — definition

And `profile_save()` at L163 calls `profile_write()` at L168. Since `profile_write` is defined at L135 < L163, the forward declaration at L122 is redundant! The function is already defined before first use.

Similarly:
- L123: `static bool profile_name_match(uint8_t idx, const char* name);`
- L143: `static bool profile_name_match(...) { ... }`
- First call: L160 (`profile_name_match()` in `find_profile()` at L158)

Wait, `find_profile()` at L158 calls `profile_name_match()` at L160. And `profile_name_match` is defined at L143 < L158. So the forward declaration at L123 is also redundant.

- L124: `static void profile_load(uint8_t idx);`
- L152: `static void profile_load(...) { ... }`
- First call: L199 (in `cmd_profile`) or L169 (in `profile_save`) — both after L152. Redundant.

- L125: `static uint8_t find_profile(const char* name);`
- L158: `static uint8_t find_profile(...) { ... }`
- First call: L197 (in `cmd_profile`). Redundant.

Also:
- L724: `static void stop_sweep(void);`
- L725: `static void cancel_sweep(void);`
- Both are defined at L925 and L934 respectively.
- `cancel_sweep` is called from `cancel_adaptive()` at L733 (line 726-737), which is BEFORE L925.
- `stop_sweep` is called from `cancel_adaptive()` at L732.
- So these forward declarations ARE needed — `cancel_adaptive()` calls `stop_sweep()` and `cancel_sweep()` which are defined later.

So actually:
- Lines 122-125: ALL redundant (definitions before first call)
- Lines 724-725: Both needed (calls before definitions)

But we also have:
- L137: `static uint8_t calc_freq(...)` — forward declaration
- L138: definition

Actually looking more carefully, L137-138 shows `calc_freq` declared and immediately defined. Hmm, let me re-read:

Actually line 137-138:
```
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t local_idx, uint8_t group_size);
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t local_idx, uint8_t group_size) {
```

Wait, looking at the slave file:
```cpp
// Line 137-138
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t local_idx, uint8_t group_size);
static uint8_t calc_freq(uint8_t mode, uint8_t ch, uint8_t local_idx, uint8_t group_size) {
```

There's a forward declaration and immediate definition! Line 137 is the same prototype as line 138. This is truly dead code — the forward declaration on line 137 is immediately followed by the definition on line 138. It serves no purpose.

**Change**: Remove lines 122-125 and line 137 in Slave_Transmitter.ino.

**Expected savings**: ~24 bytes flash (8 × 3-byte rjmp/rcall sized entries in symbol table, though compiler likely strips them).

---

#### [C4] Move `lfsr` seed to a constant expression

| Field | Value |
|-------|-------|
| **Priority** | Low |
| **File** | `Slave_Transmitter.ino` |
| **Lines** | 91–92 |
| **Difficulty** | Very Low |
| **Risk** | Very Low |
| **Category** | Code structure |

**Analysis**:

The LFSR state and next macro are well-optimized:
```cpp
static uint8_t lfsr = 0xAC;
#define LFSR_NEXT() (lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB8))
```

This is good — the LFSR polynomial 0xB8 (x⁸ + x⁶ + x⁵ + x⁴ + 1) is a maximal-length polynomial for 8-bit. The `-(lfsr & 1u)` trick branches on the sign bit without a conditional jump.

**Change**: No change needed.

**Expected savings**: None (already optimal).

---

### GROUP D: POTENTIAL FUTURE OPTIMIZATIONS (Beyond this pass)

| ID | Description | Target | Why deferred |
|----|-------------|--------|-------------|
| D1 | Precompute `7 + ch*5` as a macro `#define NRF_CENTER(ch)` | Master | Already cheap (shift+add), compiler optimizes |
| D2 | Move `nrf_scan_init()` duplicated code to shared helper | Master | Small, `nrf_check()` has different setup |
| D3 | Add `volatile` audit for ISR-safe variables | Slave | Already uses cli() protection correctly |
| D4 | Stack depth analysis on Slave | Slave | Requires runtime measurement — see below |

---

## 3. Slave Stack Depth Analysis (Qualitative)

The ATtiny88 has 512 B RAM, 258 B used by globals, **254 B remaining for stack**.

Call chain analysis for the Slave:

```
loop()
  ├─ cli() / SREG save        ~2 B
  ├─ calc_freq()              ~10 B (local vars: freq, offset, center)
  ├─ set_nrf_channel()        ~4 B  (or radio.init() — deeper)
  ├─ radio.init()             ~40 B* (calls initRadio → writeRegister → spiTransfer)
  ├─ slave_set_power()        ~8 B
  ├─ parseSerial()            ~8 B  (only with DEBUG)
  ├─ transmit_noise()         ~16 B (LFSR loop, buf pointer, freq calc)
  │   ├─ calc_freq()          ~10 B
  │   ├─ set_nrf_channel()    ~4 B
  │   └─ radio.send()         ~30 B* (waitForTx → spiTransfer)
  └─ Wire functions (ISR)     ~20 B  (I2C receive/request handlers)
```

**Worst-case stack depth**: ~80–100 B (normal operation) + 20 B (ISR) = ~120 B peak.
**Headroom**: 254 - 120 = ~130 B. Margin is comfortable but not generous.

**Risk factors**:
- Adding new features with large stack arrays (>32 B) would be dangerous
- `radio.init()` is called only on mode change (not every loop iteration), so it's not on the critical hot path

**Recommendation**: Maintain the current union-based shared buffer approach. Any future feature that adds >16 B of local variables should also use a union or static buffer.

---

## 4. Recommended Implementation Order (Phased)

### Phase 1: Highest Impact (implement first)
| Order | ID | Change | Est. saves |
|:-----:|:--:|--------|:----------:|
| 1 | B1 | Consolidate duplicate F() strings | ~400 B flash |
| 2 | A1 | Replace String class with C-string parsing | ~200 B flash + fragmentation fix |

### Phase 2: Low-risk improvements
| Order | ID | Change | Est. saves |
|:-----:|:--:|--------|:----------:|
| 3 | B3 | Add missing F() to String comparisons | Correctness fix |
| 4 | A3 | Reduce `buf[64]` → `buf[20]` in executeCommand | ~44 B stack |
| 5 | A4 | Reduce `_SS_MAX_RX_BUFF_SIZE 16` → 8 | 8 B RAM (debug only) |
| 6 | C1 | Add mode/command constants to Slave | Zero-cost maintainability |

### Phase 3: Housekeeping
| Order | ID | Change | Est. saves |
|:-----:|:--:|--------|:----------:|
| 7 | C3 | Remove redundant forward declarations | ~24 B flash |
| 8 | C2, C4 | Minor code quality tweaks | Negligible |

---

## 5. Summary: Estimated Totals

| Target | Resource | Before | After (est.) | Savings |
|--------|----------|:------:|:------------:|:-------:|
| **Master** | Flash | 23,476 B | ~22,850 B | **~626 B (2.0%)** |
| **Master** | RAM (static) | 560 B | 560 B | 0 B |
| **Master** | RAM (heap/stack) | 1,488 B | ~1,530 B | +42 B available |
| **Slave** | Flash | 3,636 B | ~3,636 B | No change |
| **Slave** | RAM (static) | 258 B | 258 B | 0 B |
| **Slave** | RAM (debug, SS buffer) | 258 B | 250 B | 8 B (debug only) |

**Net flash savings on Master**: ~626 B (from 76% → ~74% utilization)
**Net RAM improvement on Master**: Eliminates String fragmentation risk (the most important fix)

---

## 6. Verification Checklist

After applying changes, verify:

- [ ] `make compile-master` succeeds with no errors
- [ ] `make compile-slave` succeeds with no errors
- [ ] Master flash % is same or lower
- [ ] Slave flash % is same or lower
- [ ] Master RAM % is same or lower
- [ ] Slave RAM % is same or lower
- [ ] All mode-changing commands work: `start`, `stop`, `channel`, `set`, `adaptive`
- [ ] Sweep mode still advances channels correctly
- [ ] Hardware switch positions 1, 2, 3 still function
- [ ] Profile save/load/list/delete round-trips correctly
- [ ] Slave correctly receives I2C commands and transmits noise
- [ ] Slave returns correct jamming status via `requestI2C()`
- [ ] `profile list` displays correctly (EEPROM reads)
- [ ] NRF24L01+ scan still works (`snapshot`, `scan`, `adaptive`)

---

## 7. Current Baseline (for comparison)

### Master_Controller (before any changes):
```
Sketch uses 23476 bytes (76%) of program storage space.
Global variables use 560 bytes (27%) of dynamic memory.
```

### Slave_Transmitter (before any changes):
```
Sketch uses 3636 bytes (53%) of program storage space.
Global variables use 258 bytes (50%) of dynamic memory.
```

---

*End of OPTIMIZE.md — generated by automated code review.*
