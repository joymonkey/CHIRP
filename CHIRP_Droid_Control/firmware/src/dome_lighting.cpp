#include "dome_lighting.h"
#include <SoftwareSerial.h>

static EspSoftwareSerial::UART s_domeLightSerial;

void domeLightingInit() {
    // Enable internal pull-up on RX pin first so an unconnected dome harness never floats or triggers ISR storms
    pinMode(PIN_DOME_LIGHT_RX, INPUT_PULLUP);
    s_domeLightSerial.enableRxGPIOPullUp(true);

    // Initialize SoftSerial (TX=GP12 for Marcduino commands, RX=-1 disabled until needed)
    s_domeLightSerial.begin(
        DOME_LIGHT_BAUD_RATE,
        EspSoftwareSerial::SWSERIAL_8N1,
        -1, // TX-only Marcduino link prevents floating-pin GPIO ISR lockups
        PIN_DOME_LIGHT_TX,
        false
    );

    Serial.printf("[LGT] Dome Lighting SoftSerial initialized (TX=GP%d @ %lu baud)\n",
                  PIN_DOME_LIGHT_TX, (unsigned long)DOME_LIGHT_BAUD_RATE);
    Serial.flush();
}

void domeLightingSendCommand(const char* marcduinoCmd) {
    if (marcduinoCmd == nullptr || marcduinoCmd[0] == '\0') return;
    s_domeLightSerial.print(marcduinoCmd);
    s_domeLightSerial.print('\r'); // Marcduino / RSeries commands terminate with CR
    Serial.printf("[LGT] Sent Marcduino -> %s\n", marcduinoCmd);
}

void domeLightingUpdate() {
    // Reserved for future bidirectional dome queries
}
