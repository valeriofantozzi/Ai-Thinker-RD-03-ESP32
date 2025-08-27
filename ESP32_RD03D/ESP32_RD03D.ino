#include <ESP_RadarSensor.h>

RadarSensor radar(16,17); // RX, TX pins (ESP32: RX=16, TX=17)
bool DEBUG_RAW_TARGETS = false;  // Can be toggled via serial command
bool MULTI_TARGET = true;       // Can be toggled via serial command
bool EMA_ENABLED = true;        // Can be toggled via serial command

// ===== ZONE CONFIGURATION =====
// Dynamic grid system with configurable tile size
// Radar at origin (0,0), Y+ forward, X+ right
// A=left, D=right, 1=near, 4=far
const float TILE_SIZE = 1000.0; // mm (reduced from 2000 for better debugging)
const uint8_t GRID_WIDTH = 4;   // Number of columns (A, B, C, D)
const uint8_t GRID_HEIGHT = 4;  // Number of rows (1, 2, 3, 4)
const float GRID_RANGE = GRID_WIDTH * TILE_SIZE; // Total range (4000mm = 4m)

// Calculate number of zones (exclude A4 and D4 corners)
const uint8_t NUM_ZONES = GRID_WIDTH * GRID_HEIGHT - 2; // 16 - 2 = 14

// Dynamic zone definitions - will be initialized in setup()
float ZONE_BOUNDS[NUM_ZONES][4];  // [zone_id][0=x_min, 1=x_max, 2=y_min, 3=y_max]
String ZONE_NAMES[NUM_ZONES];     // Zone names like "A1", "B2", etc.

// ===== TRACKING STRUCTURES =====
const uint8_t MAX_TRACKS = 3;
const float EMA_ALPHA_MIN = 0.2;  // For slow movements
const float EMA_ALPHA_MAX = 0.8;  // For fast movements  
const float MOVEMENT_THRESHOLD = 300.0; // mm - threshold for adaptive EMA
const float ASSOCIATION_GATE = 800.0; // mm
const uint8_t LOST_FRAMES_THRESH = 10;
const uint8_t CONFIRM_FRAMES = 0; // 0 = immediate zone change
const float DEADBAND_SIZE = 50.0; // mm from tile border (reduced from 100)
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

// ===== ZONE INITIALIZATION FUNCTIONS =====

// Convert column index to letter (0=A, 1=B, 2=C, 3=D)
char getColumnLetter(uint8_t col) {
  return 'A' + col;
}

// Check if a zone should be excluded (A4 and D4 corners)
bool shouldExcludeZone(uint8_t col, uint8_t row) {
  return (col == 0 && row == 3) || (col == 3 && row == 3); // A4 or D4
}

// Initialize zone bounds and names dynamically
void initializeZones() {
  uint8_t zone_index = 0;
  
  Serial.println("Initializing dynamic zone system...");
  Serial.print("TILE_SIZE: "); Serial.print(TILE_SIZE); Serial.println(" mm");
  Serial.print("GRID: "); Serial.print(GRID_WIDTH); Serial.print("x"); Serial.println(GRID_HEIGHT);
  Serial.print("TOTAL_RANGE: "); Serial.print(GRID_RANGE); Serial.println(" mm");
  
  for (uint8_t row = 0; row < GRID_HEIGHT; row++) {
    for (uint8_t col = 0; col < GRID_WIDTH; col++) {
      // Skip A4 and D4 corners
      if (shouldExcludeZone(col, row)) {
        continue;
      }
      
      // Calculate zone bounds
      float x_min = (col - 2) * TILE_SIZE;  // Center grid at origin
      float x_max = x_min + TILE_SIZE;
      float y_min = row * TILE_SIZE;
      float y_max = y_min + TILE_SIZE;
      
      // Store bounds
      ZONE_BOUNDS[zone_index][0] = x_min;
      ZONE_BOUNDS[zone_index][1] = x_max;
      ZONE_BOUNDS[zone_index][2] = y_min;
      ZONE_BOUNDS[zone_index][3] = y_max;
      
      // Generate name (e.g., "A1", "B2", etc.)
      ZONE_NAMES[zone_index] = String(getColumnLetter(col)) + String(row + 1);
      
      Serial.print("Zone "); Serial.print(zone_index); Serial.print(" (");
      Serial.print(ZONE_NAMES[zone_index]); Serial.print("): X=[");
      Serial.print(x_min); Serial.print(","); Serial.print(x_max);
      Serial.print("] Y=["); Serial.print(y_min); Serial.print(",");
      Serial.print(y_max); Serial.println("]");
      
      zone_index++;
    }
  }
  
  Serial.print("Total zones created: "); Serial.println(zone_index);
  Serial.println("Zone initialization complete.\n");
}

void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_ARCH_ESP32)
  Serial.println("Build: ESP32 backend (ARDUINO_ARCH_ESP32 defined)");
#else
  Serial.println("Build: non-ESP32 backend (SoftwareSerial path)");
#endif
  radar.begin(256000);
  
  // Initialize dynamic zone system
  initializeZones();
  
  // Initialize radar mode
  setRadarMode(MULTI_TARGET);
  
  // Initialize tracking
  for (uint8_t i = 0; i < MAX_TRACKS; i++) {
    tracks[i].active = false;
    tracks[i].current_zone = 0xFF;
  }
  
  // Initialize zone occupancy
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    zone_occupied[i] = false;
    zone_last_seen[i] = 0;
  }
  
  Serial.println("Radar Zone Tracking Started");
  Serial.print(NUM_ZONES); Serial.println(" zones configured dynamically");
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
  
  float x_min = ZONE_BOUNDS[zone_id][0];
  float x_max = ZONE_BOUNDS[zone_id][1];
  float y_min = ZONE_BOUNDS[zone_id][2];
  float y_max = ZONE_BOUNDS[zone_id][3];
  
  // Signed distances to each edge (positive = inside distance to edge, negative = outside beyond edge)
  float dist_to_left = x - x_min;
  float dist_to_right = x_max - x;
  float dist_to_bottom = y - y_min;
  float dist_to_top = y_max - y;
  
  bool is_inside = (dist_to_left >= 0 && dist_to_right >= 0 && dist_to_bottom >= 0 && dist_to_top >= 0);
  bool in_deadband = false;
  
  if (is_inside) {
    // If inside the zone, we are in deadband when close to any edge
    float min_edge_dist = min(min(dist_to_left, dist_to_right), min(dist_to_bottom, dist_to_top));
    in_deadband = (min_edge_dist <= DEADBAND_SIZE);
  } else {
    // If slightly outside, allow deadband only when the violation beyond the nearest edge is small
    float outside_x = 0;
    if (dist_to_left < 0) outside_x = -dist_to_left; else if (dist_to_right < 0) outside_x = -dist_to_right;
    float outside_y = 0;
    if (dist_to_bottom < 0) outside_y = -dist_to_bottom; else if (dist_to_top < 0) outside_y = -dist_to_top;
    float outside_violation = max(outside_x, outside_y);
    in_deadband = (outside_violation > 0 && outside_violation <= DEADBAND_SIZE);
  }
  
  if (DEBUG_RAW_TARGETS && in_deadband) {
    Serial.print("Point (");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    Serial.print(") in deadband of ");
    Serial.print(ZONE_NAMES[zone_id]);
    Serial.print(is_inside ? " (inside)" : " (outside)");
    Serial.print(" - distances: L=");
    Serial.print(dist_to_left);
    Serial.print(" R=");
    Serial.print(dist_to_right);
    Serial.print(" B=");
    Serial.print(dist_to_bottom);
    Serial.print(" T=");
    Serial.println(dist_to_top);
  }
  
  return in_deadband;
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
      
      if (DEBUG_RAW_TARGETS) {
        Serial.print("Track ");
        Serial.print(tracks[i].id);
        Serial.print(" is in zone ");
        Serial.print(ZONE_NAMES[tracks[i].current_zone]);
        Serial.print(" at (");
        Serial.print(tracks[i].x);
        Serial.print(", ");
        Serial.print(tracks[i].y);
        Serial.println(")");
      }
    } else if (tracks[i].active) {
      if (DEBUG_RAW_TARGETS) {
        Serial.print("Track ");
        Serial.print(tracks[i].id);
        Serial.print(" has NO ZONE at (");
        Serial.print(tracks[i].x);
        Serial.print(", ");
        Serial.print(tracks[i].y);
        Serial.println(")");
      }
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
    else if (command == "EMA") {
      EMA_ENABLED = !EMA_ENABLED;
      Serial.print("EMA smoothing: ");
      Serial.println(EMA_ENABLED ? "ON" : "OFF");
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
      Serial.println("EMA   - Toggle EMA smoothing");
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
        
        // Apply adaptive EMA smoothing (if enabled)
        float dx = t.x - track.x;
        float dy = t.y - track.y;
        float movement_distance = sqrt(dx*dx + dy*dy);
        float alpha = 1.0;  // Default to full update (no smoothing)
        
        if (EMA_ENABLED) {
          // Adaptive alpha based on movement speed
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
        } else {
          // No EMA - use raw coordinates directly
          track.x = t.x;
          track.y = t.y;
        }
        
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
        
        if (DEBUG_RAW_TARGETS) {
          Serial.print("Track ");
          Serial.print(track.id);
          Serial.print(" current_zone: ");
          if (track.current_zone < NUM_ZONES) {
            Serial.print(ZONE_NAMES[track.current_zone]);
          } else {
            Serial.print("NONE");
          }
          Serial.print(", detected_zone: ");
          if (new_zone < NUM_ZONES) {
            Serial.println(ZONE_NAMES[new_zone]);
          } else {
            Serial.println("NONE");
          }
        }
        
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
            if (DEBUG_RAW_TARGETS) {
              Serial.print("Track ");
              Serial.print(track.id);
              Serial.print(" in deadband of ");
              Serial.print(ZONE_NAMES[track.current_zone]);
              Serial.print(", staying in zone despite being detected in ");
              if (new_zone < NUM_ZONES) {
                Serial.println(ZONE_NAMES[new_zone]);
              } else {
                Serial.println("NONE");
              }
            }
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
