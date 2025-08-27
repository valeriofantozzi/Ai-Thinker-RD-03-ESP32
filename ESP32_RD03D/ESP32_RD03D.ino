#include <ESP_RadarSensor.h>

RadarSensor radar(16,17); // RX, TX pins (ESP32: RX=16, TX=17)
bool DEBUG_RAW_TARGETS = false;  // Can be toggled via serial command
bool MULTI_TARGET = true;       // Can be toggled via serial command

// ===== ZONE CONFIGURATION =====
// Grid 4x4 (2m per tile), with A4 and D4 removed (14 tiles total)
// Radar at origin (0,0), Y+ forward, X+ right
// A=left, D=right, 1=near, 4=far
const uint8_t NUM_ZONES = 14;
const float TILE_SIZE = 2000.0; // mm

// Zone definitions: [zone_id][0=x_min, 1=x_max, 2=y_min, 3=y_max]
const float ZONE_BOUNDS[NUM_ZONES][4] = {
  // Row 1 (0-2m)
  {-4000, -2000, 0, 2000},  // A1
  {-2000,     0, 0, 2000},  // B1  
  {    0,  2000, 0, 2000},  // C1
  { 2000,  4000, 0, 2000},  // D1
  // Row 2 (2-4m)
  {-4000, -2000, 2000, 4000},  // A2
  {-2000,     0, 2000, 4000},  // B2
  {    0,  2000, 2000, 4000},  // C2
  { 2000,  4000, 2000, 4000},  // D2
  // Row 3 (4-6m)
  {-4000, -2000, 4000, 6000},  // A3
  {-2000,     0, 4000, 6000},  // B3
  {    0,  2000, 4000, 6000},  // C3
  { 2000,  4000, 4000, 6000},  // D3
  // Row 4 (6-8m) - only B4 and C4
  {-2000,     0, 6000, 8000},  // B4
  {    0,  2000, 6000, 8000}   // C4
};

const char* ZONE_NAMES[NUM_ZONES] = {
  "A1", "B1", "C1", "D1",
  "A2", "B2", "C2", "D2",
  "A3", "B3", "C3", "D3",
  "B4", "C4"
};

// ===== TRACKING STRUCTURES =====
const uint8_t MAX_TRACKS = 3;
const float EMA_ALPHA_MIN = 0.2;  // For slow movements
const float EMA_ALPHA_MAX = 0.8;  // For fast movements  
const float MOVEMENT_THRESHOLD = 300.0; // mm - threshold for adaptive EMA
const float ASSOCIATION_GATE = 800.0; // mm
const uint8_t LOST_FRAMES_THRESH = 10;
const uint8_t CONFIRM_FRAMES = 0; // 0 = immediate zone change
const float DEADBAND_SIZE = 100.0; // mm from tile border
const unsigned long OCCUPANCY_OFF_DELAY = 300; // ms

struct Track {
  float x, y;           // smoothed position
  float raw_x, raw_y;   // last raw position
  float speed;
  uint8_t id;
  uint8_t frames_lost;
  bool active;
  uint8_t current_zone; // 0xFF = no zone
  uint8_t zone_confirm_count;
  uint8_t proposed_zone;
};

Track tracks[MAX_TRACKS];
uint8_t next_track_id = 1;

// Zone occupancy state
bool zone_occupied[NUM_ZONES];
unsigned long zone_last_seen[NUM_ZONES];

void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_ARCH_ESP32)
  Serial.println("Build: ESP32 backend (ARDUINO_ARCH_ESP32 defined)");
#else
  Serial.println("Build: non-ESP32 backend (SoftwareSerial path)");
#endif
  radar.begin(256000);
  
  // Initialize radar mode
  setRadarMode(MULTI_TARGET);
  
  // Initialize tracking
  for (uint8_t i = 0; i < MAX_TRACKS; i++) {
    tracks[i].active = false;
    tracks[i].current_zone = 0xFF;
  }
  
  // Initialize zones
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    zone_occupied[i] = false;
    zone_last_seen[i] = 0;
  }
  
  Serial.println("Radar Zone Tracking Started");
  Serial.println("14 zones configured (4x4 grid minus corners)");
  Serial.println("Type 'HELP' for available commands");
}

// ===== HELPER FUNCTIONS =====

// Find which zone a point belongs to
uint8_t getZoneForPoint(float x, float y) {
  // Check range first
  float dist = sqrt(x*x + y*y);
  if (dist < 300 || dist > 8000) return 0xFF; // out of range
  
  // Debug output to help diagnose zone assignment
  if (DEBUG_RAW_TARGETS) {
    Serial.print("Zone check for point (");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    Serial.print(") dist=");
    Serial.println(dist);
  }
  
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    if (x >= ZONE_BOUNDS[i][0] && x <= ZONE_BOUNDS[i][1] &&
        y >= ZONE_BOUNDS[i][2] && y <= ZONE_BOUNDS[i][3]) {
      
      if (DEBUG_RAW_TARGETS) {
        Serial.print("Point in zone: ");
        Serial.println(ZONE_NAMES[i]);
      }
      return i;
    }
  }
  
  // If no exact match, find closest zone (optional)
  // This prevents "no zone" situations when slightly outside boundaries
  uint8_t closest_zone = 0xFF;
  float min_distance = 1000000;
  
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    // Calculate center of zone
    float zone_center_x = (ZONE_BOUNDS[i][0] + ZONE_BOUNDS[i][1]) / 2;
    float zone_center_y = (ZONE_BOUNDS[i][2] + ZONE_BOUNDS[i][3]) / 2;
    
    // Calculate distance to zone center
    float dx = x - zone_center_x;
    float dy = y - zone_center_y;
    float distance = sqrt(dx*dx + dy*dy);
    
    // Update closest zone if this is closer
    if (distance < min_distance) {
      min_distance = distance;
      closest_zone = i;
    }
  }
  
  if (DEBUG_RAW_TARGETS && closest_zone != 0xFF) {
    Serial.print("No exact zone match. Closest zone: ");
    Serial.println(ZONE_NAMES[closest_zone]);
  }
  
  return closest_zone; // Return closest zone or 0xFF if none found
}

// Check if point is in deadband near zone borders
bool isInDeadband(float x, float y, uint8_t zone_id) {
  if (zone_id >= NUM_ZONES) return false;
  
  float dist_to_left = x - ZONE_BOUNDS[zone_id][0];
  float dist_to_right = ZONE_BOUNDS[zone_id][1] - x;
  float dist_to_bottom = y - ZONE_BOUNDS[zone_id][2];
  float dist_to_top = ZONE_BOUNDS[zone_id][3] - y;
  
  return (dist_to_left < DEADBAND_SIZE || dist_to_right < DEADBAND_SIZE ||
          dist_to_bottom < DEADBAND_SIZE || dist_to_top < DEADBAND_SIZE);
}

// Find closest track to a detection
uint8_t findClosestTrack(float x, float y, float& min_dist) {
  uint8_t closest = 0xFF;
  min_dist = ASSOCIATION_GATE;
  
  for (uint8_t i = 0; i < MAX_TRACKS; i++) {
    if (!tracks[i].active) continue;
    
    float dx = x - tracks[i].raw_x;
    float dy = y - tracks[i].raw_y;
    float dist = sqrt(dx*dx + dy*dy);
    
    if (dist < min_dist) {
      min_dist = dist;
      closest = i;
    }
  }
  return closest;
}

// Update zone occupancy based on tracks
void updateZoneOccupancy() {
  unsigned long now = millis();
  
  // First, mark zones with active tracks
  bool zone_has_track[NUM_ZONES] = {false};
  
  for (uint8_t i = 0; i < MAX_TRACKS; i++) {
    if (tracks[i].active && tracks[i].current_zone < NUM_ZONES) {
      zone_has_track[tracks[i].current_zone] = true;
      zone_last_seen[tracks[i].current_zone] = now;
    }
  }
  
  // Update occupancy states with hysteresis
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    if (zone_has_track[i]) {
      if (!zone_occupied[i]) {
        zone_occupied[i] = true;
        Serial.print("Zone ON: ");
        Serial.println(ZONE_NAMES[i]);
      }
    } else if (zone_occupied[i]) {
      // Check if enough time has passed to turn off
      if (now - zone_last_seen[i] > OCCUPANCY_OFF_DELAY) {
        zone_occupied[i] = false;
        Serial.print("Zone OFF: ");
        Serial.println(ZONE_NAMES[i]);
      }
    }
  }
}

// Print current zone status
void printZoneStatus() {
  Serial.println("\n=== ZONE STATUS ===");
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    if (zone_occupied[i]) {
      Serial.print("[X] ");
      Serial.println(ZONE_NAMES[i]);
    }
  }
  
  Serial.print("Active tracks: ");
  uint8_t active_count = 0;
  for (uint8_t i = 0; i < MAX_TRACKS; i++) {
    if (tracks[i].active) active_count++;
  }
  Serial.println(active_count);
}

// Send command to RD-03D to set mode
void setRadarMode(bool multi) {
#if defined(ARDUINO_ARCH_ESP32)
  // Commands per RD-03D quickstart
  static const uint8_t RD03D_CMD_SINGLE[] = {0xFD,0xFC,0xFB,0xFA,0x02,0x00,0x80,0x00,0x04,0x03,0x02,0x01};
  static const uint8_t RD03D_CMD_MULTI[]  = {0xFD,0xFC,0xFB,0xFA,0x02,0x00,0x90,0x00,0x04,0x03,0x02,0x01};
  
  // Some firmwares require a mode reset before switching
  Serial2.write(RD03D_CMD_SINGLE, sizeof(RD03D_CMD_SINGLE));
  delay(100);
  
  if (multi) {
    Serial2.write(RD03D_CMD_MULTI, sizeof(RD03D_CMD_MULTI));
    Serial.println("Multi-target mode enabled");
  } else {
    Serial2.write(RD03D_CMD_SINGLE, sizeof(RD03D_CMD_SINGLE));
    Serial.println("Single-target mode enabled");
  }
  Serial2.flush();
#else
  Serial.println("Mode switching only supported on ESP32");
#endif
}

// Process serial commands
void processSerialCommand() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "DEBUG") {
      DEBUG_RAW_TARGETS = !DEBUG_RAW_TARGETS;
      Serial.print("Debug raw targets: ");
      Serial.println(DEBUG_RAW_TARGETS ? "ON" : "OFF");
    }
    else if (command == "MULTI") {
      MULTI_TARGET = !MULTI_TARGET;
      Serial.print("Multi-target mode: ");
      Serial.println(MULTI_TARGET ? "ON" : "OFF");
      setRadarMode(MULTI_TARGET);
    }
    else if (command == "ZONES") {
      Serial.println("\n=== ZONE DEFINITIONS ===");
      for (uint8_t i = 0; i < NUM_ZONES; i++) {
        Serial.print(ZONE_NAMES[i]);
        Serial.print(": X=[");
        Serial.print(ZONE_BOUNDS[i][0]);
        Serial.print(",");
        Serial.print(ZONE_BOUNDS[i][1]);
        Serial.print("] Y=[");
        Serial.print(ZONE_BOUNDS[i][2]);
        Serial.print(",");
        Serial.print(ZONE_BOUNDS[i][3]);
        Serial.println("]");
      }
      Serial.println("\n=== CURRENT TRACKS ===");
      for (uint8_t i = 0; i < MAX_TRACKS; i++) {
        if (tracks[i].active) {
          Serial.print("Track ");
          Serial.print(tracks[i].id);
          Serial.print(": (");
          Serial.print(tracks[i].x);
          Serial.print(", ");
          Serial.print(tracks[i].y);
          Serial.print(") Zone: ");
          if (tracks[i].current_zone < NUM_ZONES) {
            Serial.println(ZONE_NAMES[tracks[i].current_zone]);
          } else {
            Serial.println("NONE");
          }
        }
      }
    }
    else if (command == "HELP") {
      Serial.println("\n=== COMMANDS ===");
      Serial.println("DEBUG - Toggle raw target debug output");
      Serial.println("MULTI - Toggle multi-target mode");
      Serial.println("ZONES - Show zone definitions and current tracks");
      Serial.println("HELP  - Show this help");
    }
  }
}

void loop() {
  // Check for serial commands
  processSerialCommand();
  
  if(radar.update()) {
    uint8_t n = radar.getTargetCount();
    
    if (DEBUG_RAW_TARGETS) {
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
    }
    
    // Mark all tracks as potentially lost
    for (uint8_t i = 0; i < MAX_TRACKS; i++) {
      if (tracks[i].active) {
        tracks[i].frames_lost++;
      }
    }
    
    // Process each detection
    for (uint8_t d = 0; d < n; d++) {
      RadarTarget t = radar.getTarget(d);
      if (!t.detected) continue;
      
      // Find closest existing track
      float min_dist;
      uint8_t track_idx = findClosestTrack(t.x, t.y, min_dist);
      
      if (track_idx != 0xFF) {
        // Update existing track
        Track& track = tracks[track_idx];
        
        // Store raw position
        track.raw_x = t.x;
        track.raw_y = t.y;
        track.speed = t.speed;
        
        // Apply adaptive EMA smoothing
        float dx = t.x - track.x;
        float dy = t.y - track.y;
        float movement_distance = sqrt(dx*dx + dy*dy);
        
        // Adaptive alpha based on movement speed
        float alpha;
        if (movement_distance > MOVEMENT_THRESHOLD) {
          // Fast movement - high alpha (more responsive)
          alpha = EMA_ALPHA_MAX;
        } else {
          // Slow movement - interpolate between min and max
          float movement_ratio = movement_distance / MOVEMENT_THRESHOLD;
          alpha = EMA_ALPHA_MIN + movement_ratio * (EMA_ALPHA_MAX - EMA_ALPHA_MIN);
        }
        
        track.x = alpha * t.x + (1 - alpha) * track.x;
        track.y = alpha * t.y + (1 - alpha) * track.y;
        
        if (DEBUG_RAW_TARGETS) {
          Serial.print("Track ");
          Serial.print(track.id);
          Serial.print(" movement: ");
          Serial.print(movement_distance);
          Serial.print("mm, alpha: ");
          Serial.print(alpha);
          Serial.print(" | Raw:(");
          Serial.print(track.raw_x);
          Serial.print(",");
          Serial.print(track.raw_y);
          Serial.print(") Smooth:(");
          Serial.print(track.x);
          Serial.print(",");
          Serial.print(track.y);
          Serial.println(")");
        }
        
        track.frames_lost = 0;
        
        // Determine zone with deadband and confirmation
        // Use smoothed coordinates now that EMA is adaptive and responsive
        uint8_t new_zone = getZoneForPoint(track.x, track.y);
        
        if (DEBUG_RAW_TARGETS && new_zone != track.current_zone) {
          Serial.print("Track ");
          Serial.print(track.id);
          Serial.print(" zone change: ");
          if (track.current_zone < NUM_ZONES) {
            Serial.print(ZONE_NAMES[track.current_zone]);
          } else {
            Serial.print("NONE");
          }
          Serial.print(" -> ");
          if (new_zone < NUM_ZONES) {
            Serial.println(ZONE_NAMES[new_zone]);
          } else {
            Serial.println("NONE");
          }
        }
        
        if (new_zone != track.current_zone) {
          // Check if we're in deadband of current zone
          if (track.current_zone < NUM_ZONES && isInDeadband(track.x, track.y, track.current_zone)) {
            // Stay in current zone if in deadband
            new_zone = track.current_zone;
          } else {
            // Immediate zone change (CONFIRM_FRAMES = 0) or confirmation logic
            if (CONFIRM_FRAMES == 0) {
              // Immediate zone change
              track.current_zone = new_zone;
              track.zone_confirm_count = 0;
              track.proposed_zone = new_zone;
              
              if (new_zone < NUM_ZONES) {
                Serial.print("Track ");
                Serial.print(track.id);
                Serial.print(" entered zone: ");
                Serial.println(ZONE_NAMES[new_zone]);
              }
            } else {
              // Confirmation-based zone change
              if (new_zone == track.proposed_zone) {
                track.zone_confirm_count++;
                if (track.zone_confirm_count >= CONFIRM_FRAMES) {
                  track.current_zone = new_zone;
                  track.zone_confirm_count = 0;
                  
                  if (new_zone < NUM_ZONES) {
                    Serial.print("Track ");
                    Serial.print(track.id);
                    Serial.print(" entered zone: ");
                    Serial.println(ZONE_NAMES[new_zone]);
                  }
                }
              } else {
                track.proposed_zone = new_zone;
                track.zone_confirm_count = 1;
              }
            }
          }
        }
      } else {
        // Create new track - find free slot
        for (uint8_t i = 0; i < MAX_TRACKS; i++) {
          if (!tracks[i].active) {
            tracks[i].active = true;
            tracks[i].id = next_track_id++;
            tracks[i].x = tracks[i].raw_x = t.x;
            tracks[i].y = tracks[i].raw_y = t.y;
            tracks[i].speed = t.speed;
            tracks[i].frames_lost = 0;
            tracks[i].current_zone = getZoneForPoint(t.x, t.y);
            tracks[i].zone_confirm_count = 0;
            tracks[i].proposed_zone = tracks[i].current_zone;
            
            Serial.print("New track ");
            Serial.print(tracks[i].id);
            Serial.print(" at (");
            Serial.print(t.x);
            Serial.print(", ");
            Serial.print(t.y);
            Serial.println(")");
            break;
          }
        }
      }
    }
    
    // Remove lost tracks
    for (uint8_t i = 0; i < MAX_TRACKS; i++) {
      if (tracks[i].active && tracks[i].frames_lost > LOST_FRAMES_THRESH) {
        Serial.print("Lost track ");
        Serial.println(tracks[i].id);
        tracks[i].active = false;
      }
    }
    
    // Update zone occupancy
    updateZoneOccupancy();
    
    // Print status periodically
    static unsigned long last_status = 0;
    if (millis() - last_status > 2000) {
      printZoneStatus();
      last_status = millis();
    }
    
    delay(50); // Faster update rate for tracking
  } else {
    // No radar data - increment lost frames for all tracks
    for (uint8_t i = 0; i < MAX_TRACKS; i++) {
      if (tracks[i].active) {
        tracks[i].frames_lost++;
        if (tracks[i].frames_lost > LOST_FRAMES_THRESH) {
          tracks[i].active = false;
        }
      }
    }
    
    updateZoneOccupancy();
    delay(100);
  }
}
