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
};

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
    const textDecoder = new TextDecoderStream();
    port.readable.pipeTo(textDecoder.writable);
    const reader = textDecoder.readable.getReader();

    serial.port = port;
    serial.reader = reader;
    serial.textDecoder = textDecoder;
    serial.connected = true;
    setConnectButton(true);
    setStatus('Connected @' + baud + ' baud');

    readLoop();
  } catch (e) {
    console.error('[WS] connect error', e);
    setStatus('Connection failed: ' + (e && e.message ? e.message : e));
  }
}

async function disconnectSerial() {
  console.log('[WS] disconnect');
  try {
    serial.connected = false;
    try { await serial.reader?.cancel(); } catch {}
    try { await serial.reader?.releaseLock(); } catch {}
    try { await serial.port?.close(); } catch {}
  } finally {
    setConnectButton(false);
    setStatus('Disconnected');
    renderTargetLog();
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
  try {
    while (serial.connected) {
      const { value, done } = await serial.reader.read();
      if (done) break;
      if (!value) continue;
      serial.buffer += value;
      let lines = serial.buffer.split('\n');
      serial.buffer = lines.pop();
      for (const line of lines) {
        const l = line.trim();
        if (!l) continue;
        appendLog(l);
        const raw = parseTargets(l) || parseHumanReadable(l);
        if (Array.isArray(raw)) {
          const t = raw.filter(tg => tg && typeof tg.y === 'number' && tg.y !== 0 && !(tg.x === 0 && tg.y === 0));
          serial.targets = t;
          renderTargetLog();
        }
      }
    }
  } catch (e) {
    console.warn('[WS] readLoop error', e);
  } finally {
    // Ensure cleanup in case of cable unplug etc.
    serial.connected = false;
    try { await serial.reader?.releaseLock(); } catch {}
    try { await serial.port?.close(); } catch {}
    setConnectButton(false);
    setStatus('Disconnected');
    renderTargetLog();
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
});
