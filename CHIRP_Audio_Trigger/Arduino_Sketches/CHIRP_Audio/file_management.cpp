#include "config.h"

// ===================================
// Parse CHIRP.INI File
// ===================================
bool parseIniFile() {
    bool foundPage = false;
    bool foundVersion = false;
    bool versionMismatch = false;
    char storedVersion[32] = {0};

    // Need to preserve existing settings if we rewrite
    // We already read activeBank1Page, that's the only other setting currently.

    mutex_enter_blocking(&sd_mutex);
    FsFile iniFile = sd.open("CHIRP.INI", FILE_READ);
    
    if (iniFile) {
        char buffer[128];
        while (iniFile.available()) {
            // Read a line, trim whitespace
            String line = iniFile.readStringUntil('\n');
            line.trim();
            line.toCharArray(buffer, sizeof(buffer));

            // Check if it's a valid setting line
            if (strlen(buffer) > 0 && buffer[0] == '#') {
                char* command = buffer + 1;
                while (*command == ' ') command++; // trim leading space after #

                if (strncasecmp(command, "BANK1_PAGE", 10) == 0) {
                    char* value = strchr(command, ' ');
                    if (value) {
                        while (*(++value) == ' '); // Find first char of value
                        if (*value >= 'A' && *value <= 'Z') {
                            activeBank1Page = *value;
                            foundPage = true;
                        }
                    }
                }
                // Legacy support for BANK1_VARIANT
                else if (strncasecmp(command, "BANK1_VARIANT", 13) == 0) {
                    char* value = strchr(command, ' ');
                    if (value) {
                        while (*(++value) == ' '); // Find first char of value
                        if (*value >= 'A' && *value <= 'Z') {
                            activeBank1Page = *value;
                            foundPage = true;
                        }
                    }
                }
                // Check VERSION
                else if (strncasecmp(command, "VERSION", 7) == 0) {
                    char* value = strchr(command, ' ');
                    if (value) {
                        while (*(++value) == ' '); // Find first char of value
                        strncpy(storedVersion, value, sizeof(storedVersion)-1);
                        foundVersion = true;
                    }
                }
                // Check BAUD_RATE
                else if (strncasecmp(command, "BAUD_RATE", 9) == 0) {
                    char* value = strchr(command, ' ');
                    if (value) {
                        while (*(++value) == ' '); // Find first char of value
                        long rate = atol(value);
                        if (rate == 2400 || rate == 9600 || rate == 19200 || 
                            rate == 38400 || rate == 57600 || rate == 115200) {
                            baudRate = rate;
                        }
                    }
                }
                // Check USE_FLASH_BANK1
                else if (strncasecmp(command, "USE_FLASH_BANK1", 15) == 0) {
                    char* value = strchr(command, ' ');
                    if (value) {
                        while (*(++value) == ' '); // Find first char of value
                        int val = atoi(value);
                        useFlashForBank1 = (val == 1);
                    }
                }
            }
        }
        iniFile.close();
    }

    if (foundVersion) {
         if (strcmp(storedVersion, VERSION_STRING) != 0) {
            versionMismatch = true;
            Serial.printf("Firmware update detected! Old: %s, New: %s\n", storedVersion, VERSION_STRING);
         }
    } else {
        versionMismatch = true; // No version found, treat as update/initial
        Serial.println("No firmware version in INI. Adding it.");
    }

    // Rewrite INI if needed (missing Page, missing Version, or Version Mismatch)
    mutex_exit(&sd_mutex); // Release mutex before calling writeIniFile which takes it again
    
    if (!foundPage || versionMismatch) {
        writeIniFile();
    }
    
    return versionMismatch;
}

// ===================================
// Write CHIRP.INI File
// ===================================
void writeIniFile() {
    // PRECONDITION: mutex_enter_blocking(&sd_mutex) should be called by caller if strictly needed,
    // BUT since we are opening 'w' which might take time, and this is system config, 
    // we should handle mutex here OR assume caller handles it.
    // parseIniFile already holds the mutex when calling this!
    // However, other callers (button press) won't have it yet.
    // Recursive mutexes aren't standard here, so we must be careful.
    // Let's assume callers MUST NOT hold the mutex, and we take it here.
    // BUT parseIniFile DOES hold it. 
    // FIX: Refactor parseIniFile to release before calling, or duplicate logic?
    // EASIER: Create `writeIniFileInternal` or just put the logic here and be careful.
    // Let's make writeIniFile independent and fix parseIniFile to release first?
    // parseIniFile has logic: (!foundPage || versionMismatch) -> rewrite.
    // It's safer to just duplicate the write logic inside parseIniFile for now (it was already there),
    // OR release mutex, call writeIniFile, then return.
    
    // Let's implement writeIniFile as a standalone function that TAKES the mutex.
    // And in parseIniFile, we will release mutex before calling it.
    
    mutex_enter_blocking(&sd_mutex);
    FsFile iniFile = sd.open("CHIRP.INI", FILE_WRITE | O_TRUNC);
    if (iniFile) {
        iniFile.println("# CHIRP Configuration File");
        iniFile.println("# Settings:");
        iniFile.printf("#BANK1_PAGE %c\n", activeBank1Page); 
        iniFile.printf("#USE_FLASH_BANK1 %d\n", useFlashForBank1 ? 1 : 0);
        iniFile.printf("#BAUD_RATE %ld\n", baudRate);
        iniFile.println();
        iniFile.println("# Firmware Version (Last Booted)");
        iniFile.println("# Do not edit this manually unless you want to force voice feedback.");
        iniFile.printf("#VERSION %s\n", VERSION_STRING);
        
        iniFile.close();
        //Serial.println("CHIRP.INI updated.");
    } else {
        //Serial.println("ERROR: Could not create/update CHIRP.INI!");
    }
    mutex_exit(&sd_mutex);
}




// ===================================
// Scan Valid Bank 1 Pages (Run at startup)
// ===================================
void scanValidBank1Pages() {
    validBank1PageCount = 0;
    validBank1Pages[0] = '\0';
    
    mutex_enter_blocking(&sd_mutex);
    FsFile root = sd.open("/");
    if (!root || !root.isDirectory()) {
        mutex_exit(&sd_mutex);
        return;
    }
    
    FsFile file;
    while (file.openNext(&root, O_RDONLY)) {
        if (file.isDirectory()) {
            char dirName[64];
            file.getName(dirName, sizeof(dirName));
            
            // Check for pattern "1[A-Z]_"
            // Length must be at least 3
            if (strlen(dirName) >= 3 && 
                dirName[0] == '1' && 
                dirName[1] >= 'A' && dirName[1] <= 'Z' && 
                dirName[2] == '_') {
                
                char page = dirName[1];
                
                // Add if not already present
                if (!strchr(validBank1Pages, page)) {
                    validBank1Pages[validBank1PageCount++] = page;
                    validBank1Pages[validBank1PageCount] = '\0';
                }
            }
        }
        file.close();
    }
    root.close();
    mutex_exit(&sd_mutex);
    
    // Sort logic (Bubble sort)
    for (int i = 0; i < validBank1PageCount - 1; i++) {
        for (int j = 0; j < validBank1PageCount - i - 1; j++) {
            if (validBank1Pages[j] > validBank1Pages[j+1]) {
                char temp = validBank1Pages[j];
                validBank1Pages[j] = validBank1Pages[j+1];
                validBank1Pages[j+1] = temp;
            }
        }
    }
    
    if (validBank1PageCount == 0) {
        strcpy(validBank1Pages, "A");
        validBank1PageCount = 1;
        //Serial.println("No valid Bank 1 pages found. Defaulting to 'A'.");
    } else {
        //Serial.printf("Valid Bank 1 Pages: %s\n", validBank1Pages);
    }
}


// ===================================
// Scan Bank 1 (Finds dir matching activeBank1Page)
// ===================================
void scanBank1() {
    bank1SoundCount = 0;
    bank1DirName[0] = '\0'; // Clear the name
    
    char targetPrefix[4]; // "1A_"
    snprintf(targetPrefix, sizeof(targetPrefix), "1%c_", activeBank1Page);
    
    mutex_enter_blocking(&sd_mutex);
    FsFile root = sd.open("/");
    
    if (!root || !root.isDirectory()) {
        //Serial.println("ERROR: Could not open root directory");
        mutex_exit(&sd_mutex);
        return;
    }

    FsFile bankDir;
    // Loop 1: Find the *specific* Bank 1 Directory
    while (bankDir.openNext(&root, O_RDONLY)) {
        char dirName[64];
        bankDir.getName(dirName, sizeof(dirName));
        
        if (bankDir.isDirectory() && strncmp(dirName, targetPrefix, 3) == 0) {
            strncpy(bank1DirName, dirName, sizeof(bank1DirName) - 1);
            //Serial.printf("Found Active Bank 1 Directory: %s\n", bank1DirName);
            
            // Now, scan files inside this directory
            FsFile file;
            while (file.openNext(&bankDir, O_RDONLY)) {
                char filename[64];
                file.getName(filename, sizeof(filename));
                if (!file.isDirectory()) {
                    const char* ext = strrchr(filename, '.');
                    if (ext && (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".mp3") == 0)) {
                        char* underscore = strchr(filename, '_');
                        if (underscore && isdigit(*(underscore + 1))) {
                            char basename[16];
                            int baseLen = underscore - filename;
                            if (baseLen >= sizeof(basename)) baseLen = sizeof(basename) - 1;
                            strncpy(basename, filename, baseLen);
                            basename[baseLen] = '\0';
                            
                            int soundIdx = -1;
                            for (int i = 0; i < bank1SoundCount; i++) {
                                if (strcasecmp(bank1Sounds[i].basename, basename) == 0) {
                                    soundIdx = i;
                                    break;
                                }
                            }
                            
                            if (soundIdx == -1) {
                                if (bank1SoundCount < MAX_SOUNDS) {
                                     soundIdx = bank1SoundCount++;
                                     strncpy(bank1Sounds[soundIdx].basename, basename, sizeof(bank1Sounds[soundIdx].basename) - 1);
                                    bank1Sounds[soundIdx].variantCount = 0;
                                    bank1Sounds[soundIdx].lastVariantPlayed = -1; // Init non-repeat
                                }
                            }
                            
                            if (soundIdx != -1 && bank1Sounds[soundIdx].variantCount < 25) {
                                
                                 strncpy(bank1Sounds[soundIdx].variants[bank1Sounds[soundIdx].variantCount],
                                       filename,
                                       sizeof(bank1Sounds[soundIdx].variants[0]) - 1);
                                 bank1Sounds[soundIdx].variantCount++;
                            }
                        }
                        else {
                            char basename[16];
                            strncpy(basename, filename, sizeof(basename) - 1);
                            char* dot = strrchr(basename, '.');
                            if (dot) *dot = '\0';
                            if (bank1SoundCount < MAX_SOUNDS) {
                                int soundIdx = bank1SoundCount++;
                                strncpy(bank1Sounds[soundIdx].basename, basename, sizeof(bank1Sounds[soundIdx].basename) - 1);
                                strncpy(bank1Sounds[soundIdx].variants[0], filename, sizeof(bank1Sounds[soundIdx].variants[0]) - 1);
                                bank1Sounds[soundIdx].variantCount = 1;
                                bank1Sounds[soundIdx].lastVariantPlayed = -1; // Init non-repeat
                            }
                        }
                    }
                }
                file.close();
            } // end file loop
            
            bankDir.close();
            break; // Found and processed Bank 1, so exit the root loop
        }
        bankDir.close();
    } // end root loop
    
    root.close();
    mutex_exit(&sd_mutex);

    if (bank1DirName[0] == '\0') {
        //Serial.printf("WARNING: No Bank 1 directory matching '%s...' found on SD card.\n", targetPrefix);
    }
}


// ===================================
// Sync Bank 1 to Flash
// ===================================

// ===================================
// Voice Feedback Helper
// ===================================
void playVoiceFeedback(const char* filename) {
    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "/0_System/%s", filename);
    
    // Check if file exists first
    bool exists = false;
    mutex_enter_blocking(&sd_mutex);
    if (sd.exists(fullPath)) exists = true;
    mutex_exit(&sd_mutex);
    
    if (!exists) return; // Silent fail if file missing (user preference)

    // Unmute
    g_allowAudio = true;
    delay(120); // Ramp up (Increased to prevent pop)
    
    if (startStream(0, fullPath)) {
        // Wait for it to finish
        // Since we are blocking the main loop, we MUST manually pump the audio data!
        while (streams[0].active) {
            
            fillStreamBuffers();
            
            // Handle Auto-Stop (Logic normally in main loop)
            if (streams[0].stopRequested) {
                stopStream(0);
                streams[0].stopRequested = false;
            }
            // Logic to detect end of file + empty buffer
            if (streams[0].active && streams[0].fileFinished) {
                if (streams[0].ringBuffer->availableForRead() == 0) {
                    stopStream(0);
                }
            }
            
            delay(1); 
        }
    }
    
    // Mute again
    g_allowAudio = false;
    delay(5);
}

// Play a number file (0000.wav to 0099.wav)
// For numbers >= 100, we could implement valid logic or just limit it.
// Files are named 0000.wav ... 0100.wav based on the listing.
void playVoiceNumber(int number) {
    if (number < 0) return;
    if (number > 100) number = 100; // Cap at 100 for now based on file list
    
    char numFile[16];
    snprintf(numFile, sizeof(numFile), "%04d.wav", number);
    playVoiceFeedback(numFile);
}

void playBaudFeedback(long rate) {
    playVoiceFeedback("setting.wav");
    playVoiceFeedback("serial.wav"); 
    playVoiceFeedback("baud_rate.wav"); 
    
    // User Requested Logic:
    // 2400 -> "24" "hundred"
    // 115200 -> "11" "52" "hundred"
    
    long hundreds = rate / 100;
    
    if (hundreds > 100) {
        // e.g. 1152 -> 11, 52
        int p1 = hundreds / 100;
        int p2 = hundreds % 100;
        playVoiceNumber(p1);
        playVoiceNumber(p2);
    } else {
        // e.g. 24 -> 24
        playVoiceNumber((int)hundreds);
    }
    
    playVoiceFeedback("hundred.wav");
    
    delay(100);
    // "Hz"
    playVoiceFeedback("hz.wav");
}

void playBankNameFeedback(char page) {
    // this spells out the folder name of the currently selected Bank 1 page
    // 1. Find the directory: "1<Page>_*"
    char pattern[4];
    snprintf(pattern, sizeof(pattern), "1%c_", page);
    
    char suffix[64] = "";
    bool found = false;
    
    mutex_enter_blocking(&sd_mutex);
    FsFile root = sd.open("/");
    if (root) {
        FsFile file;
        while(file.openNext(&root, O_RDONLY)) {
            if(file.isDirectory()) {
                 char name[64];
                 file.getName(name, sizeof(name));
                 if(strncasecmp(name, pattern, 3) == 0) {
                     // Found it
                     strncpy(suffix, name + 3, sizeof(suffix)-1);
                     found = true;
                     // Don't break immediately, we need to close file? 
                     // file.close() happens after if block usually.
                     // But we want to stop.
                     file.close(); // Close current
                     break;
                 }
            }
            file.close();
        }
        root.close();
    }
    mutex_exit(&sd_mutex);
    
    if (!found || suffix[0] == '\0') return;
    
    // 2. Spell it out
    for(int i=0; suffix[i] != '\0'; i++) {
        char c = suffix[i];
        if (isdigit(c)) {
            playVoiceNumber(c - '0');
        } else if (isalpha(c)) {
            char letterFile[16];
            char lower = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
            snprintf(letterFile, sizeof(letterFile), "_%c.wav", lower);
            playVoiceFeedback(letterFile);
        }
        // Small delay between characters
        //delay(80); 
    }
}



// ===================================
// Sync Bank 1 to Flash
// ===================================

// ===================================
// Sync Bank 1 to Flash
// ===================================
// ===================================
// Play Firmware Update Feedback
// ===================================
void playFirmwareUpdateFeedback(bool fwUpdated) {
    if (!fwUpdated) {
        Serial.println("  Firmware Feedback: Skipped (No update detected)");
        return;
    }

    // Check for Voice Feedback Directory
    bool hasVoiceFeedback = false;
    mutex_enter_blocking(&sd_mutex);
    if (sd.exists("/0_System")) {
        hasVoiceFeedback = true;
    }
    mutex_exit(&sd_mutex);

    if (hasVoiceFeedback) {
        //Serial.println("  Firmware Feedback: Playing voice sequence...");
        playVoiceFeedback("chirp.wav");
        playVoiceFeedback("audio_engine.wav");
        delay(200);
        playVoiceFeedback("firmware.wav");  
        playVoiceFeedback("updated.wav");
        playVoiceFeedback("0002.wav");
        playVoiceFeedback("new_version.wav");
        delay(200);
        
        // Speak version stored in VERSION_STRING (e.g. 20251221)
        // Skip first 2 digits ("20"), read pairs: "25", "12", "21"
        if (strlen(VERSION_STRING) >= 8) {
           // 25
           int year = (VERSION_STRING[2] - '0') * 10 + (VERSION_STRING[3] - '0');
           playVoiceNumber(year);
           delay(100);
           
           // 12
           int month = (VERSION_STRING[4] - '0') * 10 + (VERSION_STRING[5] - '0');
           playVoiceNumber(month);
           delay(100);
           
           // 21
           int day = (VERSION_STRING[6] - '0') * 10 + (VERSION_STRING[7] - '0');
           playVoiceNumber(day);
           delay(150);
        }
    }
}

// ===================================
// Sync Bank 1 to Flash
// ===================================
bool syncBank1ToFlash() {
    if (!useFlashForBank1) {
        Serial.println("  Skipping sync: Flash memory usage disabled in CHIRP.INI.");
        return false;
    }

    if (bank1DirName[0] == '\0') {
        Serial.println("  Skipping sync: No active Bank 1 directory found.");
        return false;
    }
    
    // Check for Voice Feedback Directory
    bool hasVoiceFeedback = false;
    mutex_enter_blocking(&sd_mutex);
    if (sd.exists("/0_System")) {
        hasVoiceFeedback = true;
    }
    mutex_exit(&sd_mutex);

    if (hasVoiceFeedback) {
        Serial.println("  Voice Feedback: Enabled");
    }
    
    if (!LittleFS.exists("/flash")) {
        LittleFS.mkdir("/flash");
    }

    // --- Pruning stale files from flash ---
    Serial.println("  Pruning stale files from flash...");
    int filesDeleted = 0;
    Dir dir = LittleFS.openDir("/flash");
    while (dir.next()) {
        if (!dir.isDirectory()) {
            char flashFilename[64];
            strncpy(flashFilename, dir.fileName().c_str(), sizeof(flashFilename) - 1);
            
            bool foundInMasterList = false;
            // Check if this file exists in the new SD bank (master list)
            for (int i = 0; i < bank1SoundCount; i++) {
                for (int v = 0; v < bank1Sounds[i].variantCount; v++) {
                    if (strcmp(flashFilename, bank1Sounds[i].variants[v]) == 0) {
                        foundInMasterList = true;
                        break;
                    }
                }
                if (foundInMasterList) break;
            }
            
            // If the file was NOT found in the master list, delete it.
            if (!foundInMasterList) {
                char fullFlashPath[80];
                snprintf(fullFlashPath, sizeof(fullFlashPath), "/flash/%s", flashFilename);
                if (LittleFS.remove(fullFlashPath)) {
                    Serial.printf("    - Deleted stale file: %s\n", flashFilename);
                    filesDeleted++;
                } else {
                    Serial.printf("    - ERROR deleting: %s\n", flashFilename);
                }
            }
        }
    }
    if (filesDeleted == 0) {
        Serial.println("    - No stale files found.");
    }
    // --- End of Pruning Feature ---

    
    int totalFiles = 0;
    for (int i = 0; i < bank1SoundCount; i++) {
        totalFiles += bank1Sounds[i].variantCount;
    }
    
    int syncLimit = DEV_MODE ? min(totalFiles, DEV_SYNC_LIMIT) : totalFiles;
    Serial.printf("  Syncing %d files from %s", syncLimit, bank1DirName);
    if (DEV_MODE && totalFiles > syncLimit) {
        Serial.printf(" (DEV MODE: limited to first %d)", DEV_SYNC_LIMIT);
    }
    Serial.println();
    
    // --- Voice Feedback: Start ---
    /* Moved to playFirmwareUpdateFeedback() */

    // --- Count Actual Files to Sync ---
    int filesToSync = 0;
    int preCheckProcessed = 0;
    
    // We pre-calculate to provide accurate "Syncing X files" voice prompt.
    // This allows us to say "Syncing 5 files" when 95 are already synced.
    
    for (int i = 0; i < bank1SoundCount; i++) {
        for (int v = 0; v < bank1Sounds[i].variantCount; v++) {
            preCheckProcessed++;
            if (DEV_MODE && preCheckProcessed > DEV_SYNC_LIMIT) break;

            const char* filename = bank1Sounds[i].variants[v];
            char sdPath[64];
            char flashPath[64];
            snprintf(sdPath, sizeof(sdPath), "/%s/%s", bank1DirName, filename);
            snprintf(flashPath, sizeof(flashPath), "/flash/%s", filename);

            bool needsCopy = true;
            mutex_enter_blocking(&sd_mutex);
            FsFile sdFile = sd.open(sdPath, FILE_READ);
            if (sdFile) {
                size_t sdSize = sdFile.size();
                if (LittleFS.exists(flashPath)) {
                    File flashFile = LittleFS.open(flashPath, "r");
                    if (flashFile) {
                        size_t flashSize = flashFile.size();
                        if (flashSize == sdSize) {
                            needsCopy = false; 
                        }
                        flashFile.close();
                    }
                }
                sdFile.close();
            }
            mutex_exit(&sd_mutex);

            if (needsCopy) filesToSync++;
        }
        if (DEV_MODE && preCheckProcessed > DEV_SYNC_LIMIT) break;
    }
    
    // --- Voice Feedback: Start ---
    if (hasVoiceFeedback && filesToSync > 0) {
        // "Syncing"
        playVoiceFeedback("syncing.wav");
        delay(100);
        
        // "X"
        playVoiceNumber(filesToSync);
        delay(100);
        
        // "Files"
        playVoiceFeedback("files.wav");
        delay(100);

        // "Of"
        playVoiceFeedback("of.wav");
        delay(100);

        // "Y"
        playVoiceNumber(syncLimit);
        delay(100);

        // "Total"
        playVoiceFeedback("total.wav");
        delay(100);

        // "Files"
        playVoiceFeedback("files.wav");
        delay(200);
    } else if (hasVoiceFeedback) {
        Serial.println("  System in sync. Silent startup.");
    }
    
    int filesCopied = 0;
    int filesSkipped = 0;
    int filesProcessed = 0;
    int filesSyncedSoFar = 0;

    for (int i = 0; i < bank1SoundCount; i++) {
        for (int v = 0; v < bank1Sounds[i].variantCount; v++) {
            filesProcessed++;
            if (DEV_MODE && filesProcessed > DEV_SYNC_LIMIT) {
                goto sync_complete;
            }
            
            const char* filename = bank1Sounds[i].variants[v];
            Serial.printf("  [%d/%d] ", filesProcessed, syncLimit);
            
            char sdPath[64];
            char flashPath[64];
            snprintf(sdPath, sizeof(sdPath), "/%s/%s", bank1DirName, filename);
            snprintf(flashPath, sizeof(flashPath), "/flash/%s", filename);
            
            // Heartbeat for scanning
            updateSyncLEDs(false);

            bool needsCopy = true;
            bool justCopied = false;
            mutex_enter_blocking(&sd_mutex);
            FsFile sdFile = sd.open(sdPath, FILE_READ);
            if (sdFile) {
                size_t sdSize = sdFile.size();
                if (LittleFS.exists(flashPath)) {
                    File flashFile = LittleFS.open(flashPath, "r");
                    if (flashFile) {
                        size_t flashSize = flashFile.size();
                        if (flashSize == sdSize) {
                            needsCopy = false;
                            filesSkipped++;
                            Serial.printf("Skipped: %s\n", filename);
                        }
                        
                        flashFile.close();
                    }
                }
                
                if (needsCopy) {
                    // Sync File Transition Feedback
                    updateSyncLEDs(true);
                    
                    sdFile.rewind();
                    File flashFile = LittleFS.open(flashPath, "w");
                    if (flashFile) {
                        const uint16_t CHUNK_SIZE = 512;
                        uint8_t buffer[CHUNK_SIZE];
                        uint32_t remaining = sdSize;
                        bool copySuccess = true;
                        Serial.printf("Copying: %s (%lu KB)... ", 
                                     filename, sdSize / 1024);
                        while (remaining > 0 && copySuccess) {
                            uint16_t toRead = (remaining > CHUNK_SIZE) ?
                                              CHUNK_SIZE : remaining;
                            
                            int bytesRead = sdFile.read(buffer, toRead);
                            
                            // Heartbeat during copy
                            updateSyncLEDs(false);

                            if (bytesRead <= 0) {
                                Serial.println(" READ ERROR!");
                                copySuccess = false;
                                break;
                            }
                            
                            int bytesWritten = flashFile.write(buffer, bytesRead);
                            if (bytesWritten != bytesRead) {
                                Serial.println(" WRITE ERROR!");
                                copySuccess = false;
                                break;
                            }
                            
                            remaining -= bytesRead;
                        }
                        
                        flashFile.close();
                        if (copySuccess) {
                            Serial.println("OK");
                            filesCopied++;
                            filesSyncedSoFar++;
                            justCopied = true;
                            
                            // Success Feedback moved outside mutex to avoid deadlock
                        }
                    } else {
                        Serial.println(" FAILED to create flash file!");
                    }
                }
                
                sdFile.close();
            } else {
                Serial.printf("ERROR: Could not open %s\n", sdPath);
            }
            
            mutex_exit(&sd_mutex);

            if (justCopied) {
                if (hasVoiceFeedback) {
                    playVoiceNumber(filesSyncedSoFar);
                } else {
                    // Original Beeper Feedback
                    g_allowAudio = true; 
                    delay(5); // Wait for I2S to start
                    playChirp(2000, 500, 60, 50); // fast chirp
                    delay(60);
                    playChirp(2000, 4000, 50, 50); // fast chirp
                    delay(60); // Wait for chirp (blocking Core 0 is fine here)
                    g_allowAudio = false; // Mute again
                    delay(5);
                }
            }
        }
    }
    
sync_complete:
    
    if (hasVoiceFeedback && filesToSync > 0) {
        delay(200);
        // "Transfer"
        playVoiceFeedback("transfer.wav");
        delay(10);
        // "Completed" (or "Complete", checking list: complete.wav, completed.wav both exist)
        // User asked for "file transfer completed", using "completed.wav"
        playVoiceFeedback("completed.wav"); 
        delay(100);
        // "Ready"
        playVoiceFeedback("ready.wav");
    }

    Serial.printf("\n  Summary: %d copied, %d skipped, %d pruned\n", 
                  filesCopied, filesSkipped, filesDeleted);
    return true;
}


// ===================================
// Scan SD Banks (2-6 with optional pages)
// ===================================
void scanSDBanks() {
    sdBankCount = 0;
    mutex_enter_blocking(&sd_mutex);
    FsFile root = sd.open("/");
    
    if (!root || !root.isDirectory()) {
        Serial.println("ERROR: Could not open root directory");
        mutex_exit(&sd_mutex);
        return;
    }
    
    FsFile dir;
    while (dir.openNext(&root, O_RDONLY)) {
        if (dir.isDirectory()) {
            char dirName[64];
            dir.getName(dirName, sizeof(dirName));
            
            // Match pattern: [2-6][A-Z]?_[Name]
            if (strlen(dirName) >= 2 &&
                dirName[0] >= '2' && dirName[0] <= '6') {
                
                uint8_t bankNum = dirName[0] - '0';
                char page = 0;
                
                // Check for page letter
                if (strlen(dirName) >= 3 && 
                    dirName[1] >= 'A' && dirName[1] <= 'Z' &&
                    dirName[2] == '_') {
                    page = dirName[1];
                }
                else if (dirName[1] != '_') {
                    // Invalid format
                    dir.close();
                    continue;
                }
                
                // Create new bank entry
                if (sdBankCount < MAX_SD_BANKS) {
                    SDBank* bank = &sdBanks[sdBankCount];
                    bank->bankNum = bankNum;
                    bank->page = page;
                    strncpy(bank->dirName, dirName, sizeof(bank->dirName) - 1);
                    bank->fileCount = 0;
                    
                    // Scan files in this directory
                    char fullPath[80];
                    snprintf(fullPath, sizeof(fullPath), "/%s", dirName);
                    FsFile bankDir = sd.open(fullPath);
                    
                    if (bankDir && bankDir.isDirectory()) {
                        FsFile file;
                        while (file.openNext(&bankDir, O_RDONLY)) {
                            if (!file.isDirectory() && bank->fileCount < MAX_FILES_PER_BANK) {
                                char filename[64];
                                file.getName(filename, sizeof(filename));
                                
                                const char* ext = strrchr(filename, '.');
                                if (ext && (strcasecmp(ext, ".wav") == 0 ||
                                           strcasecmp(ext, ".mp3") == 0 ||
                                           strcasecmp(ext, ".aac") == 0 ||
                                           strcasecmp(ext, ".m4a") == 0)) {
                                    strncpy(bank->files[bank->fileCount], filename,
                                            sizeof(bank->files[0]) - 1);
                                    bank->fileCount++;
                                }
                            }
                            file.close();
                        }
                        bankDir.close();
                    }
                    
                    sdBankCount++;
                }
            }
        }
        dir.close();
    }
    
    root.close();
    mutex_exit(&sd_mutex);
}


// ===================================
// Find SD Bank by number and page
// ===================================
SDBank* findSDBank(uint8_t bank, char page) {
    for (int i = 0; i < sdBankCount; i++) {
        if (sdBanks[i].bankNum == bank && sdBanks[i].page == page) {
            return &sdBanks[i];
        }
    }
    return nullptr;
}


// ===================================
// Get File from SD Bank
// ===================================
const char* getSDFile(uint8_t bank, char page, int index) {
    SDBank* sdBank = findSDBank(bank, page);
    if (!sdBank) return nullptr;
    
    if (index < 1 || index > sdBank->fileCount) return nullptr;
    
    return sdBank->files[index - 1];
}


// ===================================
// Scan Root Tracks (Legacy Compatibility)
// ===================================
void scanRootTracks() {
    rootTrackCount = 0;
    mutex_enter_blocking(&sd_mutex);
    FsFile root = sd.open("/");
    
    if (!root || !root.isDirectory()) {
        Serial.println("ERROR: Could not open root directory for legacy scan");
        mutex_exit(&sd_mutex);
        return;
    }
    
    FsFile file;
    while (file.openNext(&root, O_RDONLY)) {
        if (!file.isDirectory()) {
            char filename[64];
            file.getName(filename, sizeof(filename));
                       
            // index all valid audio files in SD root
            const char* ext = strrchr(filename, '.');
            if (ext && (strcasecmp(ext, ".wav") == 0 ||
                       strcasecmp(ext, ".mp3") == 0 ||
                       strcasecmp(ext, ".aac") == 0 ||
                       strcasecmp(ext, ".m4a") == 0)) {
                
                if (rootTrackCount < MAX_ROOT_TRACKS) {
                    strncpy(rootTracks[rootTrackCount], filename, sizeof(rootTracks[0]) - 1);
                    rootTrackCount++;
                }
            }
        }
        file.close();
    }
    root.close();
    mutex_exit(&sd_mutex);
    
    // Sort the tracks alphabetically to ensure deterministic order
    // (Bubble sort is fine for < 255 items)
    for (int i = 0; i < rootTrackCount - 1; i++) {
        for (int j = 0; j < rootTrackCount - i - 1; j++) {
            if (strcasecmp(rootTracks[j], rootTracks[j+1]) > 0) {
                char temp[16];
                strncpy(temp, rootTracks[j], sizeof(temp));
                strncpy(rootTracks[j], rootTracks[j+1], sizeof(rootTracks[j]));
                strncpy(rootTracks[j+1], temp, sizeof(temp));
            }
        }
    }
    
    Serial.printf("Found %d root tracks for legacy compatibility.\n", rootTrackCount);
}