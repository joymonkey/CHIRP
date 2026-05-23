#ifndef CRSF_INTERCEPTOR_H
#define CRSF_INTERCEPTOR_H

#include <Arduino.h>

class CRSFInterceptor : public Stream {
public:
  CRSFInterceptor(Stream& underlyingSerial);
  
  void update(); // Call this frequently to pump bytes from serial to buffer
  
  // Stream overrides
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t) override;
  size_t write(const uint8_t *buffer, size_t size) override;

private:
  Stream& _serial;
  
  // Ring buffer for AlfredoCRSF to read from
  static const int BUF_SIZE = 256;
  uint8_t _buf[BUF_SIZE];
  int _head;
  int _tail;
  
  // State machine for CRSF parsing
  enum State {
    STATE_SYNC,
    STATE_LENGTH,
    STATE_TYPE,
    STATE_PAYLOAD
  };
  
  State _state;
  uint8_t _frameLength;
  uint8_t _frameType;
  uint8_t _frameBuf[64];
  uint8_t _frameIdx;
  
  void pushToBuffer(uint8_t b);
  void pushFrameToBuffer();
  void handleCustomPacket();
  uint8_t calcCRC(const uint8_t *data, uint8_t len);
};

#endif // CRSF_INTERCEPTOR_H
