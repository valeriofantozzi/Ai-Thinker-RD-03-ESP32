# ESP32 RD03D HomeKit Integration

Questo progetto integra il radar RD03D con Apple HomeKit utilizzando la libreria HomeSpan.

## Installazione HomeSpan

### 1. Installazione tramite Arduino IDE
1. Apri Arduino IDE
2. Vai su **Sketch** → **Include Library** → **Manage Libraries**
3. Cerca "HomeSpan" di Gregg Berman
4. Clicca "Install"

### 2. Installazione tramite PlatformIO
Aggiungi nel file `platformio.ini`:
```ini
lib_deps = 
    homespan/HomeSpan@^2.1.4
```

### 3. Installazione manuale
1. Clona la repository HomeSpan:
   ```bash
   git clone https://github.com/HomeSpan/HomeSpan.git
   ```
2. Copia la cartella nella directory libraries di Arduino

## Configurazione ESP32

### Requisiti Hardware
- ESP32 (S2, S3, C3, C6 supportati)
- Radar RD03D collegato ai pin:
  - RX: Pin 16
  - TX: Pin 17

### Partition Scheme
Seleziona "Minimal SPIFFS (1.9MB APP with OTA)" nel menu Arduino IDE per garantire spazio sufficiente.

## Caratteristiche HomeKit

Il progetto espone un bridge HomeKit chiamato "Radar" con due accessori:

### 1. Presence Sensor (OccupancySensor)
- **Nome**: "Radar Presence"
- **Tipo**: Sensore di Occupazione
- **Stato**: Rilevato/Non Rilevato
- **Logica**: Si attiva quando il radar rileva qualsiasi target attivo

### 2. Motion Sensor (MotionSensor)
- **Nome**: "Radar Motion"
- **Tipo**: Sensore di Movimento  
- **Stato**: Movimento/Nessun Movimento
- **Logica**: Si attiva quando un target ha velocità > 0.5 cm/s

## Comandi Serial

Connetti tramite Serial Monitor (115200 baud) per i seguenti comandi:

- `DEBUG` - Attiva/disattiva debug raw targets
- `MULTI` - Attiva/disattiva modalità multi-target
- `EMA` - Attiva/disattiva smoothing EMA
- `ZONES` - Mostra definizioni zone e tracks attivi
- `HOMEKIT` - Mostra stato sensori HomeKit
- `HELP` - Mostra comandi disponibili

## Pairing con HomeKit

1. Compila e carica il codice sull'ESP32
2. Apri l'app Home su iPhone/iPad
3. Tocca "+" per aggiungere accessorio
4. Scansiona il codice QR che appare nel Serial Monitor
5. O inserisci manualmente il setup code mostrato

## Struttura del Codice

### Servizi HomeSpan Personalizzati
```cpp
struct RadarPresenceSensor : Service::OccupancySensor {
  // Gestisce rilevamento presenza
}

struct RadarMotionSensor : Service::MotionSensor {
  // Gestisce rilevamento movimento
}
```

### Logica di Aggiornamento
- I sensori vengono aggiornati in `updateHomeKitSensors()`
- La presenza è basata sui track attivi del radar
- Il movimento è basato sulla velocità dei target (soglia: 0.5 cm/s)
- Gli aggiornamenti vengono inviati solo quando lo stato cambia

## Zone Radar

Il sistema mantiene un grid dinamico 4x4 (A1-D4, esclusi A4 e D4):
- Dimensione tile: 1000mm
- Range totale: 4000mm (4m)
- Tracking multi-target con EMA smoothing
- Deadband per ridurre oscillazioni

## Risoluzione Problemi

1. **Errori di compilazione**: Verifica che HomeSpan sia installato correttamente
2. **Non si connette a WiFi**: Usa il comando CLI di HomeSpan per configurare credenziali
3. **Non appare in HomeKit**: Controlla che l'hub HomeKit (Apple TV/HomePod) sia presente
4. **Falsi positivi**: Regola le soglie di velocità e deadband nel codice

## Note Tecniche

- HomeSpan richiede Arduino-ESP32 Core 3.1.0 o superiore
- La memoria flash minima richiesta è 1.9MB
- I sensori inviano notifiche evento solo quando lo stato cambia
- Il sistema supporta fino a 3 track simultanei
