#define MULTI_TARGET 1
#include <ESP_RadarSensor.h>

RadarSensor radar(16,17); // RX, TX pins (ESP32: RX=16, TX=17)

void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_ARCH_ESP32)
  Serial.println("Build: ESP32 backend (ARDUINO_ARCH_ESP32 defined)");
#else
  Serial.println("Build: non-ESP32 backend (SoftwareSerial path)");
#endif
  radar.begin(256000);
  
#if defined(ARDUINO_ARCH_ESP32)
  // Force Multi-Target mode per RD-03D quickstart (send command over UART)
  static const uint8_t RD03D_CMD_SINGLE[] = {0xFD,0xFC,0xFB,0xFA,0x02,0x00,0x80,0x00,0x04,0x03,0x02,0x01};
  static const uint8_t RD03D_CMD_MULTI[]  = {0xFD,0xFC,0xFB,0xFA,0x02,0x00,0x90,0x00,0x04,0x03,0x02,0x01};
  delay(50);
  // Some firmwares require a mode reset before switching
  Serial2.write(RD03D_CMD_SINGLE, sizeof(RD03D_CMD_SINGLE));
  delay(100);
  Serial2.write(RD03D_CMD_MULTI, sizeof(RD03D_CMD_MULTI));
  Serial2.flush();
  Serial.println("Multi-target command sent");
#endif
  Serial.println("Radar Sensor Started");
}

void loop() {
  if(radar.update()) {
#if MULTI_TARGET
    uint8_t n = radar.getTargetCount();
    Serial.print("Targets: "); Serial.println(n);
    for (uint8_t i = 0; i < n; i++) {
      RadarTarget t = radar.getTarget(i);
      Serial.print("["); Serial.print(i); Serial.println("]");
      Serial.print("  X (mm): "); Serial.println(t.x);
      Serial.print("  Y (mm): "); Serial.println(t.y);
      Serial.print("  Distance (mm): "); Serial.println(t.distance);
      Serial.print("  Angle (degrees): "); Serial.println(t.angle);
      Serial.print("  Speed (cm/s): "); Serial.println(t.speed);
    }
    Serial.println("-------------------------");
#else
    RadarTarget tgt = radar.getTarget();
    Serial.print("X (mm): "); Serial.println(tgt.x);
    Serial.print("Y (mm): "); Serial.println(tgt.y);
    Serial.print("Distance (mm): "); Serial.println(tgt.distance);
    Serial.print("Angle (degrees): "); Serial.println(tgt.angle);
    Serial.print("Speed (cm/s): "); Serial.println(tgt.speed);
    Serial.println("-------------------------");
#endif
    delay(100);
  } else {
    Serial.println("No data available");
    delay(500);
  }
}
