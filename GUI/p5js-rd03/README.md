# RD-03D Radar Viewer (p5.js)

## Run
- Use Google Chrome (Web Serial required).
- Serve this folder via a local server (secure context):
  - Python: `python3 -m http.server 8000`
  - Then open: http://localhost:8000/GUI/p5js-rd03/
- Click "Connect", select the ESP32 serial port.

## Arduino output format (one JSON line per frame)
- MULTI_TARGET on:
```
{"t":[{"x":INT,"y":INT,"d":FLOAT,"a":FLOAT,"s":FLOAT}, ...]}
```
- MULTI_TARGET off:
```
{"t":[{"x":INT,"y":INT,"d":FLOAT,"a":FLOAT,"s":FLOAT}]}
```

## Notes
- Default baud: 256000.
- Scale control expects mm/px (default 10 mm per px).
- Trail checkbox toggles motion trails.
