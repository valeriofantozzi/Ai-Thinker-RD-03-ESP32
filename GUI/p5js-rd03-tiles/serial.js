// Web Serial reader for RD-03D JSON lines
// Expected JSON per line: {"t":[{"x":int,"y":int,"d":float,"a":float,"s":float}, ...]}
'use strict';

let serial = {
  port: null,
  reader: null,
  textDecoder: null,
  connected: false,
  buffer: '',
  targets: [],
  zones: {}, // zone occupancy states {A1: true, B2: false, ...}
};

// Reflect device setting messages to UI checkboxes when possible
function parseDeviceSettings(line){
  // Debug raw targets: ON/OFF
  let m = line.match(/^Debug raw targets:\s*(ON|OFF)$/i);
  if (m) {
    const el = document.getElementById('debug');
    if (el) el.checked = (m[1].toUpperCase() === 'ON');
    return;
  }
  // Multi-target mode: ON/OFF or mode enabled lines
  m = line.match(/^(Multi-target mode:\s*(ON|OFF)|Multi-target mode enabled|Single-target mode enabled)$/i);
  if (m) {
    const el = document.getElementById('multi');
    if (el) {
      const on = /Multi-target mode enabled/i.test(line) || /:\s*ON$/i.test(line);
      el.checked = on;
    }
    return;
  }
  // EMA smoothing: ON/OFF
  m = line.match(/^EMA smoothing:\s*(ON|OFF)$/i);
  if (m) {
    const el = document.getElementById('ema');
    if (el) el.checked = (m[1].toUpperCase() === 'ON');
    return;
  }
}

function renderTargetLog() {
  const raw = document.getElementById('rawlog');
  const log = document.getElementById('log');
  if (!log) return;
  if (raw && raw.checked) return; // show raw instead
  const tgs = Array.isArray(serial.targets) ? serial.targets.filter(t => t && typeof t.y === 'number' && t.y !== 0 && !(t.x === 0 && t.y === 0)) : [];
  const lines = [];
  lines.push('Targets: ' + tgs.length);
  for (let i = 0; i < tgs.length; i++) {
    const t = tgs[i] || {};
    const d = (typeof t.d === 'number') ? Math.round(t.d) : 0;
    const a = (typeof t.a === 'number') ? t.a.toFixed(1) : '0.0';
    const s = (typeof t.s === 'number') ? t.s.toFixed(1) : '0.0';
    const x = (typeof t.x === 'number') ? t.x : 0;
    const y = (typeof t.y === 'number') ? t.y : 0;
    lines.push(`#${i} x:${x} y:${y} d:${d} a:${a} s:${s}`);
  }
  log.textContent = lines.join('\n');
}

function setStatus(text) {
  const el = document.getElementById('status');
  if (el) el.textContent = text;
}

function setConnectButton(connected) {
  const btn = document.getElementById('connect');
  if (!btn) return;
  btn.textContent = connected ? 'Disconnect' : 'Connect';
}

async function connectSerial(baud = 256000) {
  try {
    console.log('[WS] requestPort...');
    const port = await navigator.serial.requestPort();
    console.log('[WS] opening @', baud);
    await port.open({ baudRate: baud });
    
    // Don't pipe the readable stream - keep it free for manual reading
    // This allows writable to remain available
    const reader = port.readable.getReader();

    serial.port = port;
    serial.reader = reader;
    serial.textDecoder = new TextDecoder();
    serial.connected = true;
    setConnectButton(true);
    setStatus('Connected @' + baud + ' baud (R/W)');
    
    console.log('[WS] Port readable:', !!port.readable);
    console.log('[WS] Port writable:', !!port.writable);

    readLoop();
  } catch (e) {
    console.error('[WS] connect error', e);
    setStatus('Connection failed: ' + (e && e.message ? e.message : e));
  }
}

async function disconnectSerial() {
  console.log('[WS] disconnect');
  serial.connected = false; // Stop the read loop first
  
  try {
    // Cancel the reader to interrupt any pending read
    if (serial.reader) {
      try {
        await serial.reader.cancel();
      } catch (e) {
        console.warn('[WS] Reader cancel failed:', e);
      }
      
      try {
        await serial.reader.releaseLock();
      } catch (e) {
        console.warn('[WS] Reader release failed:', e);
      }
      serial.reader = null;
    }
    
    // Close the port
    if (serial.port) {
      try {
        await serial.port.close();
      } catch (e) {
        console.warn('[WS] Port close failed:', e);
      }
      serial.port = null;
    }
    
    // Clean up text decoder (no need to close - it's just a decoder instance)
    serial.textDecoder = null;
    
  } catch (e) {
    console.error('[WS] Disconnect error:', e);
  } finally {
    // Always update UI state
    serial.connected = false;
    serial.port = null;
    serial.reader = null;
    serial.textDecoder = null;
    setConnectButton(false);
    setStatus('Disconnected');
    renderTargetLog();
    console.log('[WS] Disconnect complete');
  }
}

// Send command to ESP32 via serial
async function sendSerialCommand(command) {
  if (!serial.connected || !serial.port) {
    console.warn('[WS] Cannot send command - not connected');
    setStatus('Not connected - cannot send command');
    return false;
  }
  
  try {
    console.log('[WS] Attempting to send command:', command);
    const writer = serial.port.writable.getWriter();
    const encoder = new TextEncoder();
    const data = encoder.encode(command + '\n');
    console.log('[WS] Encoded data:', data);
    await writer.write(data);
    writer.releaseLock();
    console.log('[WS] Command sent successfully:', command);
    setStatus('Command sent: ' + command);
    return true;
  } catch (e) {
    console.error('[WS] Error sending command:', e);
    setStatus('Error sending command: ' + e.message);
    return false;
  }
}

function appendLog(line) {
  const raw = document.getElementById('rawlog');
  const log = document.getElementById('log');
  if (raw && raw.checked && log) {
    log.textContent += line + '\n';
    log.scrollTop = log.scrollHeight;
  }
}

async function readLoop() {
  console.log('[WS] Starting read loop');
  try {
    while (serial.connected && serial.reader) {
      if (!serial.reader) {
        console.log('[WS] Reader is null, exiting read loop');
        break;
      }
      
      const { value, done } = await serial.reader.read();
      
      // Check if we're still connected after the read
      if (!serial.connected) {
        console.log('[WS] Connection closed during read, exiting loop');
        break;
      }
      
      if (done) {
        console.log('[WS] Read done, stream ended');
        break;
      }
      
      if (!value) continue;
      
      // Decode the Uint8Array to string
      const text = serial.textDecoder.decode(value, { stream: true });
      serial.buffer += text;
      let lines = serial.buffer.split('\n');
      serial.buffer = lines.pop();
      
      for (const line of lines) {
        const l = line.trim();
        if (!l) continue;
        appendLog(l);
        // Parse device settings toggles and reflect UI state
        parseDeviceSettings(l);
        
        // First try parsing zone status
        const zoneStatus = parseZoneStatus(l);
        if (zoneStatus !== null) {
          // Update zone states
          if (typeof zoneStatus === 'object') {
            Object.assign(serial.zones, zoneStatus);
          }
        }
        
        // Then try parsing targets
        const raw = parseTargets(l) || parseHumanReadable(l);
        if (Array.isArray(raw)) {
          const t = raw.filter(tg => tg && typeof tg.y === 'number' && tg.y !== 0 && !(tg.x === 0 && tg.y === 0));
          serial.targets = t;
          renderTargetLog();
        }
      }
    }
  } catch (e) {
    if (e.name === 'NetworkError' || e.message.includes('cancelled')) {
      console.log('[WS] Read cancelled (normal for disconnect)');
    } else {
      console.warn('[WS] readLoop error', e);
      setStatus('Read error: ' + e.message);
    }
  } finally {
    console.log('[WS] Read loop ended');
    // Only clean up if we're not in the middle of a controlled disconnect
    if (serial.connected) {
      console.log('[WS] Unexpected disconnection, cleaning up');
      serial.connected = false;
      setConnectButton(false);
      setStatus('Connection lost');
      renderTargetLog();
    }
  }
}

function parseTargets(line) {
  if (!line) return null;
  try {
    const obj = JSON.parse(line);
    if (Array.isArray(obj.t)) return obj.t;
  } catch {}
  return null;
}

// Parse zone status from ESP32 output
// Expected formats:
// "Zone ON: A1" -> {A1: true}
// "Zone OFF: B2" -> {B2: false}
// "[X] A1" -> {A1: true} (from status output)
// "=== ZONE STATUS ===" -> starts zone status block
function parseZoneStatus(line) {
  if (!line) return null;
  
  // Zone ON/OFF messages
  const onMatch = line.match(/Zone ON:\s*([A-D][1-4])/);
  if (onMatch) {
    return { [onMatch[1]]: true };
  }
  
  const offMatch = line.match(/Zone OFF:\s*([A-D][1-4])/);
  if (offMatch) {
    return { [offMatch[1]]: false };
  }
  
  // Zone status list parsing
  if (line === '=== ZONE STATUS ===') {
    // Clear all zones when starting a new status block
    for (const zone of ['A1','B1','C1','D1','A2','B2','C2','D2','A3','B3','C3','D3','B4','C4']) {
      serial.zones[zone] = false;
    }
    return {};
  }
  
  // Active zone in status list
  const activeMatch = line.match(/\[X\]\s*([A-D][1-4])/);
  if (activeMatch) {
    return { [activeMatch[1]]: true };
  }
  
  return null;
}

// Fallback: parse human-readable Arduino output
// Expected lines:
//   Targets: N
//   [i]
//   X (mm): 123
//   Y (mm): -45
//   Distance (mm): 456.7
//   Angle (degrees): -12.3
//   Speed (cm/s): 0.0
let hrFrame = { targets: [], current: null, expectedCount: null };
function parseHumanReadable(line) {
  if (!line) return null;
  const tg = hrFrame;
  const mTargets = line.match(/^Targets:\s*(\d+)/i);
  if (mTargets) { tg.targets = []; tg.current = null; tg.expectedCount = parseInt(mTargets[1],10); return tg.targets; }
  const mIdx = line.match(/^\[(\d+)\]/);
  if (mIdx) { tg.current = { x:0,y:0,d:0,a:0,s:0 }; tg.targets.push(tg.current); return tg.targets; }
  if (!tg.current) return null;
  let m;
  if ((m = line.match(/^X \(mm\):\s*(-?\d+)/i))) { tg.current.x = parseInt(m[1]); return tg.targets; }
  if ((m = line.match(/^Y \(mm\):\s*(-?\d+)/i))) { tg.current.y = parseInt(m[1]); return tg.targets; }
  if ((m = line.match(/^Distance \(mm\):\s*(-?\d+(?:\.\d+)?)/i))) { tg.current.d = parseFloat(m[1]); return tg.targets; }
  if ((m = line.match(/^Angle \(degrees\):\s*(-?\d+(?:\.\d+)?)/i))) { tg.current.a = parseFloat(m[1]); return tg.targets; }
  if ((m = line.match(/^Speed \(cm\/s\):\s*(-?\d+(?:\.\d+)?)/i))) { tg.current.s = parseFloat(m[1]); return tg.targets; }
  // When a block is finished (no more matching), sanitize current array
  const cleaned = tg.targets.filter(t => t && typeof t.y === 'number' && t.y !== 0 && !(t.x === 0 && t.y === 0));
  return cleaned.length ? cleaned : null;
}

window.addEventListener('error', (e) => {
  console.error('[WS] window error', e.message);
  appendLog('Error: ' + e.message);
});

window.addEventListener('unhandledrejection', (e) => {
  console.error('[WS] unhandledrejection', e.reason);
  appendLog('Unhandled: ' + (e.reason && e.reason.message ? e.reason.message : e.reason));
});

document.addEventListener('DOMContentLoaded', () => {
  const app = document.getElementById('app');
  const sidebar = document.getElementById('sidebar');
  const footer = document.getElementById('footer');
  document.getElementById('toggle-sidebar')?.addEventListener('click', ()=>{
    const collapsed = sidebar.classList.toggle('collapsed');
    if (app) app.classList.toggle('sidebar-collapsed', collapsed);
    const btn = document.getElementById('toggle-sidebar');
    if (btn) btn.textContent = collapsed ? '⟩' : '⟨';
    window.dispatchEvent(new Event('resize'));
  });
  document.getElementById('toggle-footer')?.addEventListener('click', ()=>{
    const collapsed = footer.classList.toggle('collapsed');
    if (app) app.classList.toggle('footer-collapsed', collapsed);
    const btn = document.getElementById('toggle-footer');
    if (btn) btn.textContent = collapsed ? '⌃' : '⌄';
    window.dispatchEvent(new Event('resize'));
  });

  const connectBtn = document.getElementById('connect');
  setConnectButton(false);
  connectBtn?.addEventListener('click', async () => {
    console.log('[WS] Connect/Disconnect clicked');
    if (!('serial' in navigator)) {
      setStatus('Web Serial not supported. Use Chrome');
      return;
    }
    if (serial.connected) {
      await disconnectSerial();
      return;
    }
    const baud = parseInt(document.getElementById('baud').value || '256000', 10);
    await connectSerial(baud);
  });
  const logEl = document.getElementById('log');
  const clearBtn = document.getElementById('clearlog');
  if (clearBtn && logEl) clearBtn.onclick = () => { logEl.textContent = ''; };
  
  // DEBUG toggle (already implemented on device)
  const debugChk = document.getElementById('debug');
  if (debugChk) {
    debugChk.addEventListener('change', async () => {
      await sendSerialCommand('DEBUG');
    });
  }

  // MULTI toggle
  const multiChk = document.getElementById('multi');
  if (multiChk) {
    multiChk.addEventListener('change', async () => {
      await sendSerialCommand('MULTI');
    });
  }

  // EMA toggle
  const emaChk = document.getElementById('ema');
  if (emaChk) {
    emaChk.addEventListener('change', async () => {
      await sendSerialCommand('EMA');
    });
  }

  // ZONES button
  const zonesBtn = document.getElementById('zones-btn');
  if (zonesBtn) zonesBtn.addEventListener('click', async () => { await sendSerialCommand('ZONES'); });

  // HELP button
  const helpBtn = document.getElementById('help-btn');
  if (helpBtn) helpBtn.addEventListener('click', async () => { await sendSerialCommand('HELP'); });
});
