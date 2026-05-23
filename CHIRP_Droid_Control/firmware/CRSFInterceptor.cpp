#include "CRSFInterceptor.h"
#include "config_manager.h"
#include "debug.h"
#include "audio_link.h"

// CRSF CRC8 implementation
static const uint8_t crc8_tab256[] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54, 0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06, 0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0, 0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2, 0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9, 0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B, 0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D, 0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F, 0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB, 0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9, 0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F, 0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D, 0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26, 0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74, 0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82, 0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0, 0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

uint8_t CRSFInterceptor::calcCRC(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc = crc8_tab256[crc ^ data[i]];
    }
    return crc;
}

CRSFInterceptor::CRSFInterceptor(Stream& underlyingSerial) 
  : _serial(underlyingSerial), _head(0), _tail(0), _state(STATE_SYNC) {
}

void CRSFInterceptor::pushToBuffer(uint8_t b) {
  int next = (_head + 1) % BUF_SIZE;
  if (next != _tail) {
    _buf[_head] = b;
    _head = next;
  }
}

void CRSFInterceptor::pushFrameToBuffer() {
  pushToBuffer(0xC8);
  pushToBuffer(_frameLength);
  pushToBuffer(_frameType);
  for (int i=0; i<_frameIdx; i++) {
    pushToBuffer(_frameBuf[i]);
  }
}

void CRSFInterceptor::update() {
  while (_serial.available()) {
    uint8_t b = _serial.read();
    
    switch (_state) {
      case STATE_SYNC:
        if (b == 0xC8) {
          _state = STATE_LENGTH;
        } else {
          pushToBuffer(b); // Pass-through
        }
        break;
        
      case STATE_LENGTH:
        _frameLength = b;
        if (_frameLength > 64 || _frameLength < 2) {
          // Invalid length
          pushToBuffer(0xC8);
          pushToBuffer(b);
          _state = STATE_SYNC;
        } else {
          _state = STATE_TYPE;
        }
        break;
        
      case STATE_TYPE:
        _frameType = b;
        _frameIdx = 0;
        _state = STATE_PAYLOAD;
        break;
        
      case STATE_PAYLOAD:
        _frameBuf[_frameIdx++] = b;
        if (_frameIdx == _frameLength - 1) { // -1 because Type is part of length
          // Frame complete
          if (_frameType == 0x32) { // CRSF_FRAMETYPE_COMMAND
            uint8_t crcData[64];
            crcData[0] = _frameType;
            for (int i=0; i<_frameIdx-1; i++) {
              crcData[i+1] = _frameBuf[i];
            }
            uint8_t expectedCrc = calcCRC(crcData, _frameLength - 1);
            uint8_t actualCrc = _frameBuf[_frameIdx-1];
            
            if (expectedCrc == actualCrc) {
              handleCustomPacket();
            } else {
              pushFrameToBuffer(); // Invalid CRC, pass along
            }
          } else {
            // Not our target packet, pass along
            pushFrameToBuffer();
          }
          _state = STATE_SYNC;
        }
        break;
    }
  }
}

void CRSFInterceptor::handleCustomPacket() {
  // Payload: [Dest] [Src] [ID] [Value]
  // In CRSF COMMAND, Dest=0xC8 (FC), Src=0xEA (Radio)
  if (_frameIdx < 4) return; // Dest + Src + ID + Value + CRC
  
  if (_frameBuf[0] != 0xC8) {
    pushFrameToBuffer(); // Not for FC, pass along
    return;
  }
  
  uint8_t paramId = _frameBuf[2];
  uint8_t paramValue = _frameBuf[3];
  
  DBG_EVENT(DBG_GENERAL, "Config Packet Received! ID: %d, Val: %d", paramId, paramValue);
  
  bool dirty = false;
  
  switch(paramId) {
    case 1: // bank1Page
      userConfig.bank1Page = (char)paramValue;
      dirty = true;
      {
        char bpageCmd[16];
        snprintf(bpageCmd, sizeof(bpageCmd), "BPAGE:%c", userConfig.bank1Page);
        audioSendCommand(bpageCmd);
      }
      break;
    case 2: // domeOffset
      userConfig.domeOffset = (int8_t)paramValue; // Send as int8_t
      dirty = true;
      break;
    case 3: // domeInvert
      userConfig.domeInvert = (paramValue > 0);
      dirty = true;
      break;
    case 4: // voiceDebug
      userConfig.voiceDebug = (paramValue > 0);
      dirty = true;
      break;
    case 5: // leftMotorInvert
      userConfig.leftMotorInvert = (paramValue > 0);
      dirty = true;
      break;
    case 6: // rightMotorInvert
      userConfig.rightMotorInvert = (paramValue > 0);
      dirty = true;
      break;
    case 7: // speedSlow (percentage 0-100)
      userConfig.speedSlow = paramValue;
      dirty = true;
      break;
    case 8: // speedMed
      userConfig.speedMed = paramValue;
      dirty = true;
      break;
    case 9: // speedFast
      userConfig.speedFast = paramValue;
      dirty = true;
      break;
  }
  
  if (dirty) {
    saveConfig();
  }
}

int CRSFInterceptor::available() {
  return (_head - _tail + BUF_SIZE) % BUF_SIZE;
}

int CRSFInterceptor::read() {
  if (_head == _tail) return -1;
  uint8_t b = _buf[_tail];
  _tail = (_tail + 1) % BUF_SIZE;
  return b;
}

int CRSFInterceptor::peek() {
  if (_head == _tail) return -1;
  return _buf[_tail];
}

void CRSFInterceptor::flush() {
  _serial.flush();
}

size_t CRSFInterceptor::write(uint8_t b) {
  return _serial.write(b);
}

size_t CRSFInterceptor::write(const uint8_t *buffer, size_t size) {
  return _serial.write(buffer, size);
}
