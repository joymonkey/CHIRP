#include "config.h"

// ===================================
// MP4 Parser Implementation
// ===================================

MP4Parser::MP4Parser() {
    usingFlash = false;
    stszOffset = 0;
    stcoOffset = 0;
    stscOffset = 0;
    mdatOffset = 0;
    sampleRate = 44100;
    channels = 2;
    currentSample = 0;
    totalSamples = 0;
}

void MP4Parser::close() {
    if (usingFlash) {
        if (flashFile) flashFile.close();
    } else {
        if (sdFile) sdFile.close();
    }
}

// Helpers for reading Big Endian
uint32_t MP4Parser::readUI32BE(File &f) {
    uint8_t buf[4];
    f.read(buf, 4);
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}
uint32_t MP4Parser::readUI32BE(FsFile &f) {
    uint8_t buf[4];
    f.read(buf, 4);
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}
uint32_t MP4Parser::readUI32BE() {
    if (usingFlash) return readUI32BE(flashFile);
    return readUI32BE(sdFile);
}
void MP4Parser::seek(uint32_t pos) {
    if (usingFlash) flashFile.seek(pos);
    else sdFile.seek(pos);
}
void MP4Parser::read(uint8_t* buf, size_t len) {
    if (usingFlash) flashFile.read(buf, len);
    else sdFile.read(buf, len);
}
uint32_t MP4Parser::getPos() {
    if (usingFlash) return flashFile.position();
    return sdFile.position();
}

bool MP4Parser::open(const char* filename, bool isFlash) {
    usingFlash = isFlash;
    
    if (usingFlash) {
        // Mutex handling is done by the caller (startStream/fillStreamBuffers)
        flashFile = LittleFS.open(filename, "r");
        if (!flashFile) return false;
    } else {
        // Mutex handling is done by the caller
        sdFile = sd.open(filename, FILE_READ);
        if (!sdFile) return false;
    }
    
    // Reset State
    stszOffset = 0; stcoOffset = 0; stscOffset = 0; mdatOffset = 0;
    currentSample = 0; currentChunk = 1; samplesReadInChunk = 0;
    
    // Scan Atoms
    uint32_t fileSize = (usingFlash) ? flashFile.size() : sdFile.size();
    uint32_t pos = 0;
    bool moovFound = false;
    uint32_t atomCount = 0;
    
    #ifdef DEBUG
    Serial.printf("MP4Parser: Opening %s (Size: %u)\n", filename, fileSize);
    #endif

    while (pos < fileSize && atomCount < 1000) { // Limit atom count
        seek(pos);
        uint32_t atomSize = readUI32BE();
        uint32_t atomType = readUI32BE();
        atomCount++;
        
        // Sanity Check
        if (atomSize == 0) {
             // Size 0 means "rest of file"
             break;
        }
        if (atomSize == 1) {
             // Extended size
             seek(pos + 8);
             uint32_t high = readUI32BE();
             uint32_t low = readUI32BE();
             atomSize = low; 
        }
        
        if (atomSize < 8) {
            #ifdef DEBUG
            Serial.printf("MP4Parser: Invalid atom size %u at pos %u\n", atomSize, pos);
            #endif
            break; // Corrupt
        }

        #ifdef DEBUG
        // Print FourCC
        char typeStr[5];
        typeStr[0] = (char)(atomType >> 24);
        typeStr[1] = (char)(atomType >> 16);
        typeStr[2] = (char)(atomType >> 8);
        typeStr[3] = (char)(atomType);
        typeStr[4] = 0;
        Serial.printf("  Atom: %s, Size: %u, Pos: %u\n", typeStr, atomSize, pos);
        #endif

        if (atomType == 0x6D6F6F76) { // 'moov'
            parseMoov(atomSize - 8);
            moovFound = true;
        } else if (atomType == 0x6D646174) { // 'mdat'
            mdatOffset = pos + 8; 
            #ifdef DEBUG
            Serial.printf("  -> Found mdat at %u\n", mdatOffset);
            #endif
        }
        
        pos += atomSize;
        if (pos > fileSize) break; // formatting error
    }
    
    if (moovFound && stszOffset != 0 && stcoOffset != 0 && stscOffset != 0) {
        #ifdef DEBUG
        Serial.printf("MP4Parser: Success! Rate: %u, Ch: %u, Samples: %u\n", sampleRate, channels, totalSamples);
        #endif
        
        // Initialize Reading State
        // Read first STSC entry
        seek(stscOffset + 12); // Skip version/flags + count
        // We need to preload the first run
        seek(stscOffset + 16); // First entry
        currentChunk = 1; // 1-based
        uint32_t firstChunk = readUI32BE();
        samplesInCurrentChunk = readUI32BE();
        // Skip ID
        
        // Initial Offset from STCO
        seek(stcoOffset + 16); // First entry
        currentOffset = readUI32BE();
        
        return true;
    }
    
    #ifdef DEBUG
    Serial.println("MP4Parser: Failed to find all required tables");
    #endif
    return false;
}

bool MP4Parser::parseMoov(uint32_t atomSize) {
    uint32_t startPos = getPos();
    uint32_t endPos = startPos + atomSize;
    uint32_t count = 0;
    
    #ifdef DEBUG
    Serial.println("  Parsing moov...");
    #endif

    while (getPos() < endPos && count < 500) {
        count++;
        uint32_t size = readUI32BE();
        uint32_t type = readUI32BE();
        
        if (size < 8) break; // Error
        
        uint32_t nextAtomPos = getPos() + size - 8;
        
        #ifdef DEBUG
        char typeStr[5];
        typeStr[0] = (char)(type >> 24); typeStr[1] = (char)(type >> 16);
        typeStr[2] = (char)(type >> 8); typeStr[3] = (char)(type); typeStr[4] = 0;
        Serial.printf("  moov->child: %s, Size: %u\n", typeStr, size);
        #endif

        if (type == 0x7472616B) { // 'trak'
            if (parseTrak(size - 8)) {
                 // Found valid audio track - Do NOT return yet if we want to debug other tracks
                 // But for efficiency we return true
                 return true;
            }
        }
        
        seek(nextAtomPos); 
    }
    return false;
}

bool MP4Parser::parseTrak(uint32_t atomSize) {
    uint32_t startPos = getPos();
    uint32_t endPos = startPos + atomSize;
    bool isAudio = false;
    uint32_t count = 0;
    
    // Reset Track State 
    stszOffset = 0; stcoOffset = 0; stscOffset = 0;
    
    #ifdef DEBUG
    Serial.println("  Parsing trak...");
    #endif
    
    while (getPos() < endPos && count < 500) {
        count++;
        uint32_t size = readUI32BE();
        uint32_t type = readUI32BE();
        
        if (size < 8) break;

        // Calculate Next Atom Position *immediately*
        uint32_t nextAtomPos = getPos() + size - 8;
        
        #ifdef DEBUG
        char typeStr[5];
        typeStr[0] = (char)(type >> 24); typeStr[1] = (char)(type >> 16);
        typeStr[2] = (char)(type >> 8); typeStr[3] = (char)(type); typeStr[4] = 0;
        Serial.printf("    trak->Atom: %s, Size: %u\n", typeStr, size);
        #endif
        
        if (type == 0x6D646961) { // 'mdia'
             // Recurse into mdia
             // We can just iterate here since we have the range
             uint32_t mdiaEnd = getPos() + size - 8; // Content size
             uint32_t c2 = 0;
             
             while (getPos() < mdiaEnd && c2 < 100) {
                 c2++;
                 uint32_t s2 = readUI32BE();
                 uint32_t t2 = readUI32BE();
                 if (s2 < 8) break;
                 
                 uint32_t nextMdiaChild = getPos() + s2 - 8;
                 
                 #ifdef DEBUG
                 char t2Str[5];
                 t2Str[0] = (char)(t2 >> 24); t2Str[1] = (char)(t2 >> 16);
                 t2Str[2] = (char)(t2 >> 8); t2Str[3] = (char)(t2); t2Str[4] = 0;
                 Serial.printf("      mdia->Atom: %s\n", t2Str);
                 #endif
                 
                 if (t2 == 0x68646C72) { // 'hdlr'
                     // Check handler type
                     seek(getPos() + 8); // Version/Flags + ComponentType
                     uint32_t subtype = readUI32BE();
                     if (subtype == 0x736F756E) { // 'soun'
                         isAudio = true; 
                         #ifdef DEBUG
                         Serial.println("      -> Handler is AUDIO");
                         #endif
                     }
                 } else if (t2 == 0x6D696E66) { // 'minf'
                     // Recurse into minf
                     uint32_t minfEnd = getPos() + s2 - 8;
                     uint32_t c3 = 0;
                     while (getPos() < minfEnd && c3 < 100) {
                         c3++;
                         uint32_t s3 = readUI32BE();
                         uint32_t t3 = readUI32BE();
                         if (s3 < 8) break;
                         
                         uint32_t nextMinfChild = getPos() + s3 - 8;
                         
                         if (t3 == 0x7374626C) { // 'stbl'
                             // Found Sample Table!
                             // Parse ALWAYS, check isAudio later
                             uint32_t stblEnd = getPos() + s3 - 8;
                             uint32_t c4 = 0;
                             #ifdef DEBUG
                             Serial.println("        -> Parsing stbl");
                             #endif
                             while (getPos() < stblEnd && c4 < 100) {
                                 c4++;
                                 uint32_t s4 = readUI32BE();
                                 uint32_t t4 = readUI32BE();
                                 if (s4 < 8) break;
                                 
                                 uint32_t nextStblChild = getPos() + s4 - 8;
                                 
                                 if (t4 == 0x7374737A) stszOffset = getPos() - 8; // stsz
                                 else if (t4 == 0x7374636F) stcoOffset = getPos() - 8; // stco
                                 else if (t4 == 0x73747363) stscOffset = getPos() - 8; // stsc
                                 else if (t4 == 0x73747364) { // stsd
                                     parseStsd(s4 - 8);
                                 }
                                 
                                 seek(nextStblChild);
                             }
                         }
                         seek(nextMinfChild);
                     }
                 }
                 seek(nextMdiaChild);
             }
        }
        seek(nextAtomPos);
    }
    
    // Final Check
    if (isAudio && stszOffset != 0 && stcoOffset != 0 && stscOffset != 0) {
        return true;
    }
    
    return false;
}

bool MP4Parser::parseStsd(uint32_t atomSize) {
    // Version (1) + Flags (3) + Count (4)
    seek(getPos() + 8);
    // Audio Sample Entry
    // Size (4) + Format (4) 'mp4a'
    uint32_t size = readUI32BE();
    uint32_t format = readUI32BE();
    
    if (format == 0x6D703461) { // 'mp4a'
        // Skip reserved (6) + index (2)
        seek(getPos() + 8);
        // Version (2) + Revision (2) + Vendor (4)
        seek(getPos() + 8);
        // Channels (2) + Size (2) + CompressionId (2) + PacketSize (2)
        uint16_t chans = (readUI32BE() >> 16);
        channels = chans;
        seek(getPos() + 4); // Size+Comp+Pkt
        // Sample Rate (4) 16.16 fixed point
        sampleRate = (readUI32BE() >> 16);
        
        // Parsing ES_DS for ADTS config (Profile, etc.)
        // This is deep... usually we can guess:
        // LC (Low Complexity) is profile 2 minus 1 = 1
        // Freq Index depends on sampleRate
        // Standard for M4A is usually LC-AAC
        return true;
    }
    return false;
}

// Generate ADTS Header
// 7 bytes required for Helix to sync
void generateAdtsHeader(uint8_t* header, int len, int profile, int sampleRate, int channels) {
    int freqIdx = 4; // Default 44100
    if (sampleRate >= 96000) freqIdx = 0;
    else if (sampleRate >= 88200) freqIdx = 1;
    else if (sampleRate >= 64000) freqIdx = 2;
    else if (sampleRate >= 48000) freqIdx = 3;
    else if (sampleRate >= 44100) freqIdx = 4;
    else if (sampleRate >= 32000) freqIdx = 5;
    else if (sampleRate >= 24000) freqIdx = 6;
    else if (sampleRate >= 22050) freqIdx = 7;
    else if (sampleRate >= 16000) freqIdx = 8;
    else if (sampleRate >= 12000) freqIdx = 9;
    
    // Sync Word (0xFFF)
    header[0] = 0xFF;
    header[1] = 0xF1; // MPEG-4, No CRC
    
    // Profile (2 bits), Freq (4), Channel (3)
    // Profile: 1 (Main), 2 (LC), 3 (SSR) - ADTS uses profile-1
    // Usually M4A is LC (2), so put 1 (01 binary)
    // header[2] = ((profile - 1) << 6) | (freqIdx << 2) | (channels >> 2);
    // Let's assume Profile 2 (LC) -> 1
    header[2] = (1 << 6) | (freqIdx << 2) | ((channels >> 2) & 0x01);
    
    header[3] = ((channels & 0x03) << 6) | (len >> 11);
    header[4] = (len >> 3) & 0xFF;
    header[5] = ((len & 0x07) << 5) | 0x1F;
    header[6] = 0xFC;
}

size_t MP4Parser::readNextFrame(uint8_t* buffer, size_t bufferSize) {
    if (stszOffset == 0) return 0;
    
    // 1. Get Sample Size
    // Seek to STSZ table entry
    // Header(12) + Size(4) + Count(4) -> Data starts at +20
    // If Size != 0, all samples are same size.
    
    // Optimization: Cache stsz mode?
    // Doing strict seek every time for memory safety
    seek(stszOffset + 12);
    uint32_t defaultSize = readUI32BE();
    uint32_t count = readUI32BE();
    
    if (currentSample >= count) return 0; // EOF
    
    uint32_t frameSize = defaultSize;
    if (frameSize == 0) {
        seek(stszOffset + 20 + (currentSample * 4));
        frameSize = readUI32BE();
    }
    
    // Check buffer space (ADTS + Frame)
    if (frameSize + 7 > bufferSize) return 0; // Too big
    
    // 2. Get File Offset (Handle Chunks)
    // Simple logic:
    // If samplesReadInChunk >= samplesInCurrentChunk, move to next chunk
    // Update currentOffset from STCO
    
    if (samplesReadInChunk >= samplesInCurrentChunk) {
        // Next Chunk
        currentChunk++;
        samplesReadInChunk = 0;
        
        // Get Offset from STCO
        seek(stcoOffset + 16 + ((currentChunk - 1) * 4));
        currentOffset = readUI32BE();
        
        // Update Samples Per Chunk (STSC)
        // Check if next chunk starts a new run
        /* 
           STSC Table:
           FirstChunk, SamplesPerChunk, SampleDescId
           1, 4, 1
           10, 8, 1
           
           Means: Chunks 1-9 have 4 samples. Chunk 10+ has 8.
        */
        // This part is tricky without keeping state.
        // Simplification: We need to re-read STSC or maintain state.
        // Let's assume we maintain stscIndex, nextChunkRunStart, samplesInCurrentChunk
        // TODO: Implement robust STSC logic.
        // For now, assuming fixed samples per chunk or simple lookup every time (slow but safe).
        
        seek(stscOffset + 12);
        uint32_t entryCount = readUI32BE();
        // Find entry for currentChunk
        for (uint32_t i=0; i<entryCount; i++) {
             uint32_t firstChunk = readUI32BE();
             uint32_t samplesPer = readUI32BE();
             readUI32BE(); // ID
             
             if (firstChunk > currentChunk) break;
             samplesInCurrentChunk = samplesPer;
        }
    }
    
    // 3. Read Frame
    seek(currentOffset);
    
    generateAdtsHeader(buffer, frameSize + 7, 2, sampleRate, channels);
    read(buffer + 7, frameSize);
    
    currentOffset += frameSize;
    currentSample++;
    samplesReadInChunk++;
    
    return frameSize + 7;
}
