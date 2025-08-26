#ifndef RADARSENSOR_H
#define RADARSENSOR_H

#include <Arduino.h>

#ifdef ARDUINO_ARCH_ESP32
	#include <HardwareSerial.h>
#else
	#include <SoftwareSerial.h>
#endif

typedef struct RadarTarget {
	float distance;  // mm
	float angle;     // radians
	float speed;     // cm/s
	int16_t x;       // mm
	int16_t y;       // mm
	bool detected;
} RadarTarget;

class RadarSensor {
	public:
		RadarSensor(uint8_t rxPin, uint8_t txPin);
		void begin(unsigned long baud = 256000);
		bool update();
		RadarTarget getTarget(); // first target for backward compatibility
		// Multi-target API
		uint8_t getTargetCount() const;
		RadarTarget getTarget(uint8_t index) const;
	private:
#define RD03_ZERO_THRESH_MM 10
#define RD03_HOLD_FRAMES 5
#ifdef ARDUINO_ARCH_ESP32
		uint8_t _rxPin;
		uint8_t _txPin;
#else
		SoftwareSerial radarSerial;
#endif
		// Store up to 3 targets parsed from 24-byte payload (3 x 8 bytes)
		RadarTarget targets[3];
		uint8_t targetCount;
		// Hold previous valid targets to stabilize across brief dropouts
		RadarTarget lastTargets[3];
		uint8_t missingFrames[3];
		bool parseData(const uint8_t *buffer, size_t len);
};

#endif