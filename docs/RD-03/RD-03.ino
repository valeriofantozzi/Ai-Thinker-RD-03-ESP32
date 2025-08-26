// ESP32 implementation for RD-03D Radar Module with FIFO data averaging
// Takes measurements every 100ms and calculates averages every 1 second

// ESP32 hardware serial configuration
#define RADAR_RX_PIN 16  // Connect to TX pin of radar
#define RADAR_TX_PIN 17  // Connect to RX pin of radar

// Communication parameters
#define MONITOR_BAUD 115200
#define RADAR_BAUD 256000  // Default baud rate of RD-03D

// Protocol constants
#define PACKET_HEADER_1 0xAA
#define PACKET_HEADER_2 0xFF
#define PACKET_FOOTER_1 0x55
#define PACKET_FOOTER_2 0xCC
#define BUFFER_SIZE 128

// Timing parameters
#define MEASUREMENT_INTERVAL_MS 200  // Take measurements every 100ms
#define AVERAGE_INTERVAL_MS 1000     // Calculate average every 1 second
#define FIFO_SIZE 10                 // 10 entries for 1 second (1000ms / 100ms)

// Buffer for incoming radar data
uint8_t buffer[BUFFER_SIZE];

// Structure to hold a measurement
struct RadarMeasurement {
  bool isValid;               // Whether this measurement contains valid data
  bool targetDetected;        // Target detected status
  uint8_t xPos;               // X position
  uint8_t yPos;               // Y position
  float distance;             // Calculated distance
  int signalStrength;         // Signal strength
};

// FIFO implementation using circular buffer
RadarMeasurement measurements[FIFO_SIZE];
int fifoHead = 0;             // Index for adding new measurements
int fifoCount = 0;            // Number of measurements in the FIFO


// State tracking
unsigned long lastMeasurementTime = 0;
unsigned long lastAverageTime = 0;
RadarMeasurement currentMeasurement = {false};

void setup() {
  // Initialize hardware serial for debug monitoring
  Serial.begin(MONITOR_BAUD);
  Serial.println("RD-03D Radar Module with Data Averaging (ESP32)");
  Serial.println("---------------------------------------------");
  
  // Initialize second hardware serial port for radar communication
  Serial2.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
  
  Serial.println("Waiting for radar data...");
  
  // Initialize the FIFO buffer
  for (int i = 0; i < FIFO_SIZE; i++) {
    measurements[i].isValid = false;
  }
}

void loop() {
  unsigned long currentTime = millis();
  
  // Process data from radar when available
  if (Serial2.available()) {
    int bytesRead = 0;
    
    // Read available data into buffer
    while (Serial2.available() && bytesRead < BUFFER_SIZE) {
      buffer[bytesRead] = Serial2.read();
      bytesRead++;
    }
    
    // Process received data
    if (bytesRead > 0) {
      // Process packets to extract latest measurement
      processRadarPackets(buffer, bytesRead);
    }
  }
  
  // Take measurements at regular intervals
  if (currentTime - lastMeasurementTime >= MEASUREMENT_INTERVAL_MS) {
    lastMeasurementTime = currentTime;
    
    // Add current measurement to FIFO if valid
    if (currentMeasurement.isValid) {
      addMeasurementToFifo(currentMeasurement);
      
      // Optional: Print raw measurement for debugging
      // printMeasurement(currentMeasurement);
      
      // Reset current measurement after adding to FIFO
      currentMeasurement.isValid = false;
    }
  }
  
  // Calculate and log averages at regular intervals
  if (currentTime - lastAverageTime >= AVERAGE_INTERVAL_MS && fifoCount > 0) {
    lastAverageTime = currentTime;
    calculateAndLogAverage();
  }
  
  // Forward commands from Serial Monitor to radar (for configuration)
  if (Serial.available()) {
    Serial2.write(Serial.read());
  }
}

void processRadarPackets(uint8_t* data, int length) {
  // Search for valid packets in the buffer
  for (int i = 0; i < length - 4; i++) {
    // Check for packet header
    if (data[i] == PACKET_HEADER_1 && data[i+1] == PACKET_HEADER_2) {
      // Look for a complete packet (standard length is ~28-30 bytes)
      if (i + 28 <= length && data[i+28] == PACKET_FOOTER_1 && data[i+29] == PACKET_FOOTER_2) {
        // Extract and decode the packet
        decodeRadarPacket(&data[i]);
        
        // Skip to the end of this packet
        i += 29;
      }
    }
  }
}

void decodeRadarPacket(uint8_t* packet) {
  // Command type is in byte 2-3
  uint16_t command = packet[2] | (packet[3] << 8);
  
  // Process based on command type
  if (command == 0x0003) {  // Target detection data
    // Extract target position data (bytes 4-7 appear to contain coordinates)
    uint8_t xPos = packet[4];
    uint8_t yPos = packet[6];
    
    // Extract target information
    uint16_t targetInfo = packet[10] | (packet[11] << 8);
    bool detected = (targetInfo != 0);
    
    // Update current measurement
    currentMeasurement.isValid = true;
    currentMeasurement.targetDetected = detected;
    currentMeasurement.xPos = xPos;
    currentMeasurement.yPos = yPos;
    currentMeasurement.distance = calculateDistance(xPos, yPos);
    currentMeasurement.signalStrength = packet[7];
  }
}

// Add a measurement to the FIFO queue
void addMeasurementToFifo(RadarMeasurement measurement) {
  // Store the measurement at the current head position
  measurements[fifoHead] = measurement;
  
  // Update head position (wrap around if needed)
  fifoHead = (fifoHead + 1) % FIFO_SIZE;
  
  // Update count (max out at FIFO_SIZE)
  if (fifoCount < FIFO_SIZE) {
    fifoCount++;
  }
}

// Calculate and log the average of all measurements in the FIFO
void calculateAndLogAverage() {
  // Variables for calculating averages
  int validCount = 0;
  int detectedCount = 0;
  float avgXPos = 0;
  float avgYPos = 0;
  float avgDistance = 0;
  float avgSignalStrength = 0;
  
  // Sum all valid measurements
  for (int i = 0; i < FIFO_SIZE; i++) {
    if (measurements[i].isValid) {
      validCount++;
      
      if (measurements[i].targetDetected) {
        detectedCount++;
        avgXPos += measurements[i].xPos;
        avgYPos += measurements[i].yPos;
        avgDistance += measurements[i].distance;
        avgSignalStrength += measurements[i].signalStrength;
      }
    }
  }
  
  // Calculate detection probability (percentage of measurements with detection)
  float detectionProbability = (validCount > 0) ? ((float)detectedCount / validCount) * 100.0 : 0.0;
  
  // Calculate averages if we have any detections
  if (detectedCount > 0) {
    avgXPos /= detectedCount;
    avgYPos /= detectedCount;
    avgDistance /= detectedCount;
    avgSignalStrength /= detectedCount;
  }
  
  // Log the results
  Serial.println("\n=== AVERAGED RADAR DATA (1 second) ===");
  Serial.print("Measurements: ");
  Serial.print(validCount);
  Serial.print(", Detection probability: ");
  Serial.print(detectionProbability, 1);
  Serial.println("%");
  
  if (detectedCount > 0) {
    Serial.print("Average position: X=");
    Serial.print(avgXPos, 1);
    Serial.print(", Y=");
    Serial.println(avgYPos, 1);
    Serial.print("Average distance: ");
    Serial.print(avgDistance, 2);
    Serial.println(" m");
    Serial.print("Average signal strength: ");
    Serial.println(avgSignalStrength, 1);
  } else {
    Serial.println("No targets detected in this interval.");
  }
  Serial.println("=======================================\n");
}

// Simple distance calculation (requires calibration for accuracy)
float calculateDistance(uint8_t x, uint8_t y) {
  // This is a simplified calculation that needs calibration
  // The actual formula may depend on the radar module's specifications
  return (x + y) / 100.0;  // Convert to meters
}

// Debug function to print a single measurement
void printMeasurement(RadarMeasurement measurement) {
  Serial.println("--- Single Measurement ---");
  Serial.print("Target detected: ");
  Serial.println(measurement.targetDetected ? "YES" : "NO");
  
  if (measurement.targetDetected) {
    Serial.print("Position: X=");
    Serial.print(measurement.xPos);
    Serial.print(", Y=");
    Serial.println(measurement.yPos);
    Serial.print("Distance: ");
    Serial.print(measurement.distance);
    Serial.println(" m");
    Serial.print("Signal strength: ");
    Serial.println(measurement.signalStrength);
  }
  Serial.println("------------------------");
}