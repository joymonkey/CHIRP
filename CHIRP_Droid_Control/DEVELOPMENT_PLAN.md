# CHIRP Droid Control — ESP32-S3 Development Plan & Roadmap

## Executive Overview

**CHIRP Droid Control** is an advanced control coordinator for an Astromech droid (R2-D2 style). Built around the **Espressif ESP32-S3** (Waveshare ESP32-S3 Zero and standard ESP32-S3 DevKit boards), it operates via an **ExpressLRS / Crossfire** radio link from an EdgeTX transmitter (initially targeting the **Radiomaster Zorro**, with a path to support other EdgeTX radios such as the TX16S, Boxer, and Pocket). It controls foot drive motors (via standard PWM or digital telemetry with Roboteq/VESC brushless controllers), dome rotation, lighting systems, and links seamlessly with the CHIRP_Audio sound trigger.

### Core Philosophy: Safety & Operator Feedback First

1. **Deterministic Safety & Failsafes**:
   Astromech droids are heavy, powerful robots that operate in close proximity to crowds, children, and convention floors. Unintended motion is unacceptable. CHIRP Droid Control enforces safety at the firmware level:
   - **Boot-Disarmed Interlock**: Foot drive motion is strictly disarmed on boot. Even if the transmitter powers up or connects with the arming switch (`SE`) already in the "Armed" position, the firmware ignores it until the operator intentionally cycles the switch to Disarmed and then to Armed.
   - **Slow-on-Arm Speed Interlock**: Whenever the operator switches `SE` from Disarmed to Armed, drive speed **always begins in `Slow` (30%)**, even if the speed switch (`SB`) was left in `Medium` or `Fast`. The droid remains in `Slow` until the operator intentionally moves `SB`.
   - **Zero-Latency Neutral Failsafe**: Instant loss of radio packets immediately drives all motor PWM signals to neutral (1500µs / 0 velocity).
   - **Hardware Safety Isolation**: Real-time motion and failsafes run on dedicated FreeRTOS tasks pinned to Core 1, completely isolated from serial processing, network stacks, or audio syncing.

2. **Operator Feedback & The EdgeTX Telemetry Dashboard**:
   A droid control system is only as good as the operator's situational awareness. Providing rich, live feedback directly on the transmitter screen is a flagship capability of CHIRP:
   - The companion **EdgeTX Lua script is an equal-partner subsystem**, not an afterthought.
   - Live sound names, bank and page indices, real-time battery voltages, arming status, and speed presets are transmitted back from the droid over CRSF telemetry frames and displayed directly on the transmitter's screen.

This document outlines the phased development roadmap, initial control layout, hardware architecture, and verification checklists.

---

## Hardware Architecture & Pin Budget

### Target Board Specifications (Waveshare ESP32-S3-Zero)
- **SoC**: Espressif **ESP32-S3FH4R2** Dual-Core Xtensa LX7 @ 240 MHz
- **Memory**: **512 KB SRAM**, **4 MB Quad-SPI Flash**, **2 MB Quad-SPI PSRAM** (`qio_qspi` memory mode, `default.csv` 4MB partition layout)
- **Onboard Status LED**: WS2812B RGB LED on `GP21` (uses **`NEO_RGB`** color byte order)

### Waveshare ESP32-S3-Zero Physical Thru-Hole Pinout

Every essential and auxiliary connection is mapped **exclusively to the standard 2.54mm thru-hole / castellation pins**. No soldering to bottom surface-mount pads (`GP17, 18, 38–42, 45`) or underside test pads (`GP14–16`) is required.

> [!IMPORTANT]
> **Boot Safety & ADC1 Hardware Rules**:
> 1. **Strapping Pin Isolation (`GP3`)**: On the ESP32-S3, `GP3` is a hardware boot-strapping pin (`JTAG_SEL`). Connecting an external UART device to `GP3` can pull the pin high during power-on reset and cause boot hangs. Therefore, **`GP3` is intentionally isolated as an unassigned Spare/Aux Output**, and all UARTs are mapped to 100% clean non-strapping GPIOs.
> 2. **ROM Bootloader Isolation (`GP4/GP5` for CRSF)**: `TX (GP43)` and `RX (GP44)` are the ESP32-S3's ROM bootloader console pins (`UART0`), which emit 115200-baud boot text during power-up. To prevent ROM bootloader traffic from interfering with the ExpressLRS receiver at boot, **ExpressLRS / CRSF (`Serial1`) is mapped to pure GPIOs `GP4 (TX)` and `GP5 (RX)`**, while `GP43/GP44` (`Serial0`) is assigned to the Roam-A-Dome.
> 3. **Wi-Fi / ESP-NOW Safe ADCs (`ADC1`)**: On the ESP32-S3, `ADC2` (`GP11–GP20`) is shared with the Wi-Fi radio and cannot be read while Wi-Fi or ESP-NOW is active. All three battery voltage monitors are strictly placed on **`ADC1` (`GP8, GP9, GP10`)** so analog voltage sensing works reliably alongside Phase 4 wireless features.

#### Left Header (Top to Bottom)
| Physical Pin | Label | Function | Connected Peripheral / Signal |
| :--- | :--- | :--- | :--- |
| Pin 1 | `5V` | Power In | 5V DC Supply / USB VBUS |
| Pin 2 | `GND` | Ground | Common System Ground |
| Pin 3 | `3V3` | 3.3V Out | 3.3V Regulated Output |
| Pin 4 | `GP1` | HW UART 2 TX (`Serial2`) | **CHIRP_Audio TX** (@ 115200 baud) |
| Pin 5 | `GP2` | HW UART 2 RX (`Serial2`) | **CHIRP_Audio RX** (@ 115200 baud) |
| Pin 6 | `GP3` | Spare / Aux Output | *Strapping pin (`JTAG_SEL`) — kept free of UART pull-ups for boot safety* |
| Pin 7 | `GP4` | HW UART 1 TX (`Serial1`) | **ExpressLRS / CRSF Telemetry TX** (@ 420000 baud $\rightarrow$ Receiver RX) |
| Pin 8 | `GP5` | HW UART 1 RX (`Serial1`) | **ExpressLRS / CRSF Receiver RX** (@ 420000 baud $\rightarrow$ Receiver TX) |
| Pin 9 | `GP6` | PWM Output | **Left Foot Motor PWM** (50Hz RC, 1000–2000µs) |

#### Right Header (Top to Bottom)
| Physical Pin | Label | Function | Connected Peripheral / Signal |
| :--- | :--- | :--- | :--- |
| Pin 1 | `TX` (`GP43`) | HW UART 0 TX (`Serial0`) | **Roam-A-Dome (RAD) TX** (@ 9600 baud) |
| Pin 2 | `RX` (`GP44`) | HW UART 0 RX (`Serial0`) | **Roam-A-Dome (RAD) RX** (@ 9600 baud) |
| Pin 3 | `GP13` | SoftSerial RX | **Dome Lighting (RSeries Logic) RX** (@ 9600 baud) |
| Pin 4 | `GP12` | SoftSerial TX | **Dome Lighting (RSeries Logic) TX** (@ 9600 baud) |
| Pin 5 | `GP11` | PWM Output | **Dome Motor PWM** (50Hz RC, 1000–2000µs) |
| Pin 6 | `GP10` | `ADC1_CH9` | **Battery 3 Voltage ADC** (Aux / Dome) |
| Pin 7 | `GP9` | `ADC1_CH8` | **Battery 2 Voltage ADC** (Audio) |
| Pin 8 | `GP8` | `ADC1_CH7` | **Battery 1 Voltage ADC** (Primary 12V/24V) |
| Pin 9 | `GP7` | PWM Output | **Right Foot Motor PWM** (50Hz RC, 1000–2000µs) |

*(Note: Placing Left and Right Foot Motor PWMs symmetrically at the bottom of the Left and Right headers on `GP6` and `GP7` also allows those same two pins to be dynamically repurposed as CAN bus `TWAI TX/RX` or Serial `TX/RX` in Phase 5 if digital motor controllers are investigated in the future.)*

#### Internal / Native (No External Wiring Needed)
| Pin | Function | Purpose |
| :--- | :--- | :--- |
| `USB Port` (`GP19/20`) | Native USB CDC | Host PC debug logging & serial monitor (`Serial`) |
| `GP21` (Onboard) | WS2812B RGB LED (`NEO_RGB`) | Onboard Status & Heartbeat Pixel |

---

## Radiomaster Zorro Control Mapping (Initial Profile)

The transmitter configuration is tailored to the physical ergonomics of the **Radiomaster Zorro**, providing single-operator control of driving, dome positioning, sound triggers, and mode selection:

### Physical Controls & Channel Assignments

| Physical Control on Zorro                                                                                                                              | Function                                                              |            Channel            | Operational Behavior & Range                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| :----------------------------------------------------------------------------------------------------------------------------------------------------- | :-------------------------------------------------------------------- | :---------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Gimbal 1 (Right Stick)**                                                                                                                             | **Foot Drive**                                                        |     **CH1 (X) & CH2 (Y)**     | **Arcade mixing**: Y-axis forward/reverse throttle; X-axis differential steering. Deadbands & speed scaling applied.                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| **Gimbal 2 (Left Stick Y)**                                                                                                                            | **Reserved / Unassigned**                                             |            **CH3**            | Unused vertical axis on left gimbal (available for future accessory or tilt axis).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| **Gimbal 2 (Left Stick X)**                                                                                                                            | **Dome Rotation**                                                     |            **CH4**            | **Manual Yaw velocity**: CW/CCW dome speed output (1000–2000µs). Stops at 1500µs neutral.                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| **2-Position Switch (`SE`)**                                                                                                                           | **Arm / Disarm**                                                      |            **CH5**            | **Safety Interlock**: Dedicated ExpressLRS arming channel.<br>• *Disarmed* (~1000µs): Foot drive PWMs locked at 1500µs neutral.<br>• *Armed* (~2000µs): Foot drive enabled.<br>• **Boot Protection**: Always boots disarmed. Even if the radio turns on with `SE` in "Armed", the operator must cycle it to Disarmed first to activate drive.                                                                                                                                                                                                                       |
| **3-Position Switch (`SB`)**                                                                                                                           | **Drive Speed Mode**                                                  |            **CH6**            | Sets maximum throttle scaling for foot drive:<br>• **Slow (Pos 1)**: ~30% max speed (crowds / tight indoor spaces)<br>• **Medium (Pos 2)**: ~70% max speed (standard cruising)<br>• **Fast (Pos 3)**: 100% full speed (outdoor sprint)<br>• **Slow-on-Arm Protection**: Transitioning `SE` from Disarmed to Armed always begins in **Slow (30%)** until `SB` is moved.                                                                                                                                                                                                     |
| **3-Position Switch (`SC`)**                                                                                                                           | **Roam-A-Dome Auto Mode**                                             |            **CH7**            | Sets autonomous dome movement frequency:<br>• **Off (Pos 1)**: Manual rotation only<br>• **Less Frequent (Pos 2)**: Relaxed / intermittent auto movements<br>• **More Frequent (Pos 3)**: Active / lively animated head movement                                                                                                                                                                                                                                                                                                                                    |
| **Potentiometer Dial `S2`**                                                                                                                            | **Master Volume**                                                     |            **CH8**            | Continuous volume scaling from 0 to 99% (maps directly to `VOL:<vol>` on CHIRP_Audio).                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **Sound Playback System**<br>• **Dial `S1`** (Sound Scroll)<br>• **Page Control** (Page A/B/C)<br>• **Momentary Buttons `SD, SH, SA, SG`** (Banks 1–4) | **Universal Sound Trigger**<br>*(CRSF Direct Command + CH9 Fallback)* | **CRSF `0x32`**<br>*(or CH9)* | **Dual-Capable Trigger Architecture**:<br>• Scrolling Dial `S1` and changing Pages happens locally in the Lua UI (**0 radio packets sent while browsing**).<br>• **Primary (Lua Direct CRSF)**: Pressing **`SD` (Bank 1)**, **`SH` (Bank 2)**, **`SA` (Bank 3)**, or **`SG` (Bank 4)** sends a CRC-verified CRSF command packet (`0x32`) via `crossfireTelemetryPush` containing the exact `(Bank, Page, Track)` bytes.<br>• **Fallback (CH9 Pulse)**: For non-Lua profiles, CH9 supports deterministic pulse encoding (`1000µs` idle, `1050–2000µs` active range). |
| **Auxiliary Channels**                                                                                                                                 | **Expansion / Accessories**                                           |         **CH10–CH16**         | Completely free for dome lighting animations (RSeries Logic Engine), periscope/arm servos, or future accessories.                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **2-Position Switch (`SF`)**                                                                                                                           | **Aux / Reserved**                                                    |               —               | Unassigned / available for future features (e.g. periscope / smoke).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| **Digital Trims (`T1–T4`)**                                                                                                                            | **Aux / Reserved**                                                    |               —               | Unassigned / available for future lighting routine or accessory triggers.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |

---

### Custom Mobile-Robot CRSF Engine (`CrsfLink`) & Sound Trigger Architecture

Drone-oriented CRSF libraries (such as `AlfredoCRSF`) are built around flight controllers: they only parse incoming RC channels and link stats, ignore incoming handset command frames (`0x32`), and force mobile robots to disguise telemetry inside fake GPS and flight-mode packets.

CHIRP Droid Control replaces external CRSF libraries with a purpose-built, single-pass **`CrsfLink`** driver designed specifically for astromech droids and bidirectional EdgeTX Lua integration:

#### 1. Primary Sound & Config Transport: Bidirectional CRSF Command Frames (`0x32`)
- **Local Handset Navigation (Zero RF Overhead)**:
  - As the operator rotates dial `S1`, the Lua script scrolls through the cached sound list on the Zorro's 128x64 display.
  - No packets are sent over the radio while browsing.
- **Direct Binary Trigger on Button Press (`SD` / `SH` / `SA` / `SG`)**:
  - When the operator presses one of the 4 physical momentary buttons on the Zorro (`SD` = Bank 1, `SH` = Bank 2, `SA` = Bank 3, `SG` = Bank 4), the EdgeTX Lua script calls:
    ```lua
    -- Frame 0x32 (CRSF Command): [Dest=0xC8, Src=0xEA, Cmd=0xA1 (PLAY), Bank, Page, Track]
    crossfireTelemetryPush(0x32, { 0xC8, 0xEA, 0xA1, bank, pageIdx, trackIdx })
    ```
  - Pressing `SA + SD` together sends `Cmd=0xA2 (STOP_AUDIO)` to immediately halt audio playback.
  - Adjusting droid settings in the Lua menu sends `ParamID (1..10)` with `[ParamID, Value]`.
- **Why Direct CRSF Command Frames Are Superior**:
  - **Zero Quantization or Threshold Errors**: Sends exact binary integers `(Bank, Page, Track)` protected by an 8-bit hardware CRC (`0xD5` polynomial).
  - **Zero EdgeTX Mixer Overhead**: No need to configure Global Variables (`GV1`) or custom channel mixers on the radio just to pass data from Lua into an RC channel.
  - **Unlimited Scalability**: Supports up to 255 banks $\times$ 255 pages $\times$ 255 tracks without analog resolution limits.

#### 2. Universal Fallback: Single-Channel Deterministic Encoding (`CH9`)
To remain controller-agnostic (supporting basic transmitters without Lua scripts or future external controllers), `CrsfLink` also supports single-channel pulse decoding on **CH9** with a corrected deadband window so all 288 addresses (`4 Banks × 3 Pages × 24 Tracks`) trigger cleanly:
- **Coordinate Formula** ($\text{Sound ID} \in [1, 288]$):
  $$\text{Sound ID} = (\text{Bank} - 1) \times 72 + (\text{Page}) \times 24 + \text{Track}$$
- **Shifted Active Pulse Band (`1050µs` – `2000µs`)**:
  - **Idle State**: CH9 sits at **`1000µs`** (any value `< 1025µs` is treated as idle/reset).
  - **Trigger Pulse** (held for `100ms`):
    $$\text{Pulse } (\mu\text{s}) = 1050 + \left(\text{Sound ID} - 1\right) \times \frac{950}{287} \approx 1050 + (\text{Sound ID} - 1) \times 3.31\mu\text{s}$$
  - Because `Sound ID = 1` starts at **`1050µs`** (well above the `1025µs` trigger threshold), Tracks 1–5 in Bank 1 Page A never collide with the idle deadband.

---

## FreeRTOS Dual-Core Architecture

```mermaid
graph TD
    subgraph Core 1 ["Core 1: Real-Time Deterministic Control (High Priority)"]
        CRSF_IN["CrsfLink Receiver (UART1 GP4/5 @ 420k)"] --> PARSE["RC & Command Frame Processing"]
        PARSE --> FAILSAFE{"Link OK & Armed?"}
        FAILSAFE -->|Yes| MIX["Arcade Foot Drive Mixer"]
        FAILSAFE -->|No| STOP["Failsafe Stop (1500µs neutral)"]
        MIX --> MOTORS["Foot PWMs (GPIO 6 & 7)"]
        PARSE --> DOME["Dome Rotation Controller"]
        DOME --> RAD_PWM["Dome PWM (GPIO 11)"]
        RAD_UART["Roam-A-Dome (UART0 GP43/44 @ 9600)"] <--> DOME
        DOME_LIGHTS["RSeries Logic Engine (SoftSerial GP12/13 @ 9600)"] <--- PARSE
    end

    subgraph Core 0 ["Core 0: Communications & Background Services (Normal Priority)"]
        AUDIO_LINK["CHIRP_Audio Link (UART2 GP1/2 @ 115200)"]
        TELEM["CrsfLink Telemetry Uplink Engine"]
        ADC_MON["Battery ADC1 Monitor (GP8, GP9, GP10)"]
        ESPNOW["ESP-NOW Engine (WCB & Wireless Dome)"]
        WEB["Async Web Server (Phone Config Portal)"]
        NVS["Storage Manager (Preferences / NVS)"]
    end

    SHARED_STATE["Thread-Safe Shared State (robotState)"]
    CMD_QUEUE["FreeRTOS Command Queue"]

    PARSE -.->|Audio & Config Events| CMD_QUEUE
    CMD_QUEUE -.-> AUDIO_LINK
    CMD_QUEUE -.-> NVS
    SHARED_STATE <--> Core 1
    SHARED_STATE <--> Core 0
```

---

## Phased Development Roadmap

### Phase 1: Baseline Architecture & Toolchain Bringup *(Completed)*
*Status: Verified on Hardware*

- **Objectives**:
  - Establish a clean PlatformIO project properly configured for both the **Waveshare ESP32-S3-Zero** (`ESP32-S3FH4R2`: 4MB Quad-SPI Flash, 2MB Quad-SPI PSRAM, `qio_qspi`, `default.csv`) and the **ESP32-S3-DevKitC-1 N16R8** (`16MB Flash`, `8MB Octal PSRAM`, `qio_opi`).
  - Set up Native USB CDC (`ARDUINO_USB_CDC_ON_BOOT=1`, `ARDUINO_USB_MODE=1`) for flashing and console logs over the onboard USB-C port.
  - Pin external libraries (`EspSoftwareSerial`, `Adafruit NeoPixel`) and use native ESP32-S3 LEDC hardware PWM (`50Hz`, 14-bit) — no external CRSF or servo library dependency needed.
  - Implement the dual-core FreeRTOS task skeleton (`Core 1` Real-Time Control loop, `Core 0` Comms & Telemetry loop, plus Status LED heartbeat).
  - Establish the safe, non-strapping hardware pin budget in `include/config.h` and thread-safe shared data models in `include/types.h`.
- **Bench Verification**:
  1. Flash firmware via PlatformIO (`pio run -t upload`).
  2. Open serial monitor (`pio device monitor` @ 115200). Observe clean boot and startup banner:
     ```text
     ========================================
       CHIRP Droid Control [version_num]
       Target: Waveshare ESP32-S3-Zero (4MB Flash / 2MB PSRAM)
     ========================================
     [SYS] FreeRTOS tasks started successfully.
     ```
  3. Verify onboard WS2812B LED flashes with a smooth teal/blue heartbeat pulse every 500ms.

---

### Phase 2: Core Motion, Native `CrsfLink` Input & Dome Lighting *(Completed)*
*Status: Verified on Hardware*

- **Objectives**:
  - **Custom `CrsfLink` Receiver**: Connect Hardware `UART1` (`Serial1` on `GPIO 4/5` @ 420,000 baud) to the ExpressLRS receiver. Decode 16 RC channels (`0x16`), link quality/RSSI (`0x14`), and incoming EdgeTX Lua command frames (`0x32`) in a single-pass sliding-window state machine.
  - **Boot-Disarmed Safety Interlock, Slow-on-Arm Protection & Arcade Drive Mixer**: Differential steering mixer mapping Throttle (`CH2`) and Steering (`CH1`) to Left/Right foot PWMs (`GPIO 6 / 7`). Enforce boot-disarm protection on `SE` (`CH5`), Slow-on-Arm speed interlock on `SB` (`CH6`), deadbands, and instant neutral failsafe (`1500µs`) on link loss.
  - **Dome Control**: Manual yaw stick (`CH4`) mapped to Dome PWM (`GPIO 11`). Roam-A-Dome position feedback parsing on Hardware `UART0` (`Serial0` on `GPIO 43/44` @ 9600 baud).
  - **Dome Lighting Link**: SoftwareSerial link (`GPIO 12/13` @ 9600 baud) for sending Marcduino commands to the RSeries Logic Engine.
- **Bench Verification**:
  1. Bind transmitter to ELRS receiver and monitor channel readings and link status over USB serial.
  2. Verify boot-disarm interlock (powering on with `SE` Armed keeps motors locked at 1500µs until `SE` is cycled) and Slow-on-Arm protection (`SB` always starts in `Slow` on arming until moved).
  3. Test foot and dome PWM signals (`GP6, GP7, GP11`) with an oscilloscope, servo tester, or motor driver.
  4. Power cycle transmitter to verify instant motor disarm / neutral failsafe.
  5. Send test animation commands over SoftwareSerial (`GP12/13`) to the RSeries Logic Engine.

---

### Phase 3: Audio Link, Telemetry & Fresh EdgeTX Lua Script *(Current / Next Up)*
*Status: Ready for Implementation*

- **Objectives**:
  - **CHIRP_Audio Serial Protocol**: Connect Hardware `UART2` (`Serial2` on `GPIO 1/2` @ 115200 baud) to the CHIRP_Audio Trigger. Implement manifest request (`GMAN`), sound name fetching (`GNME`), sound triggering (`PLAY`), and vocal feedback (`PVOICE`).
  - **CRSF Telemetry Uplink & Bidirectional Commands**: Transmit battery telemetry (`ADC1` on `GP8, GP9, GP10`), drive/arming status, volume, and sound names back to the Radiomaster Zorro, and process incoming `0x32` sound triggers and config updates from the handset.
  - **New EdgeTX Lua Script**: Develop a fresh, responsive Lua telemetry dashboard from scratch for the Radiomaster Zorro (128x64 B&W display):
    - Live Sound Triggering Dashboard using `S1` scroll and **`SD / SH / SA / SG`** momentary buttons (Banks 1–4) sending direct CRSF `0x32` play commands.
    - Droid Diagnostics & 3-Battery Monitor.
    - Settings Editor for adjusting dome offset, motor inverts, and speeds from the transmitter.
  - **Multi-Transmitter Architecture**: Lay the groundwork to support additional EdgeTX transmitter form factors down the line (e.g., Radiomaster TX16S color touchscreen 480x272, Boxer, Pocket).
- **Bench Verification**:
  1. Connect ESP32-S3 to CHIRP_Audio board; verify automatic sound manifest sync over UART2.
  2. Turn sound selection knob `S1` on Zorro; confirm track name updates on the Lua screen.
  3. Press `SD / SH / SA / SG` buttons; verify instant `0x32` command reception and audio playback.

---

### Phase 4: Wireless Expansion & Smart Droid Features

- **Objectives**:
  - **ESP-NOW Dome Bridge**: Upgrade communication with the RSeries Logic Engine (which is already an ESP32-WROVER) to communicate wirelessly over **ESP-NOW**, eliminating physical slip-ring serial wires entirely.
  - **WCB Compatibility**: Implement ESP-NOW listener/transmitter for Greg Hulette's Wireless Communication Board (WCB) standard.
  - **Phone Web Portal**: Self-hosted Wi-Fi Captive Portal / Web Dashboard running on Core 0. Allows configuring speeds, audio settings, and triggering routines directly from a smartphone or tablet.
  - **Autonomous Behaviors**: Non-blocking AutoDome (random dome turning routines) and AutoChirp (random ambient sound generation based on activity).

---

### Phase 5: Future Investigation — Digital Motor Controller Telemetry (Roboteq & VESC)

For initial phases, foot drive uses universal 50Hz RC PWM signals (`1000–2000µs`) on `GP6` (Left Foot) and `GP7` (Right Foot). As a future investigation, if digital communication with brushless motor controllers (**Roboteq SBL1360** or **VESC**) is desired, `GP6` and `GP7` can be dynamically reconfigured as a **CAN bus (`TWAI TX/RX`)** or **Serial (`UART TX/RX`)** pair to support:
- **Roboteq (SBL1360)**: Querying battery current (`?A`), motor RPM (`?S`), and internal temperature (`?T`).
- **VESC (Vedder ESC)**: Reading real-time motor current (amps), amp-hours consumed, bus voltage, MOSFET temperature, and fault codes over VESC UART or CAN.
- **Telemetry Integration**: Feeding live current and bus voltage directly into `robotState` for display on the EdgeTX handset.

---

## Repository Structure

```text
CHIRP_Droid_Control/
├── DEVELOPMENT_PLAN.md      <-- This document
├── README.md                <-- Project overview
├── firmware-pico2/          <-- Archived Raspberry Pi Pico 2 W implementation
├── lua/
│   ├── CHIRP.lua            <-- Active EdgeTX Telemetry & Direct CRSF 0x32 Lua Dashboard
│   └── legacy/
│       └── CHIRP2.lua       <-- Archived legacy Lua script
└── firmware/                <-- Active ESP32-S3 PlatformIO project
    ├── platformio.ini       <-- Build config for S3-DevKit N16R8 (16MB/8MB) & S3-Zero (4MB/2MB)
    ├── include/
    │   ├── config.h         <-- Safe non-strapping pin definitions & system constants
    │   ├── types.h          <-- Thread-safe data structures, audio manifest & command queues
    │   ├── crsf_link.h      <-- Custom sliding-window CRSF receiver & telemetry driver
    │   ├── drive_control.h  <-- Arcade foot drive mixer & safety interlocks
    │   ├── dome_control.h   <-- Dome PWM & Roam-A-Dome UART parser
    │   ├── dome_lighting.h  <-- RSeries dome lighting Marcduino SoftSerial
    │   ├── storage_manager.h<-- ESP32 NVS persistent configuration manager
    │   ├── audio_link.h     <-- CHIRP_Audio UART2 protocol, manifest sync & PVOICE engine
    │   └── telemetry.h      <-- ADC1 3-battery voltage monitor & CRSF telemetry uplink
    └── src/
        ├── main.cpp         <-- Dual-core FreeRTOS task entrypoints & RGB heartbeat
        ├── crsf_link.cpp
        ├── drive_control.cpp
        ├── dome_control.cpp
        ├── dome_lighting.cpp
        ├── storage_manager.cpp
        ├── audio_link.cpp
        └── telemetry.cpp
```
