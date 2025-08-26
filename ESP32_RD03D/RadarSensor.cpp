#include "RadarSensor.h"

#ifdef ARDUINO_ARCH_ESP32

RadarSensor::RadarSensor(uint8_t rxPin, uint8_t txPin)
	: _rxPin(rxPin), _txPin(txPin), targetCount(0)
{
	for (uint8_t i = 0; i < 3; i++) {
		targets[i] = {0,0,0,0,0,false};
		lastTargets[i] = {0,0,0,0,0,false};
		missingFrames[i] = 0;
	}
}

void RadarSensor::begin(unsigned long baud) {
	Serial2.begin(baud, SERIAL_8N1, _rxPin, _txPin);
}

// Parser state-machine for UART data
bool RadarSensor::update() {
	static uint8_t buffer[64];
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
				// accumulate until trailer 0x55 0xCC is seen or buffer is full
				if (index < sizeof(buffer)) {
					buffer[index++] = byteIn;
				}
				if (index >= 2 && buffer[index-2] == 0x55 && buffer[index-1] == 0xCC) {
					size_t payloadLen = index - 2; // exclude trailer
					data_updated = parseData(buffer, payloadLen);
					state = WAIT_AA;
					index = 0;
				}
				break;
		}
	}
	return data_updated;
}

#else

RadarSensor::RadarSensor(uint8_t rxPin, uint8_t txPin)
	: radarSerial(rxPin, txPin), targetCount(0)
{
	for (uint8_t i = 0; i < 3; i++) targets[i] = {0,0,0,0,0,false};
}

void RadarSensor::begin(unsigned long baud) {
	radarSerial.begin(baud);
}

// Parser state-machine for UART data
bool RadarSensor::update() {
	static uint8_t buffer[64]; 
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
				if (index < sizeof(buffer)) {
					buffer[index++] = byteIn;
				}
				if (index >= 2 && buffer[index-2] == 0x55 && buffer[index-1] == 0xCC) {
					size_t payloadLen = index - 2;
					data_updated = parseData(buffer, payloadLen);
					state = WAIT_AA;
					index = 0;
				}
				break;
		}
	}
	return data_updated;
}

#endif

static inline int16_t parseSigned(uint16_t raw) {
	return ((raw & 0x8000) ? 1 : -1) * (raw & 0x7FFF);
}

bool RadarSensor::parseData(const uint8_t *buf, size_t len) {
	// Accept variable-length payloads as long as they are a multiple of 8 bytes (one block per target)
	if (len < 8 || (len % 8) != 0)
		return false;

	// Reset targets
	for (uint8_t i = 0; i < 3; i++) targets[i] = {0,0,0,0,0,false};
	targetCount = 0;

	// Debug dump: raw 24-byte payload
	Serial.print("RAW24:");
	for (uint8_t i = 0; i < 24; i++) {
#ifdef ARDUINO_ARCH_ESP32
		Serial.printf(" %02X", buf[i]);
#else
		if (buf[i] < 16) Serial.print(" 0");
		else Serial.print(" ");
		Serial.print(buf[i], HEX);
#endif
	}
	Serial.println();

	// Each target block is 8 bytes: X(2), Y(2), Speed(2), Distance(2)
	uint8_t slots = len / 8;
	if (slots > 3) slots = 3; // cap to storage size
	for (uint8_t t = 0; t < slots; t++) {
		uint8_t base = t * 8;
		int16_t raw_x = buf[base + 0] | (buf[base + 1] << 8);
		int16_t raw_y = buf[base + 2] | (buf[base + 3] << 8);
		int16_t raw_speed = buf[base + 4] | (buf[base + 5] << 8);
		uint16_t raw_pixel_dist = buf[base + 6] | (buf[base + 7] << 8);

		// Debug dump: per-block raw values
		Serial.print("T"); Serial.print(t);
		Serial.print(" RAW x="); Serial.print(raw_x);
		Serial.print(" y="); Serial.print(raw_y);
		Serial.print(" v="); Serial.print(raw_speed);
		Serial.print(" dpx="); Serial.println(raw_pixel_dist);

		// Apply zero-threshold filter: ignore slots with near-zero x/y
		int16_t sx = parseSigned(raw_x);
		int16_t sy = parseSigned(raw_y);
		int16_t sv = parseSigned(raw_speed);
		// y cannot be 0: reject slots where sy == 0
		bool detected = (sy != 0) && ((abs(sx) > RD03_ZERO_THRESH_MM) || (abs(sy) > RD03_ZERO_THRESH_MM));
		if (!detected) continue;

		RadarTarget rt;
		rt.detected = true;
		rt.x = sx;
		rt.y = sy;
		rt.speed = sv;
		// distance from pixels is not used; recompute from x,y for consistency
		rt.distance = sqrt(rt.x * rt.x + rt.y * rt.y);

		float angleRad = atan2(rt.y, rt.x);
		float angleDeg = angleRad * (180.0 / PI);
		rt.angle = angleDeg;

		// Stabilize with hold: if this slot index existed previously but now is missing briefly, reuse last target
		targets[targetCount] = rt;
		lastTargets[targetCount] = rt;
		missingFrames[targetCount] = 0;
		targetCount++;
		if (targetCount >= 3) break;
	}

	// Apply hold for missing targets: if fewer targets detected, keep last ones for up to RD03_HOLD_FRAMES frames
	if (targetCount < 3) {
		for (uint8_t i = targetCount; i < 3; i++) {
			if (lastTargets[i].detected && lastTargets[i].y != 0 && missingFrames[i] < RD03_HOLD_FRAMES) {
				// keep previously known target briefly
				targets[targetCount++] = lastTargets[i];
				missingFrames[i]++;
			}
		}
	}

	// If a slot was re-detected at earlier indices, increment missing counter for the rest
	for (uint8_t i = targetCount; i < 3; i++) {
		if (!lastTargets[i].detected) missingFrames[i] = 0;
	}

	return targetCount > 0;
}

RadarTarget RadarSensor::getTarget() {
	return targetCount > 0 ? targets[0] : RadarTarget{0,0,0,0,0,false};
}

uint8_t RadarSensor::getTargetCount() const {
	return targetCount;
}

RadarTarget RadarSensor::getTarget(uint8_t index) const {
	if (index >= targetCount) return RadarTarget{0,0,0,0,0,false};
	return targets[index];
}