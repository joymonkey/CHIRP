# CHIRP Droid Control - ESP32-S3 DevKit Breakout Board

> [!WARNING]
> **Work In Progress**: This KiCad hardware project is currently in active development and has not yet been fabricated, assembled, or hardware-verified. Pinouts, component selections, and board layouts may change without notice.

## Overview
This custom breakout board connects an ESP32-S3 DevKit module to the CHIRP Droid Control system, featuring:
- Support for wide-format ESP32-S3 DevKit boards with labeled pinouts and antenna overhang guides
- Multi-battery voltage sensing dividers (up to 3 battery inputs) with flexible WAGO 2601-1102 and 2.54mm header footprint options
- On-board high-voltage (up to 36V input) 5V step-down buck regulator (`TPS54360DDA`) with reverse-polarity and TVS transient protection
- Optional drop-in header footprint for Pololu step-down regulator modules (`D36V28F5` / `D24V22F5`)
- Dedicated 5V auxiliary outputs for peripheral modules (CHIRP Audio Trigger, ExpressLRS receiver, etc.)
- Diode backfeed isolation between external 5V and ESP32-S3 onboard USB-C 5V
