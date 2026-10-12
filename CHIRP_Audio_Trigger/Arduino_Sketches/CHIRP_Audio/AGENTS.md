# AGENTS.md — CHIRP Audio Trigger Firmware

Guidance for AI agents reviewing or editing the firmware in this folder. Read this before making changes.

## 1. What this is

Firmware for the **CHIRP Audio Trigger**, a custom **RP2350**-based PCB (Raspberry Pi Pico 2 class MCU) that:

- Reads WAV / MP3 / AAC / M4A files from an **SD card** (SPI1).
- Decodes and **mixes multiple simultaneous streams** (default 3) and outputs **16-bit stereo 44.1 kHz** over **I2S** to a PCM5102A-style DAC.
- Streams audio directly from the SD card across all sound banks with near-zero latency; prototype-era SD-to-Flash syncing for Bank 1 has been retired.
- Is controlled over **UART (Serial2)** by a droid's main controller (e.g. an ESP32), plus USB CDC (`Serial`) for debug. Also accepts legacy **SparkFun/Robertsonics MP3 Trigger** commands.
- Can expose the SD card to a PC as a **USB Mass Storage device (MSC)**.
- Speaks **voice feedback** (e.g. "baud rate setting 96 0 0") using small MP3 clips embedded in program flash.

Primary use case is an Astromech droid sound system. See `../../README.md` for user-facing docs (SD layout, commands, buttons).

## 2. Build environment

- **Toolchain:** PlatformIO (`platformio.ini` in this folder, `src_dir = .` — every `.cpp` here is compiled).
  - Platform: `maxgerhardt/platform-raspberrypi`, board `rpipico2`, **Earle Philhower arduino-pico core pinned to 5.4.4**.
  - CPU 250 MHz, `-Ofast`, `USE_TINYUSB`, 16 MB flash with **14 MB LittleFS**, **8 MB PSRAM** (CS = GPIO 0).
  - Libraries: `arduino-libhelix` v0.9.2 (MP3/AAC decoders), `CRC32`, `Adafruit NeoPixel`. SdFat, LittleFS, I2S, TinyUSB come from the core.
- Arduino IDE equivalents are referenced in log messages ("2MB Sketch, 14MB FS", "Fast (-Ofast)"). Keep `platformio.ini` and those settings consistent.
- Build check: `pio run` from this folder. There are **no automated tests**; the only validation available to agents is a successful compile. Behavioral changes must be tested on hardware by the user — say so when relevant.
- Do not change pinned versions (core, libhelix) without being asked; audio timing is sensitive to them.

## 3. File map

| File | Responsibility |
|---|---|
| `config.h` | **Single shared header.** Pins, compile-time flags, limits, all structs (`AudioStream`, `RingBuffer`, `SoundFile`, `SDBank`, `MP4Parser` class, serial queue), `extern` globals and **all cross-file function prototypes**. Every `.cpp` includes only this (plus `system_audio_data.h` in `file_management.cpp`). |
| `globals.cpp` | Definitions of most globals (`sd`, `i2s`, bank tables, mutexes, `log_message()`). |
| `main.cpp` | `setup()` / `loop()` on **Core 0**. Boot sequence, button handling, auto-stop of finished streams, debug stats. Re-initializes `Serial2` immediately after `parseIniFile()` to apply configured baud rate on boot. Header comment documents CHIRP.INI keys and commands. |
| `audio_playback.cpp` | Audio engine. `initAudioSystem()`, `fillStreamBuffers()` (Core 0 producer: read file → decode → ring buffer), `Mixer::processSample()` and `setup1()/loop1()` (**Core 1** consumer → I2S), MP3/AAC decoder callbacks with resampling, `startStream()`, `startProgmemStream()`, `stopStream()`, chirp/sweep tone generator `playChirp()`. Also owns stream/decoder config globals (`maxStreams`, `streamBufferSize`, …). |
| `file_management.cpp` | CHIRP.INI parse/write, firmware-version check (LittleFS `/version.txt`), SD scanning (Bank 1 pages, Bank 1 sounds + variant grouping, Banks 2–6, root tracks with 64-char buffers), retired Bank 1 flash sync stub, format helpers, and all **voice-feedback** helpers (`playVoiceFeedback`, `playVoiceNumber`, `play*Feedback`). |
| `serial_commands.cpp` | CHIRP text command parser/dispatcher (`processSerialCommands`) with UART silence reset (150 ms) to purge line noise, and handlers (`handlePlay`, `handleStop`, `S`/`P` aliases, …). |
| `mp3_compat.cpp` | Legacy MP3 Trigger non-blocking state machine parser (`checkAndHandleMp3Command`) for 1-byte (`O`,`F`,`R`) and 2-byte (`t`,`T`,`v`,`p`) commands with 500 ms safety timeout, smart Bank 1 fallback in `action_playTrackById()`, and `action_*` functions used by both legacy commands and the physical buttons. |
| `serial_queue.cpp` | Outgoing ring queue for Serial2 replies; `isCpuBusy()` throttle used by queue and LEDs. |
| `msc_interface.cpp` | USB Mass Storage (TinyUSB) start/stop, sector callbacks, GPIO 7 trigger polling. (No `.h`; prototypes in `config.h`.) |
| `mp4_parser.cpp` | Minimal MP4/M4A atom parser (`moov/trak/stsd/stsz/stco/stsc`); emits each AAC frame prefixed with a synthesized 7-byte **ADTS header** so Helix can decode it. |
| `blinkies.cpp` | 3 NeoPixels on GPIO 19: startup, SD-error (halts forever), sync heartbeat, runtime (per-stream color: blue=WAV, green=MP3, orange=AAC/M4A; idle Cylon scan; MSC scan). |
| `system_audio_data.h/.cpp` | **Auto-generated.** Embedded MP3 voice clips (`sysAudioFiles[]` lookup by basename). The `.cpp` is ~3 MB of hex — **never open/read it in full, never hand-edit it.** |
| `config.h.bak` | Stale backup (only differs by `DEBUG` enabled). Ignored by git (`*.bak`). Do not edit. |

### Regenerating system voice clips
Run `CHIRP_Audio_Trigger/Sounds/convert_system_audio.py`. It reads `Sounds/0_system/*.mp3` (recommended: mono, 22.05 kHz, 16 kbps) and writes `system_audio_data.h/.cpp` into **`Sounds/CHIRP_Audio/`** — those outputs must then be copied into this folder. Clips are looked up by filename without extension (e.g. `"baud_rate"`, `"0042"`, `"_b"`).

## 4. Architecture

### Dual-core model (most important concept)

```
Core 0 (setup/loop, main.cpp)                    Core 1 (setup1/loop1, audio_playback.cpp)
-----------------------------                    -----------------------------------------
serial cmds, buttons, MSC, LEDs                  while(true):
fillStreamBuffers():                               if g_allowAudio: i2s.begin once,
  SD/flash/PROGMEM read  ──► Helix decode ──►        Mixer::processSample()  (blocking i2s.write16)
  resample/mono→stereo   ──► RingBuffer.push      else: i2s.end, delay(1)
                              (per stream) ──────► RingBuffer.pop (L,R) × volume × master → sum → clip
                                                    + chirp sine LUT generator
```

- Each stream has a **single-producer (Core 0) / single-consumer (Core 1) lock-free ring buffer** of `int16_t`, **interleaved stereo at 44.1 kHz**. Every producer must push **L,R pairs**; mono and 22.05 kHz sources are expanded before pushing. Buffer sizes must be **powers of two** (mask wrapping).
- Core 1 does nothing but mixing; I2S writes block and pace it. **Never** add file I/O, serial, `delay()`, logging, or allocation to the Core 1 path. Keep `Mixer::processSample()` integer-cheap.
- `g_allowAudio` gates whether Core 1 runs the I2S peripheral (used for software mute on Rev.A and during boot).
- All decoding happens on Core 0. Helix callbacks don't pass user context through, so the global **`currentDecodingStream`** is set immediately before each `decoder->write()` and reset after. Preserve this pattern; decoders are not re-entrant.

### Memory
- Normal stream ring buffers and Helix decoders live in **PSRAM** (`pmalloc` + placement `new`). Default 512 KB/stream; ~25 KB per MP3 decoder, ~70 KB per AAC decoder.
- **Stream 0** is special: a fixed **64 KB internal-RAM buffer** (32768 samples, its own `bufferMask`), intended for the system voice / PROGMEM stream. Code that assumes every stream uses `streamBufferSize`/`streamBufferMask` is wrong for stream 0.
- Decoder pools: `maxMp3Decoders` MP3 + the same count of AAC decoders (AAC pool reuses `maxMp3Decoders`). Both scale with `MAX_STREAMS`.
- RevA boards have only 2 MB PSRAM; RevB 8 MB. Be conservative with defaults.

### Stream types (`StreamType`)
`WAV_FLASH, WAV_SD, MP3_SD, MP3_FLASH, MP3_PROGMEM, AAC_SD, AAC_FLASH, M4A_SD, M4A_FLASH`. Path prefix `/flash/` means LittleFS, otherwise SD. Format is chosen purely by **file extension** (`getAudioFormat`). When adding a type, update: `startStream`, `fillStreamBuffers`, `stopStream` (decoder release + file close), MSC safety checks, `isCpuBusy`, and LED colors.

### Thread safety
- `sd_mutex`, `flash_mutex`, `log_mutex` (pico SDK, **non-recursive**). Wrap every SdFat / LittleFS access in the matching mutex.
- `writeIniFile()` takes `sd_mutex` itself — **callers must not hold it** (deadlock). `parseIniFile()` releases before calling it.
- `MP4Parser` methods expect the **caller** to hold the appropriate mutex.
- `log_message()` is the mutex-protected logger; many places still use `Serial.print*` directly.

### Boot sequence (`setup()`)
LEDs → USB Serial / Serial2 → SPI1 pins → buttons, MSC pin → Serial2 queue → SD init (25 MHz, fallback 4 MHz) → LittleFS mount → **parse CHIRP.INI** (sets `maxStreams`, buffer size, baud, etc., so it must precede allocation) → **Serial2 re-initialized at parsed baudRate** (`Serial2.end(); Serial2.begin(baudRate);`) → firmware version check (rewrites INI if changed) → `initAudioSystem()` → allocate decoders in PSRAM → if SD failed: `playErrorSequence()` (**never returns**) → scan Bank 1 pages/sounds → scan Banks 2–6 → filename CRC32 → scan root tracks → unmute. Boot completes in <200 ms.

### Blocking voice feedback
`playVoiceFeedback()` plays an embedded clip on **stream 0** and **blocks Core 0**, manually pumping `fillStreamBuffers()` and auto-stop until done. Anything that calls it (button combos, BAUD/BPAGE/MUSB/CCRC handlers) pauses serial handling and other stream refills meanwhile. It also interrupts whatever was on stream 0.

## 5. SD card / data model
- **Bank 1** (`1<Page>_Name`, e.g. `1A_R2D2`): active page from INI `BANK1_PAGE`. Files are grouped into `SoundFile`s: if a filename has `_` followed by a digit, the text before the **first `_`** (≤15 chars) is the basename and files sharing it become **variants** (max 25) chosen randomly without immediate repeats. Otherwise each file is its own sound. Max `MAX_SOUNDS` (100). All Bank 1 audio streams directly from SD; prototype flash syncing (`#USE_FLASH_BANK1`) is retired.
- **Banks 2–6** (`2_Name` or `2B_Name`): flat file lists in `SDBank` (max `MAX_SD_BANKS`=20 dirs, `MAX_FILES_PER_BANK`=100, names < 64 chars). Order = SdFat directory order (not sorted).
- **Root tracks**: audio files in SD root, sorted alphabetically, names up to 63 chars (`MAX_ROOT_TRACK_NAME_LEN 64`), used by legacy commands and buttons.
- **Manifest checksum**: CRC32 of all Bank 1 variant names + Bank 2–6 filenames, reported in `GMAN` as `MSUM` so the host can detect changes.
- **CHIRP.INI** (SD root, lines starting with `#KEY value`): `BANK1_PAGE`, `BAUD_RATE`, `MAX_STREAMS` (1–10), `STREAM_BUFFER_SIZE` (SMALL/MEDIUM/LARGE/KB, rounded down to power of 2, 32–4096), `LEGACY_MONOPHONIC`, `VERSION`. `writeIniFile()` regenerates the whole file — if you add a key, add it to **both** parse and write.

## 6. Serial protocol

Lines terminated by `\r` or `\n`, max 127 chars. Same parser for USB `Serial` and UART `Serial2`. Replies via `sendSerialResponse*()`: **immediate on USB, queued on Serial2** (`trySendQueuedMessages`, deferred while `isCpuBusy()`; queue drops oldest when full). Note many error replies call `serial.println()` directly and bypass the queue.
On `Serial2`, a 150 ms silence timeout discards any stale un-terminated line buffer fragments to purge line noise.

| Command | Reply (key lines) |
|---|---|
| `PLAY:index[,bank[,page[,vol]]]` (bank default 1, page default A, vol 0–99) | `PACK:PLAY`, `S:<stream>,ply,<vol>` / `ERR:...` |
| `STOP` / `STOP:*` / `STOP:<stream>` / `S` | `PACK:STOP`, `S:<n>,idle,,0` |
| `P` | Toggle play/pause (last played track) |
| `VOL:<vol>` (all) / `VOL:<stream>,<vol>` | `PACK:SVOL` |
| `CHRP:startHz,endHz,ms[,vol0-255]` | `PACK:CHRP` |
| `GMAN` | `MDAT:<n>`, `BANK:<num>,<dir>,<count>`…, `MSUM:<crc>`, `MEND` |
| `GNME:bank,page,index` | `NAME:bank,page,index,<file>` (Bank 1 always appends `.wav`) |
| `LIST`, `STAT:<stream>` | human-readable / `STAT:playing,<file>,<vol>` |
| `BAUD:<rate>`, `BPAGE:<A-Z>` | persisted to INI; BPAGE needs reboot |
| `MUSB` / `MUSB:1` / `MUSB:0`, `CCRC`, `PVOICE:<clip>` | `PACK:...` |

Stream selection for PLAY: first inactive stream, else **steals stream 0**. Volume mapping: protocol 0–99 → `float 0..1`; legacy `v` 0 (loud)–255 (silent); `masterAttenMultiplier` is 0–256 fixed-point.

**Legacy MP3 Trigger commands** are handled by a dedicated non-blocking state machine per stream (`checkAndHandleMp3Command` in `mp3_compat.cpp`):
- **1-byte navigation**: `O` (play/pause toggle), `F` (next track), `R` (previous track).
- **2-byte control**: `t` (binary track 0–255), `T` (ASCII track '0'–'9'), `v` (binary volume 0–255), `p` (binary track index 0–255).
- If the second byte does not arrive within **500 ms**, the parser safely resets back to idle.
- **Smart Bank 1 Fallback**: When playing by track ID (`t` or `T`), if the track is not found in SD root (or if `rootTrackCount == 0`), the engine automatically searches active Bank 1 (`1A_...`) for matching numeric prefixes (e.g. `001_` or `001.`) or 1-based indices, so SparkFun-formatted SD cards and CHIRP-formatted cards both work seamlessly.
- Single-letter commands `S` (STOP) and `P` (Play/Pause) are also supported when line-terminated.

## 7. Hardware notes (`config.h`)
- SD on SPI1: CS 13, MISO 12, MOSI 15, SCK 14. I2S: BCLK 9, LRCK 10, DATA 11. UART TX 4 / RX 5. NeoPixels 19. Buttons (active-low, pull-up): Play/Stop 17, Next 16, Prev 18. MSC trigger GPIO 7 (pull low).
- `BOARD_REV_B` defines `I2S_MUTE_PIN 8` (DAC XSMT, **HIGH = unmuted**). On Rev.A, GPIO 8 is PSRAM CS — never drive it; muting there is done by stopping I2S (`g_allowAudio=false`).
- Bank 1 SD→flash syncing and obsolete dev settings (`MUTE_DURING_SYNC`, `DEV_MODE`, `DEV_SYNC_LIMIT`, `useFlashForBank1`, `syncBank1ToFlash`) have been completely retired.

## 8. Conventions
- C++ in Arduino style; `-fno-exceptions -fno-rtti`. Prefer fixed-size `char[]` + `snprintf/strncpy` over `String` in hot paths (`String` is tolerated in logging).
- New cross-file functions/globals: prototype/`extern` in `config.h`, definition in the owning `.cpp`.
- Section banners `// ===...===` separate functions; keep existing comments.
- Debug-only code goes under `#ifdef DEBUG` (disabled by default in `config.h`).
- **Bump `VERSION_STRING` (`YYYYMMDD`)** for releases: a change triggers the spoken "firmware updated, new version YY MM DD" on next boot. Ask the user before bumping.
- Keep RAM/PSRAM use and Core 0 loop latency in mind; MP3 refill only happens when a buffer has >16384 free samples, WAV >2048.

## 9. Known quirks / potential bugs (verify before "fixing"; confirm with user)
- `isCpuBusy()` compares against `streamBufferSize/4` for every stream; for stream 0 (64 KB buffer) playing MP3 from SD, this threshold exceeds capacity, so it would always report busy (suppressing Serial2 replies and LED updates). Debug buffer stats in `main.cpp` have the same stream-0 assumption.
- MSC safety checks (`startMSC`, `fillStreamBuffers`) only consider `WAV_SD`/`MP3_SD`, not `AAC_SD`/`M4A_SD`.
- `.ogg` is recognized by `getAudioFormat`/`isAudioFile` but has no decoder; `startStream` would treat it as WAV.
- `startStream` for flash audio (`*_FLASH` types) is now dormant/dead code for Bank 1 since all sound banks stream directly from SD card; LittleFS is retained only for `/version.txt`.
- `mp3DataCallback` spin-waits (up to 100 ms per sample pair) for buffer space, blocking Core 0; `aacDataCallback` drops samples instead.
- MP4 parser: 64-bit atom sizes truncated; STSC rescanned per chunk (TODO in code); ADTS profile hard-coded to AAC-LC.
- Mixer fade-in calls `millis()` per sample on Core 1.
- `calculateGlobalChecksum()` in `main.cpp` is unused (logic is duplicated inline in `setup()`).
- Legacy 2-byte serial commands (`t`, `T`, `v`, `p`) timeout after 500 ms if the argument byte is abandoned by the host, safely resetting back to idle.

## 10. Agent checklist
1. Read `config.h` first; it defines the contract between files.
2. Decide which core your change runs on. Core 1 code must be real-time safe.
3. Respect mutex rules and the `currentDecodingStream` pattern.
4. Keep ring-buffer data interleaved stereo 44.1 kHz.
5. Don't read/edit `system_audio_data.cpp`; don't touch `config.h.bak`.
6. Update `../../README.md` and the `main.cpp` header comment if you change commands, INI keys, or SD layout rules.
7. Compile (`pio run`) if possible, and tell the user what needs hardware testing.
