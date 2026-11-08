import os
import sys

Import("env")

def patch_sdfat():
    platform = env.PioPlatform()
    framework_dir = platform.get_package_dir("framework-arduinopico")
    if not framework_dir:
        print("[patch_sdfat] Warning: framework-arduinopico package directory not found.")
        return

    sdfat_target = os.path.join(
        framework_dir,
        "libraries",
        "SdFat",
        "src",
        "SdCard",
        "SdSpiCard",
        "SdSpiCard.cpp",
    )

    if not os.path.exists(sdfat_target):
        print(f"[patch_sdfat] Warning: {sdfat_target} not found.")
        return

    with open(sdfat_target, "r", encoding="utf-8") as f:
        content = f.read()

    # Check if already patched
    if "bit-shifted R1" in content or "cmd == CMD12" in content:
        print("[patch_sdfat] SdFat SdSpiCard.cpp is already patched for CMD12 bitshift.")
        return

    # Original code block to match
    original = (
        "  // there are 1-8 fill bytes before response.  fill bytes should be 0XFF.\n"
        "  uint8_t n = 0;\n"
        "  do {\n"
        "    m_status = spiReceive();\n"
        "  } while (m_status & 0X80 && ++n < 10);\n"
        "  return m_status;"
    )

    # Replacement code block
    replacement = (
        "  // there are 1-8 fill bytes before response.  fill bytes should be 0XFF.\n"
        "  uint8_t n = 0;\n"
        "  if (cmd == CMD12) {\n"
        "    // Some cards abort the data stream mid-byte and send a bit-shifted R1 for\n"
        "    // STOP_TRANSMISSION (e.g. E0 1F for R1 = 0). Realign it the way Linux\n"
        "    // mmc_spi does: take the first non-0XFF byte plus the next byte and shift\n"
        "    // out the leading one bits.\n"
        "    do {\n"
        "      m_status = spiReceive();\n"
        "    } while (m_status == 0XFF && ++n < 10);\n"
        "    if (m_status != 0XFF && (m_status & 0X80)) {\n"
        "      uint16_t rotator = (uint16_t)(m_status << 8) | spiReceive();\n"
        "      while (rotator & 0X8000) {\n"
        "        rotator <<= 1;\n"
        "      }\n"
        "      m_status = rotator >> 8;\n"
        "    }\n"
        "    return m_status;\n"
        "  }\n"
        "  do {\n"
        "    m_status = spiReceive();\n"
        "  } while (m_status & 0X80 && ++n < 10);\n"
        "  return m_status;"
    )

    if original in content:
        patched_content = content.replace(original, replacement, 1)
        with open(sdfat_target, "w", encoding="utf-8") as f:
            f.write(patched_content)
        print("[patch_sdfat] Applied CMD12 bitshift patch to framework-arduinopico SdFat.")
    else:
        # Also check with CRLF line endings
        original_crlf = original.replace("\n", "\r\n")
        replacement_crlf = replacement.replace("\n", "\r\n")
        if original_crlf in content:
            patched_content = content.replace(original_crlf, replacement_crlf, 1)
            with open(sdfat_target, "w", encoding="utf-8") as f:
                f.write(patched_content)
            print("[patch_sdfat] Applied CMD12 bitshift patch to framework-arduinopico SdFat.")
        else:
            print("[patch_sdfat] Note: SdSpiCard.cpp differs from expected; patch not applied.")

patch_sdfat()
