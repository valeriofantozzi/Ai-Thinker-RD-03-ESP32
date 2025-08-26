#include "RadarSensor.h"

RadarSensor::RadarSensor(uint8_t rxPin, uint8_t txPin)
  : radarSerial(rxPin, txPin)
{}

void RadarSensor::begin(unsigned long baud) {
  radarSerial.begin(baud);
}

// Parser state-machine for UART data
bool RadarSensor::update() {
  static uint8_t buffer[30]; 
  static size_t index = 0;
  static enum {WAIT_AA, WAIT_FF, WAIT_03, WAIT_00, RECEIVE_FRAME} state = WAIT_AA;

  bool data_updated = false;

  while (radarSerial.available()) {
    byte byteIn = radarSerial.read();

    switch(state) {
      case WAIT_AA:
        if(byteIn == 0xAA) state = WAIT_FF;
        break;

      case WAIT_FF:
        if(byteIn == 0xFF) state = WAIT_03;
        else state = WAIT_AA;
        break;

      case WAIT_03:
        if(byteIn == 0x03) state = WAIT_00;
        else state = WAIT_AA;
        break;

      case WAIT_00:
        if(byteIn == 0x00) {
          index = 0;
          state = RECEIVE_FRAME;
        } else state = WAIT_AA;
        break;

      case RECEIVE_FRAME:
        buffer[index++] = byteIn;
        if(index >= 26) { // 24 bytes data + 2 tail bytes
          if(buffer[24] == 0x55 && buffer[25] == 0xCC) {
            data_updated = parseData(buffer, 24);
          }
          state = WAIT_AA;
          index = 0;
        }
        break;
    }
  }
  return data_updated;
}

bool RadarSensor::parseData(const uint8_t *buf, size_t len) {
  if(len != 24)
    return false;

  // Only parse first 8 bytes for the first target
  int16_t raw_x = buf[0] | (buf[1] << 8);
  int16_t raw_y = buf[2] | (buf[3] << 8);
  int16_t raw_speed = buf[4] | (buf[5] << 8);
  uint16_t raw_pixel_dist = buf[6] | (buf[7] << 8);

  target.detected = !(raw_x == 0 && raw_y == 0 && raw_speed == 0 && raw_pixel_dist == 0);

  // correctly parse signed valuss
  target.x = ((raw_x & 0x8000) ? 1 : -1) * (raw_x & 0x7FFF);
  target.y = ((raw_y & 0x8000) ? 1 : -1) * (raw_y & 0x7FFF);
  target.speed = ((raw_speed & 0x8000) ? 1 : -1) * (raw_speed & 0x7FFF);

  if (target.detected) {
    target.distance = sqrt(target.x * target.x + target.y * target.y);
    
    // angle calculation (convert radians to degrees, then flip)
    float angleRad = atan2(target.y, target.x) - (PI / 2);
    float angleDeg = angleRad * (180.0 / PI);
    target.angle = -angleDeg; // align angle with x measurement positive / negative sign
  } else {
    target.distance = 0.0;
    target.angle = 0.0;
  }
  
  return true;
}

RadarTarget RadarSensor::getTarget() {
  return target;
}