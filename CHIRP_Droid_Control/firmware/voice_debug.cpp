#include "voice_debug.h"
#include "globals.h"
#include "debug.h"
#include <stdarg.h>

bool voiceAckReceived = false;

#ifdef DEBUG_VOICE_ENABLED

#define QUEUE_SIZE 16
static const char* wordQueue[QUEUE_SIZE];
static uint8_t queueHead = 0;
static uint8_t queueTail = 0;

enum VoiceState {
  VOICE_IDLE,
  VOICE_SENDING,
  VOICE_COOLDOWN
};

static VoiceState voiceState = VOICE_IDLE;
static uint32_t stateTime = 0;
static uint32_t lastAnnouncementTime[4] = {0, 0, 0, 0};
static uint8_t currentPriority = 3;

void voiceDebugInit() {
  queueHead = 0;
  queueTail = 0;
  voiceState = VOICE_IDLE;
  for(int i=0; i<4; i++) lastAnnouncementTime[i] = 0;
}

static void clearQueue() {
  queueHead = 0;
  queueTail = 0;
}

static bool pushWord(const char* word) {
  uint8_t nextHead = (queueHead + 1) % QUEUE_SIZE;
  if (nextHead == queueTail) return false; // Full
  wordQueue[queueHead] = word;
  queueHead = nextHead;
  return true;
}

static const char* popWord() {
  if (queueHead == queueTail) return nullptr; // Empty
  const char* word = wordQueue[queueTail];
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  return word;
}

static bool isQueueEmpty() {
  return queueHead == queueTail;
}

void voiceDebugSayPriority(uint8_t priority, const char* word1, ...) {
  if (priority > 3) priority = 3;
  
  uint32_t now = millis();
  
  if (now - lastAnnouncementTime[priority] < VOICE_COOLDOWN_MS) {
    return; // Cooldown active for this priority
  }
  
  if (voiceState != VOICE_IDLE) {
    if (priority < currentPriority) {
      // Preempt!
      clearQueue();
    } else {
      // Drop
      return;
    }
  }
  
  lastAnnouncementTime[priority] = now;
  currentPriority = priority;
  
  va_list args;
  va_start(args, word1);
  const char* word = word1;
  while (word != nullptr) {
    pushWord(word);
    word = va_arg(args, const char*);
  }
  va_end(args);
}

void voiceDebugSay(const char* word1, ...) {
  uint32_t now = millis();
  uint8_t priority = 3;
  
  if (now - lastAnnouncementTime[priority] < VOICE_COOLDOWN_MS) return;
  
  if (voiceState != VOICE_IDLE) {
    if (priority < currentPriority) {
      clearQueue();
    } else {
      return;
    }
  }
  lastAnnouncementTime[priority] = now;
  currentPriority = priority;
  
  va_list args;
  va_start(args, word1);
  const char* word = word1;
  while (word != nullptr) {
    pushWord(word);
    word = va_arg(args, const char*);
  }
  va_end(args);
}

void voiceDebugUpdate() {
  uint32_t now = millis();
  
  switch (voiceState) {
    case VOICE_IDLE:
      if (!isQueueEmpty()) {
        const char* word = popWord();
        if (word) {
          SerialAudio.printf("PVOICE:%s\n", word);
          DBG_EVENT(DBG_AUDIO, "→ TX: PVOICE:%s", word);
          voiceAckReceived = false;
          voiceState = VOICE_SENDING;
          stateTime = now;
        }
      }
      break;
      
    case VOICE_SENDING:
      if (voiceAckReceived || (now - stateTime > VOICE_WORD_TIMEOUT_MS)) {
        voiceState = VOICE_COOLDOWN;
        stateTime = now;
      }
      break;
      
    case VOICE_COOLDOWN:
      if (now - stateTime > VOICE_WORD_GAP_MS) {
        voiceState = VOICE_IDLE;
      }
      break;
  }
}

#else

void voiceDebugInit() {}
void voiceDebugUpdate() {}
void voiceDebugSay(const char* word1, ...) {}
void voiceDebugSayPriority(uint8_t priority, const char* word1, ...) {}

#endif