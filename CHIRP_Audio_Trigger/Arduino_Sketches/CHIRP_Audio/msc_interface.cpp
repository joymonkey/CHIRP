// Mass Storage Class support!
// (for managing your SD card contents without having to take it out)
#include "config.h"

// ===================================
// MSC Callbacks
// ===================================
int32_t msc_read_cb(uint32_t lba, void* buffer, uint32_t bufsize) {
    return sd.card()->readSectors(lba, (uint8_t*)buffer, bufsize / 512) ? bufsize : -1;
}

int32_t msc_write_cb(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
    return sd.card()->writeSectors(lba, buffer, bufsize / 512) ? bufsize : -1;
}

void msc_flush_cb(void) {
    sd.card()->syncDevice();
}

// ===================================
// Setup MSC Trigger Pin
// ===================================
void setupMSC() {
    pinMode(PIN_MSC_TRIGGER, INPUT_PULLUP);
}

// ===================================
// Start MSC Mode
// ===================================
void startMSC() {
    if (g_mscActive) return;

    Serial.println("Starting MSC Mode...");

    // 1. Stop all SD streams
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (streams[i].active && (streams[i].type == STREAM_TYPE_WAV_SD || streams[i].type == STREAM_TYPE_MP3_SD)) {
            stopStream(i); // This releases SdFat file handles
        }
    }
    
    // Setting `g_mscActive = true` effectively disables `fillStreamBuffers` SD access (we will add that check).
    g_mscActive = true;

    // USB MSC Config
    uint32_t block_count = sd.card()->sectorCount();
    usb_msc.setID("CHIRP", "Audio SD", "1.0");
    usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
    usb_msc.setCapacity(block_count, 512);
    usb_msc.setUnitReady(true);
    
    // Force re-enumeration
    if (TinyUSBDevice.mounted()) {
      TinyUSBDevice.detach();
      delay(1000); // Matched to Test Sketch
      TinyUSBDevice.attach();
    }

    if (usb_msc.begin()) {
      Serial.println("[+++] MSC Interface ACTIVE.");
    } else {
      Serial.println("[!!!] MSC Setup Failed!");
      g_mscActive = false;
    }
}

// ===================================
// Stop MSC Mode
// ===================================
void stopMSC() {
    if (!g_mscActive) return;

    Serial.println("Stopping MSC Mode...");
    
    usb_msc.setUnitReady(false);
    
    // Detach to remove drive from PC
    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(1000); // Matched to Test Sketch
        TinyUSBDevice.attach();
    }
    
    // We don't need to reset Pins if we never changed them.
    // But we need to make sure SdFat is happy.
    
    g_mscActive = false;
    Serial.println("[---] MSC Interface INACTIVE.");
}

// ===================================
// Poll Trigger
// ===================================
void pollMSCTrigger() {
    // Wait for boot to settle (handled by loop logic usually, but let's be safe)
    if (millis() < 3000) return;
    
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 200) return; // Debounce/poll rate
    lastCheck = millis();

    bool pinActive = (digitalRead(PIN_MSC_TRIGGER) == LOW);
    
    static bool pinWasActive = false;

    if (pinActive && !g_mscActive) {
        startMSC();
        pinWasActive = true;
    } else if (!pinActive && g_mscActive && pinWasActive) {
        // When jumper removed, stop.
        stopMSC();
        pinWasActive = false;
    }
}
