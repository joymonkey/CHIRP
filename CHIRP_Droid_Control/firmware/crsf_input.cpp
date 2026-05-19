#include "crsf_input.h"
#include "globals.h"
#include "debug.h"
#include "voice_debug.h"
#include "audio_link.h"
#include <AlfredoCRSF.h>

AlfredoCRSF crsf;
static int lastZone = 0;
static bool stopTriggered = false;
static bool enteredFromIdle = false; // Only trigger sound if button was pressed from idle (zone 0)

static bool lastLinkUp = false;
static bool lastArmed = false;
static uint8_t lastSpeedMode = 1;
static int8_t lastAutoTogglePos = 0; // -1=down, 0=center, 1=up
static uint8_t lastPage = 0;

void crsfInit()
{
  SerialCRSF.setRX(CRSF_RX_PIN);
  SerialCRSF.setTX(CRSF_TX_PIN);
  SerialCRSF.begin(CRSF_BAUD, SERIAL_8N1);
  crsf.begin(SerialCRSF);
}

void crsfUpdate()
{
  crsf.update();

  bool linkUp = crsf.isLinkUp();
  robotState.rcLinkUp = linkUp; // Expose to global state for debug status
  if (linkUp != lastLinkUp)
  {
    DBG_EVENT(DBG_CRSF, "RC Link: %s", linkUp ? "UP" : "DOWN");
    if (linkUp)
    {
      VSAY_P(1, "receiver", "connected", nullptr);
    }
    else
    {
      VSAY_P(0, "warning", "receiver", "disconnected", nullptr);
    }
    lastLinkUp = linkUp;
  }

  if (!linkUp)
  {
    if (robotState.isArmed)
    {
      // Failsafe
      robotState.isArmed = false;
      rcChannels[CH_ROLL] = 1500;
      rcChannels[CH_PITCH] = 1500;
      rcChannels[CH_YAW] = 1500;
      rcChannels[CH_AUTODOME_FREQ] = 1500;
    }
    return;
  }

  // Read all channels
  for (int i = 1; i <= 16; i++)
  {
    rcChannels[i] = crsf.getChannel(i);
  }

  // Update Robot State
  robotState.isArmed = (rcChannels[CH_ARM] > 1500);
  if (robotState.isArmed != lastArmed)
  {
    DBG_EVENT(DBG_CRSF, "Arm → %s", robotState.isArmed ? "ARMED" : "DISARMED");
    if (robotState.isArmed)
    {
      VSAY_P(1, "foot_drives", "armed", nullptr);
    }
    else
    {
      VSAY_P(1, "foot_drives", "disarmed", nullptr);
    }
    lastArmed = robotState.isArmed;
  }

  // Speed Mode (3-pos)
  uint16_t speedCh = rcChannels[CH_SPEED_MODE];
  if (speedCh < 1200)
    robotState.speedMode = 1;
  else if (speedCh < 1800)
    robotState.speedMode = 2;
  else
    robotState.speedMode = 3;
  if (robotState.speedMode != lastSpeedMode)
  {
    DBG_EVENT(DBG_CRSF, "Speed → %d", robotState.speedMode);
    const char *speedWord = "slow";
    if (robotState.speedMode == 2)
      speedWord = "medium";
    else if (robotState.speedMode == 3)
      speedWord = "fast";
    VSAY_P(2, "speed", speedWord, nullptr);
    lastSpeedMode = robotState.speedMode;
  }

  // Autodome / Autochirp (3-pos momentary toggle on Ch 11)
  // UP (>1800) = toggle Autodome, DOWN (<1200) = toggle Autochirp, CENTER = idle
  uint16_t autoToggleCh = rcChannels[CH_AUTO_TOGGLE];
  int8_t autoTogglePos = 0; // center
  if (autoToggleCh > 1800)
    autoTogglePos = 1; // up
  else if (autoToggleCh < 1200)
    autoTogglePos = -1; // down

  // Detect press edge (center → up or center → down)
  if (autoTogglePos != lastAutoTogglePos)
  {
    if (autoTogglePos == 1 && lastAutoTogglePos == 0)
    {
      // Trim UP pressed → toggle Autodome
      robotState.autodomeEnabled = !robotState.autodomeEnabled;
      DBG_EVENT(DBG_CRSF, "Autodome → %s", robotState.autodomeEnabled ? "ON" : "OFF");
      VSAY_P(2, "auto", "dome", robotState.autodomeEnabled ? "enabled" : "disabled", nullptr);
    }
    else if (autoTogglePos == -1 && lastAutoTogglePos == 0)
    {
      // Trim DOWN pressed → toggle Autochirp
      robotState.autochirpEnabled = !robotState.autochirpEnabled;
      DBG_EVENT(DBG_CRSF, "Autochirp → %s", robotState.autochirpEnabled ? "ON" : "OFF");
      VSAY_P(2, "auto", "chirp", robotState.autochirpEnabled ? "enabled" : "disabled", nullptr);
    }
    lastAutoTogglePos = autoTogglePos;
  }

  // Page Select (3-pos: A/B/C)
  uint16_t pageCh = rcChannels[CH_PAGE_SELECT];
  if (pageCh < 1200)
    robotState.currentPage = 0; // A
  else if (pageCh > 1800)
    robotState.currentPage = 2; // C
  else
    robotState.currentPage = 1; // B
  if (robotState.currentPage != lastPage)
  {
    DBG_EVENT(DBG_CRSF, "Page → %c", 'A' + robotState.currentPage);
    const char *pageWord = (robotState.currentPage == 0) ? "_a" : (robotState.currentPage == 1) ? "_b"
                                                                                                : "_c";
    VSAY_P(2, "page", pageWord, nullptr);
    lastPage = robotState.currentPage;
  }

  // Volume
  // Map 988-2012 to 0-100
  static uint8_t lastVolume = 255; // Init to impossible value to force first update
  int volRaw = rcChannels[CH_VOLUME];
  if (volRaw < 1000)
    robotState.volume = 0;
  else if (volRaw > 2000)
    robotState.volume = 100;
  else
    robotState.volume = map(volRaw, 1000, 2000, 0, 100);
  
  // Send global volume to CHIRP_Audio when it changes (±2 deadband to avoid knob jitter)
  if (abs((int)robotState.volume - (int)lastVolume) > 1) {
    audioSetVolume(robotState.volume);
    lastVolume = robotState.volume;
  }
  DBG_THROTTLE(DBG_CRSF, 500, "Volume: %d", robotState.volume);

  // 6-Zone Trigger Logic
  // On the Radiomaster Zorro we have 4 momentary buttons.
  // We assign these 4 buttons to a single channel with a range of values
  // when the button is released.
  // The 4 buttons trigger a sound from 1 of the 4 sound banks.
  // Button SD = Sound Bank 1
  // Button SA = Sound Bank 2
  // Button SG = Sound Bank 3
  // Button SH = Sound Bank 4
  // Pressing SD and SA together will stop all sounds.
  uint16_t trigCh = rcChannels[CH_PLAY_TRIGGER];
  int currentZone = 0;

  if (trigCh > 2000)
  {
    currentZone = 5; // Stop All
  }
  else if (trigCh >= 900 && trigCh < 1150)
  {
    currentZone = 1;
  }
  else if (trigCh >= 1150 && trigCh < 1350)
  {
    currentZone = 2;
  }
  else if (trigCh >= 1650 && trigCh < 1850)
  {
    currentZone = 3;
  }
  else if (trigCh >= 1850 && trigCh <= 2000)
  {
    currentZone = 4;
  }
  else
  {
    currentZone = 0; // Idle (1350 - 1650)
  }

  // Stop immediately if zone 5
  if (currentZone == 5)
  {
    if (!stopTriggered)
    {
      onStopAll();
      stopTriggered = true;
    }
    enteredFromIdle = false; // Releasing from stop-all should NOT trigger a sound
  }
  else
  {
    stopTriggered = false; // Reset stop flag if we leave zone 5
  }

  // Track whether a button press originated from idle
  if (currentZone >= 1 && currentZone <= 4 && lastZone == 0)
  {
    enteredFromIdle = true; // Clean single-button press from idle
    DBG_EVENT(DBG_CRSF, "Entered Zone %d", currentZone);
  }

  // Trigger on release (transition to 0 from 1-4), but ONLY if we entered from idle
  if (currentZone == 0 && lastZone >= 1 && lastZone <= 4 && enteredFromIdle)
  {
    DBG_EVENT(DBG_CRSF, "Zone %d Triggered", lastZone);
    onPlayTrigger(lastZone);
    enteredFromIdle = false;
  }

  // If we return to idle from zone 5 (via a brief pass through 1-4), just reset
  if (currentZone == 0 && !enteredFromIdle)
  {
    enteredFromIdle = false; // Ensure clean state for next press
  }

  lastZone = currentZone;
}