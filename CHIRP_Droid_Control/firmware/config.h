#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
#define DEBUG_ENABLED // Comment out for production builds
#define DEBUG_BAUD 115200
#define DEBUG_CATEGORIES DBG_ALL      // Bitmask: DBG_GENERAL|DBG_CRSF|DBG_AUDIO|etc
#define DEBUG_STATUS_INTERVAL_MS 5000 // Periodic status dump interval

// Voice Debug - spoken status via CHIRP_Audio PVOICE command
#define DEBUG_VOICE_ENABLED       // Comment out to disable voice announcements
#define VOICE_WORD_TIMEOUT_MS 500 // Max wait for PACK:PVOICE ACK
#define VOICE_WORD_GAP_MS 100     // Pause between words in a phrase
#define VOICE_COOLDOWN_MS 3000    // Min interval between same-priority announcements

// ============================================================================
// HARDWARE PINS (Raspberry Pi Pico 2 W - RP2350)
// ============================================================================

// Serial1 (HW UART0) - CRSF / ELRS RX
#define CRSF_TX_PIN 0
#define CRSF_RX_PIN 1

// Serial2 (HW UART1) - CHIRP_Audio
#define AUDIO_TX_PIN 4
#define AUDIO_RX_PIN 5

// SerialPIO (PIO0) - Roam-A-Dome
#define RAD_TX_PIN 8
#define RAD_RX_PIN 9

// SerialPIO (PIO1) - Marcduino (Future)
#define MARC_TX_PIN 16
#define MARC_RX_PIN 17

// PWM Outputs
#define LEFT_FOOT_PWM_PIN 10
#define RIGHT_FOOT_PWM_PIN 11
#define DOME_PWM_PIN 12

// Other IO
#define STATUS_LED_PIN LED_BUILTIN
#define VBAT_PIN 26
#define VBAT2_PIN 27
#define VBAT3_PIN 28

// ============================================================================
// SERIAL BAUD RATES
// ============================================================================
#define CRSF_BAUD 420000
#define AUDIO_BAUD 115200
#define RAD_BAUD 9600
#define MARC_BAUD 9600

// ============================================================================
// RC CHANNEL ASSIGNMENTS (Radiomaster Zorro Profile)
// ============================================================================
#define CH_ROLL 1
#define CH_PITCH 2
#define CH_AUTODOME_FREQ 3
#define CH_YAW 4
#define CH_ARM 5
#define CH_VOLUME 6
#define CH_SOUND_INDEX 7
#define CH_PLAY_TRIGGER 8
// CH 9 Available
#define CH_SPEED_MODE 10
#define CH_AUTO_TOGGLE 11 // 3-pos momentary: UP=toggle Autodome, DOWN=toggle Autochirp
// CH 12 Available
#define CH_PAGE_SELECT 13

// ============================================================================
// MOTOR DRIVE CONFIGURATION
// ============================================================================
#define RC_MIN_US 988
#define RC_MAX_US 2012
#define SERVO_MIN_US 1000
#define SERVO_MAX_US 2000
#define SERVO_STOP_US 1500

#define SPEED_FACTOR_SLOW 0.3
#define SPEED_FACTOR_MED 0.7
#define SPEED_FACTOR_FAST 1.0

#define DRIVE_DEADZONE 50
#define DOME_DEADZONE 20

// ============================================================================
// SOUND BANK CONFIGURATION
// ============================================================================
#define MAX_BANKS 4
#define MAX_PAGES 3 // A, B, C
#define MAX_SOUNDS 24
#define MAX_FILENAME_LEN 11 // Max 11 chars due to CRSF limitations (14 byte packet - 3 byte header)

// ============================================================================
// TIMING & DEBOUNCE
// ============================================================================
#define TELEMETRY_RATE_MS 30
#define CACHE_SYNC_TIMEOUT_MS 1000
#define AUTODOME_MIN_INTERVAL_MS 5000
#define AUTODOME_MAX_INTERVAL_MS 30000
#define AUTOCHIRP_MIN_INTERVAL_MS 15000
#define AUTOCHIRP_MAX_INTERVAL_MS 45000
#define VBAT_READ_INTERVAL_MS 1000

// ============================================================================
// CRSF FRAME TYPES
// ============================================================================
#ifndef CRSF_FRAMETYPE_FLIGHT_MODE
#define CRSF_FRAMETYPE_FLIGHT_MODE 0x21
#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct Sound
{
  char name[MAX_FILENAME_LEN + 1];
};

struct BankPage
{
  char name[MAX_FILENAME_LEN + 1];
  char letter; // 'A', 'B', 'C'
  uint8_t soundCount;
  Sound sounds[MAX_SOUNDS];
};

struct SoundBank
{
  BankPage pages[MAX_PAGES]; // Index 0=A, 1=B, 2=C
};

struct AudioManifest
{
  SoundBank banks[MAX_BANKS]; // Index 0=Bank1, 1=Bank2...
};

enum SyncState
{
  SYNC_IDLE,
  SYNC_REQUESTING,
  SYNC_WAITING,
  SYNC_COMPLETE
};

struct RobotState
{
  float battery1;
  float battery2;
  float battery3;
  float currentDraw;
  float peakCurrent;

  bool rcLinkUp;
  bool isArmed;
  uint8_t speedMode; // 1=slow, 2=med, 3=fast
  uint8_t volume;    // 0-100

  bool autodomeEnabled;
  bool autochirpEnabled;

  uint8_t currentPage; // 0='A', 1='B', 2='C'
  uint16_t domeAngle;
};

#endif // CONFIG_H