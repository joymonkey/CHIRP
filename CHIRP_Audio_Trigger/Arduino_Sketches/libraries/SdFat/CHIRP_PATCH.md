# Patched SdFat for CHIRP

This is SdFat 2.3.1 exactly as bundled with the Earle Philhower arduino-pico
core 5.4.4 (upstream commit `cda057318bec196183d4cc92b01bc1dd64bbfb02`, also
the latest upstream release at the time), with one change in
`src/SdCard/SdSpiCard/SdSpiCard.cpp`. Only `src/`, the license and the README
are included.

`CHIRP_Audio/platformio.ini` points `lib_extra_dirs` here, so PlatformIO builds
use this copy instead of the core's bundled SdFat.

## Problem

Some SD cards answer CMD12 (STOP_TRANSMISSION) with a correct R1 response
that is not byte-aligned. Stock SdFat reads it byte-aligned, sees a bogus
status, and fails every multi-block read stop. On the CHIRP Audio Trigger the
board then reports:

```
Initializing SD Card... FAILED at 25MHz, trying 4MHz...
SdError: 0XC,0X1F
```

(`0x0C` is reported for CMD18 because the failed CMD12 happens inside the
next read's `syncDevice()`; the `0x1F` is the misread CMD12 response.)

Observed on a 16GB SDHC card (CID manufacturer 0x9C, OEM "SO", product
"USD00", 2018-07). Raw MISO bytes after CMD12, identical at 400 kHz, 4 MHz and
25 MHz:

```
3F E0 1F FF  ->  00111111 11100000 00011111 11111111
                 R1 = 0x00 starts 3 bits late; SdFat reads 0x1F
```

The same card reads correctly at all speeds with raw CMD17/CMD18; wiring,
clock speed, CRC and MISO pull-up were all ruled out.

## Change

For CMD12 only, after the existing discarded stuff byte: skip 0xFF bytes, and
if the first non-0xFF byte has its top bit set, combine it with the next byte
and shift out the leading one bits to recover R1. This is the approach the
Linux `mmc_spi` driver uses for all commands ("ugly card with a bit-shifted
response"). Byte-aligned responses take the same path as before.

```diff
@@ -286,6 +286,23 @@

   // there are 1-8 fill bytes before response.  fill bytes should be 0XFF.
   uint8_t n = 0;
+  if (cmd == CMD12) {
+    // Some cards abort the data stream mid-byte and send a bit-shifted R1 for
+    // STOP_TRANSMISSION (e.g. E0 1F for R1 = 0). Realign it the way Linux
+    // mmc_spi does: take the first non-0XFF byte plus the next byte and shift
+    // out the leading one bits.
+    do {
+      m_status = spiReceive();
+    } while (m_status == 0XFF && ++n < 10);
+    if (m_status != 0XFF && (m_status & 0X80)) {
+      uint16_t rotator = (uint16_t)(m_status << 8) | spiReceive();
+      while (rotator & 0X8000) {
+        rotator <<= 1;
+      }
+      m_status = rotator >> 8;
+    }
+    return m_status;
+  }
   do {
     m_status = spiReceive();
   } while (m_status & 0X80 && ++n < 10);
```

## Arduino IDE

The Arduino IDE does not read `platformio.ini`, so it keeps using the core's
bundled SdFat. To use this copy, place this `SdFat` folder in your sketchbook
libraries folder (for example `~/Documents/Arduino/libraries/SdFat`). A
sketchbook library takes priority over a platform-bundled library with the
same name and architecture (`architectures=*` for both), per the Arduino CLI
dependency resolution rules. The IDE will print "Multiple libraries were found
for SdFat.h"; check that the "Used:" path points at the sketchbook copy.

## Notes

- Only `src/SdCard/SdSpiCard/SdSpiCard.cpp` differs from the bundled copy.
- If the core's SdFat is updated, re-copy it here and re-apply the change
  above, or drop this copy if upstream fixes the issue.
