let cnv, infoEl;
let trailEnabled = true;
let trails = []; // history of points
const TRAIL_MS = 30000; // trail length in ms

function resizeToContainer(){
  const el = document.getElementById("canvas-container");
  if (!el) return;
  const w = el.clientWidth;
  const h = el.clientHeight;
  if (w>0 && h>0) resizeCanvas(w, h);
}

function setup() {
  cnv = createCanvas(10, 10); resizeToContainer();
  cnv.parent("canvas-container");
  frameRate(60);
  infoEl = document.getElementById("info");

  // controls
  const halfEl = document.getElementById('half');

  document.getElementById("trail").addEventListener("change", (e) => {
    trailEnabled = e.target.checked;
    if (!trailEnabled) trails = [];
  });

}

function draw() {
  const mmPerPx = max(1, parseFloat(document.getElementById("scale").value) || 10);
  background(18);
  const half = !!(document.getElementById('half')?.checked);
  const pad = 20;
  if (half) { translate(width/2, height - pad); } else { translate(width/2, height/2); }

  // grid
  stroke(50); strokeWeight(1);
  let maxR;
  if (half) {
    maxR = Math.max(0, Math.min(width/2 - pad, height - pad*2));
  } else {
    maxR = Math.max(0, Math.min(width, height)/2 - pad);
  }
  const targetRings = 6;
  const stepPx = maxR / targetRings;
  for (let r = stepPx; r <= maxR + 1; r += stepPx) {
    noFill();
    if (half) {
      // top semicircle
      arc(0, 0, r*2, r*2, PI, 0);
    } else {
      circle(0,0, r*2);
    }
    // distance label
    const distMM = r * mmPerPx;
    let label;
    if (distMM >= 1000) {
      const m = distMM / 1000.0;
      label = (m >= 10 ? m.toFixed(0) : m.toFixed(1)) + ' m';
    } else {
      const cm = distMM / 10.0;
      label = Math.round(cm).toString() + ' cm';
    }
    push();
    noStroke();
    fill(180);
    textSize(12);
    textAlign(CENTER, BOTTOM);
    text(label, 0, -r - 4);
    pop();
  }
  stroke(40);
  if (half) {
    // baseline only
    line(-width/2, 0, width/2, 0);
  } else {
    line(-width/2, 0, width/2, 0);
    line(0, -height/2, 0, height/2);
  }

  // draw targets
  const k = 1.0 / mmPerPx; // mm -> px
  const tgs = serial.targets || [];

  function applyTransform(px, py){
    return { x: px, y: py };
  }
  function inFov(x, y){
    if (!half) return true;
    return y <= 0;
  }

  // trails
  if (trailEnabled) {
    const now = millis();
    // push current frame (only in-FOV points) with timestamp
    if (tgs.length > 0) {
      const ptsMm = [];
      for (const t of tgs) {
        ptsMm.push({ x: t.x, y: t.y });
      }
      if (ptsMm.length > 0) trails.push({ t: now, pts: ptsMm });
    }
    // prune older than TRAIL_MS
    while (trails.length && (now - trails[0].t) > TRAIL_MS) trails.shift();
    // draw line segments between consecutive frames for each target index
    strokeWeight(2);
    noFill();
    for (let i = 1; i < trails.length; i++) {
      const prev = trails[i-1];
      const curr = trails[i];
      const n = Math.min(prev.pts.length, curr.pts.length);
      const age = (now - prev.t) / TRAIL_MS; // 0 new .. 1 old
      const alpha = Math.max(0, Math.min(160, 160 * (1 - age) + 20));
      stroke(0, 180, 255, alpha);
      for (let j = 0; j < n; j++) {
        const aMm = prev.pts[j];
        const bMm = curr.pts[j];
        const a = applyTransform(aMm.x * k, -aMm.y * k);
        const b = applyTransform(bMm.x * k, -bMm.y * k);
        if (inFov(a.x, a.y) || inFov(b.x, b.y)) {
          line(a.x, a.y, b.x, b.y);
        }
      }
    }
  }

  // current points
  noStroke();
  for (let i = 0; i < tgs.length; i++) {
    const t = tgs[i];
    if (!t || t.y === 0 || (t.x === 0 && t.y === 0)) continue;
    let tp = applyTransform(t.x * k, -t.y * k);
    const x = tp.x;
    const y = tp.y;
    if (!inFov(x,y)) continue;
    fill(0, 220, 255);
    circle(x, y, 10);

    fill(220);
    textSize(12);
    textAlign(LEFT, BOTTOM);
    const lab = `#${i} x:${t.x} y:${t.y} d:${nf(t.d,1,0)}mm a:${nf(t.a,1,1)} s:${nf(t.s,1,1)}`;
    push(); translate(x+8, y-8); rotate(0); text(lab, 0, 0); pop();
  }

  // info panel
  if (infoEl) {
    const lines = [];
    lines.push(`Targets: ${tgs.length}`);
    tgs.forEach((t,i)=>{
      lines.push(`#${i} x:${t.x} y:${t.y} d:${t.d.toFixed(0)} a:${t.a.toFixed(1)} s:${t.s.toFixed(1)}`);
    });
    infoEl.textContent = lines.join("\n");
  }
}

function windowResized(){ resizeToContainer(); }
