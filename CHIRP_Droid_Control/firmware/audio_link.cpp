#include "audio_link.h"
#include "globals.h"
#include "debug.h"
#include "voice_debug.h"
#include <LittleFS.h>

unsigned long syncStartTime = 0;
uint8_t syncBank = 0;
uint8_t syncPage = 0;
uint8_t syncSound = 0;
uint32_t manifestChecksum = 0;

void requestNextName() {
  // Find next sound name to request
  while (syncBank < MAX_BANKS) {
    while (syncPage < MAX_PAGES) {
      if (syncSound == 0) syncSound = 1; // start at 1, no 0th sound
      
      if (syncSound <= manifest.banks[syncBank].pages[syncPage].soundCount) {
        char pageChar = 'A' + syncPage;
        SerialAudio.printf("GNME:%d,%c,%d\n", syncBank + 1, pageChar, syncSound);
        DBG_EVENT(DBG_AUDIO, "→ TX: GNME:%d,%c,%d", syncBank + 1, pageChar, syncSound);
        return;
      } else {
        syncSound = 1;
        syncPage++;
      }
    }
    syncPage = 0;
    syncBank++;
  }
  
  // Done syncing
  audioSyncState = SYNC_COMPLETE;
  DBG_EVENT(DBG_AUDIO, "Sync: COMPLETE");
  VSAY("sync", "complete", nullptr);
}

void audioInit() {
  SerialAudio.setRX(AUDIO_RX_PIN);
  SerialAudio.setTX(AUDIO_TX_PIN);
  SerialAudio.begin(AUDIO_BAUD, SERIAL_8N1);
  LittleFS.begin();
  
  // Clear manifest
  for (int b = 0; b < MAX_BANKS; b++) {
    for (int p = 0; p < MAX_PAGES; p++) {
      manifest.banks[b].pages[p].soundCount = 0;
      sprintf(manifest.banks[b].pages[p].name, "Pg%c", 'A' + p);
      for (int s = 0; s < MAX_SOUNDS; s++) {
        sprintf(manifest.banks[b].pages[p].sounds[s].name, "Snd %d", s + 1);
      }
    }
  }

  // Request manifest
  SerialAudio.println("GMAN");
  audioSyncState = SYNC_REQUESTING;
  syncStartTime = millis();
  DBG_EVENT(DBG_AUDIO, "→ TX: GMAN");
  DBG_EVENT(DBG_AUDIO, "Sync: IDLE → REQUESTING");
}

void audioUpdate() {
  // Autochirp logic
  static unsigned long lastAutochirpTime = 0;
  static unsigned long autochirpInterval = 15000;
  
  if (robotState.autochirpEnabled) {
    if (millis() - lastAutochirpTime > autochirpInterval) {
      lastAutochirpTime = millis();
      autochirpInterval = random(AUTOCHIRP_MIN_INTERVAL_MS, AUTOCHIRP_MAX_INTERVAL_MS);
      
      // Send PLAY command for Bank 1 currently selected sound
      uint16_t currentKnob = rcChannels[CH_SOUND_INDEX];
      uint8_t maxIdx = manifest.banks[0].pages[robotState.currentPage].soundCount;
      if (maxIdx > 0) {
        long knobOffset = constrain((long)currentKnob - RC_MIN_US, 0, RC_MAX_US - RC_MIN_US);
        uint8_t sndIdx = (uint8_t)((knobOffset * maxIdx) / (RC_MAX_US - RC_MIN_US)) + 1;
        sndIdx = constrain(sndIdx, 1, maxIdx);
        DBG_EVENT(DBG_AUDIO, "Autochirp Triggered");
        audioPlaySound(1, robotState.currentPage, sndIdx, robotState.volume);
      }
    }
  } else {
    lastAutochirpTime = millis(); // Keep timer reset while disabled
  }

  // Sync Logic
  if (audioSyncState == SYNC_REQUESTING) {
    if (millis() - syncStartTime > CACHE_SYNC_TIMEOUT_MS) {
      // Retry
      SerialAudio.println("GMAN");
      syncStartTime = millis();
      DBG_EVENT(DBG_AUDIO, "Sync Timeout! Retrying GMAN...");
    }
  } else if (audioSyncState == SYNC_WAITING) {
    if (millis() - syncStartTime > CACHE_SYNC_TIMEOUT_MS) {
      DBG_EVENT(DBG_AUDIO, "Sync Timeout! Retrying GNME...");
      requestNextName();
      syncStartTime = millis();
    }
  }

  while (SerialAudio.available() > 0) {
    String line = SerialAudio.readStringUntil('\n');
    line.trim();

    if (line.length() > 0) {
        DBG_EVENT(DBG_AUDIO, "← RX: %s", line.c_str());
    }

    if (line.startsWith("PACK:PVOICE")) {
      voiceAckReceived = true;
    } else if (line.startsWith("MDAT:")) {
      // Parse manifest data
    } else if (line.startsWith("BANK:")) {
      int idx1 = line.indexOf(',', 5);
      int idx2 = line.indexOf(',', idx1 + 1);
      if (idx1 > 0 && idx2 > 0) {
        int b = line.substring(5, idx1).toInt();
        String pageStr = line.substring(idx1 + 1, idx2);
        int count = line.substring(idx2 + 1).toInt();
        
        char pChar = 'A';
        int underscoreIdx = pageStr.indexOf('_');
        if (underscoreIdx > 0) {
            char charBefore = pageStr.charAt(underscoreIdx - 1);
            if (charBefore >= 'A' && charBefore <= 'Z') pChar = charBefore;
        } else if (pageStr.length() > 0) {
            char first = pageStr.charAt(0);
            if (first >= 'A' && first <= 'Z') pChar = first;
        }
        
        int p = pChar - 'A';
        if (b > 0 && b <= MAX_BANKS && p >= 0 && p < MAX_PAGES) {
          manifest.banks[b-1].pages[p].soundCount = count;
          // Extract clean name for page
          String cleanName = pageStr;
          if (underscoreIdx > 0 && underscoreIdx + 1 < pageStr.length()) {
              cleanName = pageStr.substring(underscoreIdx + 1);
          } else if (underscoreIdx > 0) {
              cleanName = "";
          }
          strncpy(manifest.banks[b-1].pages[p].name, cleanName.c_str(), MAX_FILENAME_LEN);
          manifest.banks[b-1].pages[p].name[MAX_FILENAME_LEN] = '\0';
        }
      }
    } else if (line.startsWith("MEND")) {
      // Manifest structure loaded, start requesting names
      syncBank = 0;
      syncPage = 0;
      syncSound = 1;
      requestNextName();
      audioSyncState = SYNC_WAITING;
      syncStartTime = millis();
      DBG_EVENT(DBG_AUDIO, "Sync: REQUESTING → WAITING");
    } else if (line.startsWith("NAME:")) {
      // NAME:bank,page,index,name
      int idx1 = line.indexOf(',', 5);
      int idx2 = line.indexOf(',', idx1 + 1);
      int idx3 = line.indexOf(',', idx2 + 1);
      if (idx1 > 0 && idx2 > 0 && idx3 > 0) {
        int b = line.substring(5, idx1).toInt();
        String pStr = line.substring(idx1 + 1, idx2);
        int idx = line.substring(idx2 + 1, idx3).toInt();
        String nameStr = line.substring(idx3 + 1);
        
        char pChar = 'A';
        if (pStr.length() > 0) {
            char first = pStr.charAt(0);
            if (first >= 'A' && first <= 'Z') pChar = first;
        }
        
        int p = pChar - 'A';
        if (b > 0 && b <= MAX_BANKS && p >= 0 && p < MAX_PAGES) {
          // Strip extension if present
          int dotIdx = nameStr.lastIndexOf('.');
          if (dotIdx > 0) nameStr = nameStr.substring(0, dotIdx);
          
          if (idx <= MAX_SOUNDS && idx > 0) {
            strncpy(manifest.banks[b-1].pages[p].sounds[idx-1].name, nameStr.c_str(), MAX_FILENAME_LEN);
            manifest.banks[b-1].pages[p].sounds[idx-1].name[MAX_FILENAME_LEN] = '\0';
          }
        }
        
        syncSound++;
        requestNextName();
        syncStartTime = millis();
      }
    }
  }
}

void audioPlaySound(uint8_t bankIndex, uint8_t pageIndex, uint8_t soundIndex, uint8_t volume) {
  char pageChar = 'A' + pageIndex;
  DBG_EVENT(DBG_AUDIO, "→ TX: PLAY:%d,%d,%c,%d", soundIndex, bankIndex, pageChar, volume);
  SerialAudio.printf("PLAY:%d,%d,%c,%d\n", soundIndex, bankIndex, pageChar, volume);
}

void audioStopAll() {
  DBG_EVENT(DBG_AUDIO, "→ TX: STOP");
  SerialAudio.println("STOP");
}

void audioSetVolume(uint8_t volume) {
  uint8_t vol = constrain(volume, 0, 99);
  DBG_EVENT(DBG_AUDIO, "→ TX: VOL:%d", vol);
  SerialAudio.printf("VOL:%d\n", vol);
}

const char* getSoundName(uint8_t bank, uint8_t page, uint8_t index) {
  if (bank == 0 || bank > MAX_BANKS) return "ErrB";
  if (page >= MAX_PAGES) return "ErrP";
  if (index == 0 || index > MAX_SOUNDS) return "ErrI";
  return manifest.banks[bank-1].pages[page].sounds[index-1].name;
}

const char* getPageName(uint8_t bank, uint8_t page) {
  if (bank == 0 || bank > MAX_BANKS) return "ErrB";
  if (page >= MAX_PAGES) return "ErrP";
  return manifest.banks[bank-1].pages[page].name;
}