// ESP32 Radar RD-03D con Serial2 hardware UART
// Parsing diretto dei pacchetti senza libreria SoftwareSerial

typedef struct RadarTarget {
  float distance;  // mm
  float angle;     // gradi
  float speed;     // cm/s
  int16_t x;       // mm
  int16_t y;       // mm
  bool detected;
} RadarTarget;

RadarTarget target;

void setup() {
  Serial.begin(115200);
  Serial2.begin(256000, SERIAL_8N1, 16, 17); // RX=16, TX=17
  Serial.println("ESP32 Radar RD-03D Started");
  Serial.println("Using hardware Serial2");
  Serial.println("Waiting for radar data...");
  
  // Inizializza target
  target.detected = false;
}

void loop() {
  if(updateRadar()) {
    Serial.println("=== TARGET DETECTED ===");
    Serial.print("X (mm): "); Serial.println(target.x);
    Serial.print("Y (mm): "); Serial.println(target.y);
    Serial.print("Distance (mm): "); Serial.println(target.distance);
    Serial.print("Angle (degrees): "); Serial.println(target.angle);
    Serial.print("Speed (cm/s): "); Serial.println(target.speed);
    Serial.print("Detected: "); Serial.println(target.detected ? "YES" : "NO");
    Serial.println("========================");
    delay(100);
  } else {
    Serial.print(".");
    delay(100);
  }
}

bool updateRadar() {
  static uint8_t buffer[30]; 
  static size_t index = 0;
  static enum {WAIT_AA, WAIT_FF, WAIT_03, WAIT_00, RECEIVE_FRAME} state = WAIT_AA;

  bool data_updated = false;

  while (Serial2.available()) {
    byte byteIn = Serial2.read();

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

bool parseData(const uint8_t *buf, size_t len) {
  if(len != 24) return false;

  // Parse primi 8 bytes per primo target
  int16_t raw_x = buf[0] | (buf[1] << 8);
  int16_t raw_y = buf[2] | (buf[3] << 8);
  int16_t raw_speed = buf[4] | (buf[5] << 8);
  uint16_t raw_pixel_dist = buf[6] | (buf[7] << 8);

  target.detected = !(raw_x == 0 && raw_y == 0 && raw_speed == 0 && raw_pixel_dist == 0);

  // Parse valori con segno
  target.x = ((raw_x & 0x8000) ? 1 : -1) * (raw_x & 0x7FFF);
  target.y = ((raw_y & 0x8000) ? 1 : -1) * (raw_y & 0x7FFF);
  target.speed = ((raw_speed & 0x8000) ? 1 : -1) * (raw_speed & 0x7FFF);

  if (target.detected) {
    target.distance = sqrt(target.x * target.x + target.y * target.y);
    
    // Calcolo angolo (radianti -> gradi)
    float angleRad = atan2(target.y, target.x) - (PI / 2);
    float angleDeg = angleRad * (180.0 / PI);
    target.angle = -angleDeg;
  } else {
    target.distance = 0.0;
    target.angle = 0.0;
  }
  
  return true;
}
