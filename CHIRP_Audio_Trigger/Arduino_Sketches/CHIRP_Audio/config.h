#ifndef CONFIG_H
#define CONFIG_H

#include <SdFat.h>
#include <LittleFS.h>
#include <I2S.h>
#include "pico/mutex.h"
#include "MP3DecoderHelix.h"
#include "AACDecoderHelix.h"
#include <Adafruit_TinyUSB.h>

using namespace libhelix;

// ===================================
// Constants
// ===================================
#define VERSION_STRING "20260117"
//#define DEBUG // Comment out to disable debug logging

// Hardware Configuration
#define SD_CS   13
#define SD_MISO 12
#define SD_MOSI 15
#define SD_SCK  14
#define I2S_BCLK 9
#define I2S_LRCK 10
#define I2S_DATA 11
#define NEOPIXEL_PIN 19 //CHIRP Audio Trigger PCB has 3 neopixels on pin 19
#define PIN_MSC_TRIGGER 7 // Pull low to enable MSC mode

// UART Pins (Serial2)
#define UART_TX 4
#define UART_RX 5

// Button Configuration
#define PIN_BTN_NAV 17 // Start/Stop
#define PIN_BTN_FWD 16 // Next
#define PIN_BTN_REV 18 // Prev

// Development Mode
#define DEV_MODE true
#define DEV_SYNC_LIMIT 100
#define FORMAT_FLASH false

// Audio Configuration
#define SAMPLE_RATE 44100
#define DEFAULT_MAX_STREAMS 3
#define DEFAULT_STREAM_BUFFER_KB 512

// Bank/File Limits
#define MAX_SOUNDS 100
#define MAX_SD_BANKS 20
#define MAX_FILES_PER_BANK 100

// Outgoing Serial Message Queue
#define SERIAL2_QUEUE_SIZE 16
#define SERIAL2_MSG_MAX_LENGTH 128

// ===================================
// Struct Definitions
// ===================================
struct WAVHeader {
    char riff[4];
    uint32_t fileSize;
    char wave[4];
    char fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4];
    uint32_t dataSize;
};

struct SoundFile {
    char basename[16];
    char variants[25][32];
    int variantCount;
    int lastVariantPlayed; // For non-repeating random
};

struct SDBank {
    uint8_t bankNum;
    char page;
    char dirName[32];
    char files[MAX_FILES_PER_BANK][64];
    int fileCount;
};

struct SerialMessage {
    char buffer[SERIAL2_MSG_MAX_LENGTH];
    uint8_t length;
};

struct SerialQueue {
    SerialMessage messages[SERIAL2_QUEUE_SIZE];
    volatile int readPos;
    volatile int writePos;
    uint32_t messagesSent;
    uint32_t messagesDropped;
};

// ===================================
// Extern Global Variables
// ===================================

// File Systems
extern SdFat sd;
extern I2S i2s;

// Thread Safety
extern mutex_t sd_mutex;
extern mutex_t flash_mutex;
extern mutex_t log_mutex;




// Bank File Lists
extern SoundFile bank1Sounds[MAX_SOUNDS];
extern int bank1SoundCount;
extern char bank1DirName[64]; 

extern char activeBank1Page;
extern char validBank1Pages[27];
extern int validBank1PageCount;
extern SDBank sdBanks[MAX_SD_BANKS];
extern int sdBankCount;
extern bool useFlashForBank1;


// Root Tracks (Legacy Compatibility)
#define MAX_ROOT_TRACKS 255
extern char rootTracks[MAX_ROOT_TRACKS][16]; // "NNN.MP3"
extern int rootTrackCount;

// Test Tone State
extern volatile bool testToneActive;
extern volatile uint32_t testTonePhase;
extern volatile int16_t masterAttenMultiplier;
#define TEST_TONE_FREQ 440
#define PHASE_INCREMENT ((uint32_t)TEST_TONE_FREQ << 16) / SAMPLE_RATE

// MP3 Decoder
extern MP3DecoderHelix* mp3Decoder;

// Configuration
extern long baudRate;

// Filename Checksum
extern uint32_t globalFilenameChecksum;

// Outgoing Serial Message Queue
extern SerialQueue serial2Queue;

// Control I2S Hardware State from Core 0
extern volatile bool g_allowAudio;

// MSC State
extern volatile bool g_mscActive;
extern Adafruit_USBD_MSC* usb_msc;

// Legacy Compatibility
extern bool legacyMonophonic;

// ===================================
// Flexible Audio Architecture
// ===================================

// Global Configuration Variables
extern int maxStreams;
extern int maxMp3Decoders;
extern int streamBufferSize; // Size in SAMPLES (not bytes)
extern int streamBufferMask; // Mask for bitwise wrapping (size - 1)

enum StreamType {
    STREAM_TYPE_INACTIVE = 0,
    STREAM_TYPE_WAV_FLASH, 
    STREAM_TYPE_WAV_SD,
    STREAM_TYPE_MP3_SD,
    STREAM_TYPE_MP3_FLASH,
    STREAM_TYPE_AAC_SD,
    STREAM_TYPE_AAC_FLASH,
    STREAM_TYPE_M4A_SD,
    STREAM_TYPE_M4A_FLASH
};

enum AudioFormat {
    FORMAT_UNKNOWN = 0,
    FORMAT_WAV,
    FORMAT_MP3,
    FORMAT_AAC,
    FORMAT_M4A,
    FORMAT_OGG
};

struct RingBuffer {
    int16_t* buffer; // Pointer to PSRAM
    volatile int readPos;
    volatile int writePos;
    
    // Helper to get available write space
    int availableForWrite() {
        if (!buffer) return 0;
        int currentLevel = (writePos - readPos + streamBufferSize) & streamBufferMask;
        return (streamBufferSize - 1) - currentLevel;
    }
    
    // Helper to get available samples to read
    int availableForRead() {
        if (!buffer) return 0;
        return (writePos - readPos + streamBufferSize) & streamBufferMask;
    }
    
    bool push(int16_t sample) {
        if (!buffer) return false;
        
        int nextWrite = (writePos + 1) & streamBufferMask;
        if (nextWrite == readPos) {
            // Buffer Full - Drop sample
            return false;
        }
        
        buffer[writePos] = sample;
        writePos = nextWrite;
        return true;
    }
    
    int16_t pop() {
        if (!buffer) return 0;
        int16_t sample = buffer[readPos];
        readPos = (readPos + 1) & streamBufferMask;
        return sample;
    }
    
    void clear() {
        readPos = 0;
        writePos = 0;
        if (buffer) {
            // Optional: memset(buffer, 0, streamBufferSize * sizeof(int16_t));
        }
    }
};

// ===================================
// MP4/M4A Parser Class
// ===================================
class MP4Parser {
public:
    MP4Parser();
    bool open(const char* filename, bool isFlash);
    void close();
    size_t readNextFrame(uint8_t* buffer, size_t bufferSize);
    
    // Config getters
    uint32_t getSampleRate() { return sampleRate; }
    uint8_t getChannels() { return channels; }
    
private:
    // Files
    File flashFile;
    FsFile sdFile;
    bool usingFlash;
    
    // Structure Offsets
    uint32_t stszOffset;
    uint32_t stcoOffset;
    uint32_t stscOffset;
    uint32_t mdatOffset;
    
    // Track Info
    uint32_t sampleRate;
    uint8_t channels;
    uint8_t objectType; // for ADTS
    
    // Playback State
    uint32_t currentSample;
    uint32_t totalSamples;
    
    // Chunk State
    uint32_t currentChunk;
    uint32_t samplesInCurrentChunk;
    uint32_t samplesReadInChunk;
    uint32_t currentOffset; // Absolute file offset
    
    // STSC Cursor (to know when samplesPerChunk changes)
    uint32_t stscCount;
    uint32_t stscIndex; 
    uint32_t nextChunkRunStart;
    
    // Data Widths
    uint8_t stszSampleSize; // 0 if variable
    
    // Helpers
    uint32_t readUI32BE(File &f);
    uint32_t readUI32BE(FsFile &f);
    uint32_t readUI32BE(); // wrappers that use active file
    void seek(uint32_t pos);
    void read(uint8_t* buf, size_t len);
    uint32_t getPos();
    
    bool findAtom(const char* atomName, uint32_t &atomSize, uint32_t limit);
    bool parseMoov(uint32_t atomSize);
    bool parseTrak(uint32_t atomSize);
    bool parseMdhd(uint32_t atomSize);
    bool parseHdlr(uint32_t atomSize);
    bool parseStsd(uint32_t atomSize);
};

struct AudioStream {
    bool active;
    StreamType type;
    float volume; // 0.0 to 1.0
    int decoderIndex; // -1 if not using MP3/AAC decoder
    
    // File Handles
    File flashFile; // For LittleFS
    FsFile sdFile;  // For SdFat
    
    // MP4 Parser
    MP4Parser mp4Parser;
    
    // Buffer
    RingBuffer* ringBuffer;
    
    // State
    char filename[64];
    bool stopRequested;
    bool fileFinished;
    uint8_t channels; // 1 = Mono, 2 = Stereo
    uint32_t sampleRate; // Source sample rate (e.g. 44100 or 22050)
    uint32_t startTime; // Debug timestamp
};

extern AudioStream* streams;
extern RingBuffer* streamBuffers;
extern MP3DecoderHelix** mp3Decoders;
extern bool* mp3DecoderInUse;
extern AACDecoderHelix** aacDecoders;
extern bool* aacDecoderInUse;

// ===================================
// Function Prototypes
// ===================================

// from serial_commands.cpp
void log_message(const String& msg);
void processSerialCommands(Stream &serial); // Dual-buffer fix

// from file_management.cpp
bool parseIniFile();
void writeIniFile();
void scanValidBank1Pages();
void scanBank1();
bool syncBank1ToFlash();
void playFirmwareUpdateFeedback(bool fwUpdated);

void scanSDBanks();
void scanRootTracks();
SDBank* findSDBank(uint8_t bank, char page);
const char* getSDFile(uint8_t bank, char page, int index);
void playVoiceFeedback(const char* filename); // Exposed for other files
void playVoiceNumber(int number); // Exposed for other files
void playBaudFeedback(long rate); // Helper for baud rate feedback
void playBankNameFeedback(char page); // Helper for Bank Name feedback
AudioFormat getAudioFormat(const char* filename); // Helper to get format from extension
bool isAudioFile(const char* filename); // Helper to check if file is supported

// from audio_playback.cpp
void mp3DataCallback(MP3FrameInfo &info, int16_t *pcm_buffer, size_t len, void* ref);
void aacDataCallback(AACFrameInfo &info, int16_t *pcm_buffer, size_t len, void* ref);
bool startStream(int streamIdx, const char* filename);
void stopStream(int streamIdx);
void fillStreamBuffers(); // Main loop task
void initAudioSystem();
void playChirp(int startFreq, int endFreq, int durationMs, uint8_t vol);

// from serial_commands.cpp (MP3 Trigger Compat)
void action_togglePlayPause();
void action_playNext();
void action_playPrev();
void action_playTrackById(int trackNum);
void action_playTrackByIndex(int trackIndex);
void action_setSparkfunVolume(uint8_t sfVol);
bool checkAndHandleMp3Command(Stream &s, uint8_t firstByte);

// from serial_queue.cpp
void initSerial2Queue();
bool queueSerial2Message(const char* msg);
void trySendQueuedMessages(int maxMessages);
bool isCpuBusy();
int getQueuedMessageCount();

// from blinkies.cpp
void initBlinkies();
void playStartupSequence();
void playErrorSequence();
void updateSyncLEDs(bool fileTransferEvent = false);
void updateRuntimeLEDs();

// from msc_interface.h
void setupMSC();
void pollMSCTrigger();
void startMSC();
void stopMSC();

#endif // CONFIG_H