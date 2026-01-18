#include "config.h"

// ===================================
// Helper: Serial Output
// ===================================
// Sends to USB Serial immediately, queues for Serial2
void sendSerialResponse(Stream &serial, const char* msg) {
    if (&serial == &Serial) {
        // USB Serial: Send immediately for debugging
        serial.println(msg);
    } else if (&serial == &Serial2) {
        // Serial2: Queue the message
        queueSerial2Message(msg);
    }
}

void sendSerialResponseF(Stream &serial, const char* format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    sendSerialResponse(serial, buffer);
}

// ===================================
// Helper: Parsing
// ===================================

// Advances ptr to next comma or end of string. Returns true if comma found.
bool skipToNextArg(char*& ptr) {
    if (!ptr) return false;
    char* comma = strchr(ptr, ',');
    if (comma) {
        ptr = comma + 1;
        return true;
    }
    ptr = ptr + strlen(ptr); // Point to terminator
    return false;
}

// Parses int and advances ptr. If no int found, returns defaultValue.
int parseArgInt(char*& ptr, int defaultValue = 0) {
    if (!ptr || *ptr == '\0' || *ptr == '\r' || *ptr == '\n') return defaultValue;
    
    // Check if empty argument (e.g. "1,,3")
    if (*ptr == ',') return defaultValue;

    int val = atoi(ptr);
    skipToNextArg(ptr);
    return val;
}

// Parses single char and advances ptr.
char parseArgChar(char*& ptr, char defaultValue = 0) {
    if (!ptr || *ptr == '\0' || *ptr == '\r' || *ptr == '\n' || *ptr == ',') return defaultValue;
    
    char c = *ptr;
    skipToNextArg(ptr);
    return c;
}

// Helper to find the next available stream
int getNextAvailableStream() {
    // 1. Try to find an inactive stream
    if (streams) {
        for (int i = 0; i < maxStreams; i++) {
            if (!streams[i].active) {
                return i;
            }
        }
    }
    // 2. All busy? Steal Stream 0.
    return 0;
}

// ===================================
// Command Handlers
// ===================================

void handlePlay(Stream &serial, char* args) {
    // Format: PLAY:index,bank,page,volume
    // or PLAY:index
    
    char* ptr = args;
    
    // 1. Index (Required)
    if (*ptr == '\0' || *ptr == '\r' || *ptr == '\n') {
       serial.println("ERR:PARAM - Format: PLAY:index,bank,page,volume");
       return;
    }
    
    int index = parseArgInt(ptr);
    int bank = parseArgInt(ptr, 1); // Default Bank 1
    
    // Parse Page carefully (char)
    char page = 'A'; // Default Page A
    if (*ptr != '\0' && *ptr != '\r' && *ptr != '\n') {
        if (*ptr == ',') {
             // Empty page argument, use default 'A' or '0'? 
             // Logic in original was slightly complex. Let's look at ptr directly.
             ptr++; // Skip comma
        } else {
             char c = *ptr;
             if (c >= 'a' && c <= 'z') c -= 32; // Uppercase
             
             if ((c >= 'A' && c <= 'Z') || c == '0') {
                 page = c;
             }
             skipToNextArg(ptr);
        }
    }

    int volume = parseArgInt(ptr, -1); // Default -1 (Current)
    
    int stream = getNextAvailableStream();
    if (stream < 0 || stream >= maxStreams) {
        serial.println("ERR:PARAM - Invalid stream");
        return;
    }

    if (bank == 1) {
        if (index >= 1 && index <= bank1SoundCount) {
            
            // Pick random variant, avoiding the last-played one
            SoundFile& sound = bank1Sounds[index - 1];
            int variantIdx;

            if (sound.variantCount == 1) {
                variantIdx = 0; 
            } else {
                variantIdx = random(sound.variantCount);
                if (variantIdx == sound.lastVariantPlayed) {
                    variantIdx = (variantIdx + 1) % sound.variantCount;
                }
            }
            
            sound.lastVariantPlayed = variantIdx;
            const char* filename = sound.variants[variantIdx];
            
            // Dynamic path (Flash or SD)
            char fullPath[80];
            if (useFlashForBank1) {
                snprintf(fullPath, sizeof(fullPath), "/flash/%s", filename);
            } else {
                snprintf(fullPath, sizeof(fullPath), "/%s/%s", bank1DirName, filename);
            }
            
            // Common Playback Execution
            sendSerialResponse(serial, "PACK:PLAY");
            sendSerialResponseF(serial, "S:%d,ply,%d", stream, volume);

            if (startStream(stream, fullPath)) {
                if (volume >= 0) {
                    if (volume > 99) volume = 99;
                    streams[stream].volume = (float)volume / 99.0f;
                }
            } else {
                serial.println("ERR:NOFILE");
            }
        } else {
            serial.println("ERR:PARAM - Invalid sound index");
        }
    }
    else if (bank >= 2 && bank <= 6) {
        const char* filename = getSDFile(bank, page, index);
        if (filename) {
            SDBank* sdBank = findSDBank(bank, page);
            char fullPath[128];
            snprintf(fullPath, sizeof(fullPath), "/%s/%s", sdBank->dirName, filename);
            
            sendSerialResponse(serial, "PACK:PLAY");
            sendSerialResponseF(serial, "S:%d,ply,%d", stream, volume);

            if (startStream(stream, fullPath)) {
                if (volume >= 0) {
                    if (volume > 99) volume = 99;
                    streams[stream].volume = (float)volume / 99.0f;
                }
            } else {
                serial.println("ERR:NOFILE");
            }
        } else {
            serial.println("ERR:PARAM - Invalid file index");
        }
    }
    else {
        serial.println("ERR:PARAM - Invalid bank");
    }
}

void handleStop(Stream &serial, char* args) {
    if (args[0] == '\0' || args[0] == '*') {
        // Stop all
        for (int i = 0; i < maxStreams; i++) {
            stopStream(i);
            sendSerialResponse(serial, "PACK:STOP");
            sendSerialResponseF(serial, "S:%d,idle,,0", i);
        }
    } else {
        int stream = atoi(args);
        if (stream >= 0 && stream < maxStreams) {
            stopStream(stream);
            sendSerialResponse(serial, "PACK:STOP");
            sendSerialResponseF(serial, "S:%d,idle,,0", stream);
        } else {
            serial.println("ERR:PARAM - Invalid stream");
        }
    }
}

void handleChirp(Stream &serial, char* args) {
    // Format: CHRP:StartHz,EndHz,DurationMs,Volume
    char* ptr = args;
    
    int start = parseArgInt(ptr);
    int end = parseArgInt(ptr);
    int ms = parseArgInt(ptr);
    int vol = parseArgInt(ptr, 128); // Default 128
    
    playChirp(start, end, ms, vol);
    sendSerialResponse(serial, "PACK:CHRP");
}

void handleVolume(Stream &serial, char* args) {
    char* ptr = args;
    
    // Check if we have two arguments (Stream, Vol) or one (Global Vol)
    // Simple heuristic: Does args contain a comma?
    char* comma = strchr(args, ',');
    
    if (comma) {
        // Stream specific
        int stream = parseArgInt(ptr);
        int volume = parseArgInt(ptr);
        
        if (volume < 0) volume = 0;
        if (volume > 99) volume = 99;
        
        if (stream >= 0 && stream < maxStreams) {
            if (streams) streams[stream].volume = (float)volume / 99.0f;
            sendSerialResponse(serial, "PACK:SVOL");
        } else {
            serial.println("ERR:PARAM - Invalid stream");
        }
    } else {
        // Global
        int volume = parseArgInt(ptr);
        if (volume < 0) volume = 0;
        if (volume > 99) volume = 99;

        if (streams) {
            for (int i = 0; i < maxStreams; i++) {
                streams[i].volume = (float)volume / 99.0f;
            }
        }
        sendSerialResponse(serial, "PACK:SVOL");
    }
}

void handleList(Stream &serial) {
    serial.println("\n=== Bank 1 (Flash) ===");
    serial.printf("Sounds: %d\n", bank1SoundCount);
    for (int i = 0; i < bank1SoundCount && i < 10; i++) {
        serial.printf("  %2d. %s (%d variants)\n",
                      i + 1,
                      bank1Sounds[i].basename,
                      bank1Sounds[i].variantCount);
    }
    if (bank1SoundCount > 10) {
        serial.printf("  ... and %d more\n", bank1SoundCount - 10);
    }
    
    serial.println("\n=== Banks 2-6 (SD) ===");
    for (int i = 0; i < sdBankCount; i++) {
        serial.printf("Bank %d%c: %s (%d files)\n",
                      sdBanks[i].bankNum,
                      sdBanks[i].page ? sdBanks[i].page : ' ',
                      sdBanks[i].dirName,
                      sdBanks[i].fileCount);
    }
    serial.println();
}

void handleGman(Stream &serial) {
    sendSerialResponseF(serial, "MDAT:%d", sdBankCount + 1);
    // Send full directory name for Bank 1
    sendSerialResponseF(serial, "BANK:1,%s,%d", 
                  bank1DirName, 
                  bank1SoundCount);

    for (int i = 0; i < sdBankCount; i++) {
        sendSerialResponseF(serial, "BANK:%d,%s,%d",
                     sdBanks[i].bankNum,
                     sdBanks[i].dirName,
                     sdBanks[i].fileCount);
    }
    
    sendSerialResponseF(serial, "MSUM:%lu", globalFilenameChecksum);
    sendSerialResponse(serial, "MEND");
}

void handleGnme(Stream &serial, char* args) {
    char* ptr = args;
    int bank = parseArgInt(ptr);
    
    // Page is tricky again due to potential empty value
    char page = 0;
    if (*ptr == ',') {
        ptr++; // Empty page
    } else if (*ptr != '\0') {
        page = *ptr;
        skipToNextArg(ptr);
    }
    
    int index = parseArgInt(ptr);
    if (index == 0) return; // invalid

    if (bank == 1 && index >= 1 && index <= bank1SoundCount) {
        sendSerialResponseF(serial, "NAME:1,,%d,%s.wav",
                      index,
                      bank1Sounds[index - 1].basename);
    }
    else if (bank >= 2 && bank <= 6 && index >= 1) {
        const char* filename = getSDFile(bank, page, index);
        if (filename) {
            sendSerialResponseF(serial, "NAME:%d,%c,%d,%s",
                         bank, page == 0 ? ',' : page, 
                         index, filename);
        } else {
            sendSerialResponseF(serial, "NAME:%d,%c,%d,INVALID",
                         bank, page == 0 ? ',' : page, index);
        }
    }
}

void handleCcrc(Stream &serial) {
    serial.println("CMD: CCRC - Clearing Flash...");
    
    for (int i = 0; i < maxStreams; i++) {
        stopStream(i);
    }
    
    int count = 0;
    Dir dir = LittleFS.openDir("/flash");
    while (dir.next()) {
        if (!dir.isDirectory()) {
            String fullPath = "/flash/" + dir.fileName();
            if (LittleFS.remove(fullPath)) {
                count++;
            }
        }
    }
    
    serial.printf("Deleted %d files from /flash.\n", count);
    serial.println("Please REBOOT the board to re-sync files.");
    sendSerialResponse(serial, "PACK:CCRC");
}

void handleStat(Stream &serial, char* args) {
    int stream = atoi(args);
    if (stream >= 0 && stream < maxStreams) {
        if (streams && streams[stream].active) {
            int vol = (int)(streams[stream].volume * 99.0f);
            serial.printf("STAT:playing,%s,%d\n",
                         streams[stream].filename, vol);
        } else {
            serial.printf("STAT:idle,,0\n");
        }
    } else {
        serial.println("ERR:PARAM - Invalid stream");
    }
}

void handleBaud(Stream &serial, char* args) {
    long rate = atol(args);
    if (rate == 2400 || rate == 9600 || rate == 19200 || 
        rate == 38400 || rate == 57600 || rate == 115200) {
        baudRate = rate;
        writeIniFile();
        sendSerialResponse(serial, "PACK:BAUD");
        sendSerialResponseF(serial, "BAUD:%ld", baudRate);
        
        Serial2.end();
        Serial2.begin(baudRate);
    } else {
        serial.println("ERR:PARAM - Invalid baud rate");
    }
}

void handleBpage(Stream &serial, char* args) {
    char page = args[0];
    if (page >= 'a' && page <= 'z') page -= 32;
    
    if (page >= 'A' && page <= 'Z') {
        activeBank1Page = page;
        writeIniFile();
        sendSerialResponse(serial, "PACK:BPAGE");
        sendSerialResponseF(serial, "BPAGE:%c", activeBank1Page);
        serial.println("Note: Reboot required to reload Bank 1.");
    } else {
        serial.println("ERR:PARAM - Invalid page (A-Z)");
    }
}

void handleMusb(Stream &serial, char* args) {
    // Check for explicit argument 0 or 1
    if (args[0] == ':') {
        int val = atoi(args + 1);
        if (val == 1) {
            startMSC();
            sendSerialResponse(serial, "PACK:MUSB");
            sendSerialResponse(serial, "MUSB:1");
        } else {
            stopMSC();
            sendSerialResponse(serial, "PACK:MUSB");
            sendSerialResponse(serial, "MUSB:0");
        }
    } else {
        // Toggle
        if (g_mscActive) {
            stopMSC();
            sendSerialResponse(serial, "PACK:MUSB");
            sendSerialResponse(serial, "MUSB:0");
        } else {
            startMSC();
            sendSerialResponse(serial, "PACK:MUSB");
            sendSerialResponse(serial, "MUSB:1");
        }
    }
}

// ===================================
// Main Serial Processing Loop
// ===================================
void processSerialCommands(Stream &serial) {
    static char usbCmdBuffer[128];
    static int usbCmdPos = 0;
    static char uartCmdBuffer[128];
    static int uartCmdPos = 0;

    char* cmdBuffer;
    int* cmdPosPtr; 

    if (&serial == &Serial) {
        cmdBuffer = usbCmdBuffer;
        cmdPosPtr = &usbCmdPos;
    } else if (&serial == &Serial2) {
        cmdBuffer = uartCmdBuffer;
        cmdPosPtr = &uartCmdPos;
    } else {
        return; 
    }
    
    int& cmdPos = *cmdPosPtr; 
    
    while (serial.available()) {                
        char c = serial.read();
        
        // Try compat layer first (ONLY if we are not already building a command)
        if (cmdPos == 0 && checkAndHandleMp3Command(serial, (uint8_t)c)) {
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (cmdPos > 0) {
                cmdBuffer[cmdPos] = '\0';
                
                // Debug Logging
                if (&serial == &Serial2) {
                    Serial.printf("RX [UART]: %s\n", cmdBuffer);
                }

                // --- Dispatch Commands ---
                
                if (strncmp(cmdBuffer, "PLAY:", 5) == 0) {
                    handlePlay(serial, cmdBuffer + 5);
                }
                else if (strncmp(cmdBuffer, "STOP", 4) == 0) {
                    // STOP or STOP: param
                    char* args = cmdBuffer + 4;
                    if (*args == ':') args++;
                    handleStop(serial, args);
                }
                else if (strncmp(cmdBuffer, "CHRP:", 5) == 0) {
                    handleChirp(serial, cmdBuffer + 5);
                }
                else if (strncmp(cmdBuffer, "VOL:", 4) == 0) {
                    handleVolume(serial, cmdBuffer + 4);
                }
                else if (strcmp(cmdBuffer, "LIST") == 0) {
                    handleList(serial);
                }
                else if (strcmp(cmdBuffer, "GMAN") == 0) {
                    handleGman(serial);
                }
                else if (strncmp(cmdBuffer, "GNME:", 5) == 0) {
                    handleGnme(serial, cmdBuffer + 5);
                }
                else if (strcmp(cmdBuffer, "CCRC") == 0) {
                    handleCcrc(serial);
                }
                else if (strncmp(cmdBuffer, "STAT:", 5) == 0) {
                    handleStat(serial, cmdBuffer + 5);
                }
                else if (strncmp(cmdBuffer, "BAUD:", 5) == 0) {
                    handleBaud(serial, cmdBuffer + 5);
                }
                else if (strncmp(cmdBuffer, "BPAGE:", 6) == 0) {
                    handleBpage(serial, cmdBuffer + 6);
                }
                else if (strncmp(cmdBuffer, "MUSB", 4) == 0) {
                    handleMusb(serial, cmdBuffer + 4); // Handles :1, :0 or empty (toggle)
                }
                else {
                    serial.println("ERR:UNKNOWN");
                }
                
                cmdPos = 0; 
            }
        }
        else if (cmdPos < (sizeof(usbCmdBuffer) - 1)) {
            cmdBuffer[cmdPos++] = c;
        }
    }
}