"""Builds a replay page from a recorded gym run (see Replay.ps1): the frames the game saved (ScreenShots/Replay_*.png) and the run's log,
laid together in one self-contained HTML file. Each frame shows with the trace lines written since the frame before (the route-follower's
decisions, the routes asked for, the course's result), and the whole log of those lines runs beside it, the current one highlighted.

    python MakeReplay.py <run log> <screenshot folder> <output .html> [title]
"""
import base64
import glob
import html
import io
import json
import os
import re
import sys

from PIL import Image

log_path, shot_dir, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
title = sys.argv[4] if len(sys.argv) > 4 else os.path.basename(out_path)

FRAME = re.compile(r'^REPLAY frame (\d+) t ([\d.]+) at (-?\d+),(-?\d+) vel (-?[\d.]+),(-?[\d.]+)')
KEEP = re.compile(r'^(AITRACE (mover|path for|climb|GoToRoute)|AIBUNKER .*: (arrived|GAVE UP|died))')

frames = []
pending = []
results = []
with open(log_path, encoding='utf-8', errors='replace') as f:
    for raw in f:
        line = raw.rstrip('\n').rstrip('\r')
        m = FRAME.match(line)
        if m:
            frames.append({'n': int(m.group(1)), 't': float(m.group(2)), 'pos': [int(m.group(3)), int(m.group(4))],
                           'vel': [float(m.group(5)), float(m.group(6))], 'lines': pending})
            pending = []
        elif KEEP.match(line):
            text = line[:220]
            if text.startswith('AIBUNKER'):
                results.append(text)
            pending.append(text)
if pending and frames:
    frames[-1]['lines'].extend(pending)

shots = {}
for path in glob.glob(os.path.join(shot_dir, 'Replay_*.png')):
    m = re.match(r'Replay_(\d+)', os.path.basename(path))
    if m:
        shots[int(m.group(1))] = path

kept = []
for frame in frames:
    path = shots.get(frame['n'])
    if not path:
        continue
    img = Image.open(path).convert('RGB')
    # The middle of the screen, where the camera keeps the unit, at a size a page plays smoothly.
    w, h = img.size
    crop = img.crop((w * 0.15, h * 0.1, w * 0.85, h * 0.9))
    crop = crop.resize((640, int(crop.height * 640 / crop.width)))
    buf = io.BytesIO()
    crop.save(buf, 'JPEG', quality=62)
    frame['img'] = base64.b64encode(buf.getvalue()).decode('ascii')
    kept.append(frame)

data = json.dumps({'title': title, 'results': results, 'frames': kept})

page = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Gym Replay</title>
<style>
:root { --bg: #f4f5f7; --panel: #fff; --ink: #1c2027; --dim: #6b7280; --line: #e3e5ea; --accent: #2563eb; --hi: #fff3c4; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg: #111318; --panel: #1a1d24; --ink: #e6e8ee; --dim: #9aa1ae; --line: #2a2f3a; --accent: #5aa9f0; --hi: #3a3320; } }
:root[data-theme="dark"] { --bg: #111318; --panel: #1a1d24; --ink: #e6e8ee; --dim: #9aa1ae; --line: #2a2f3a; --accent: #5aa9f0; --hi: #3a3320; }
* { box-sizing: border-box; }
body { margin: 0; padding: 16px; background: var(--bg); color: var(--ink); font: 14px/1.4 system-ui, -apple-system, Segoe UI, sans-serif; }
h1 { font-size: 17px; margin: 0 0 4px; }
.sub { color: var(--dim); font-size: 12px; margin-bottom: 10px; }
.wrap { display: grid; grid-template-columns: minmax(0, 720px) minmax(0, 1fr); gap: 14px; }
@media (max-width: 900px) { .wrap { grid-template-columns: 1fr; } }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 10px; }
img { width: 100%; display: block; border-radius: 6px; image-rendering: pixelated; }
.controls { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-top: 8px; }
button { background: var(--panel); color: var(--ink); border: 1px solid var(--line); border-radius: 6px; padding: 4px 10px; cursor: pointer; font: inherit; }
button.on { border-color: var(--accent); color: var(--accent); }
input[type=range] { flex: 1; min-width: 160px; }
.now { font-variant-numeric: tabular-nums; color: var(--dim); font-size: 12px; }
.lines { font: 12px/1.45 ui-monospace, Consolas, monospace; max-height: 70vh; overflow: auto; }
.ev { padding: 1px 6px; border-radius: 4px; cursor: pointer; white-space: pre-wrap; word-break: break-word; }
.ev .t { color: var(--dim); margin-right: 6px; }
.ev.cur { background: var(--hi); }
.ev.res { font-weight: 600; }
.here { min-height: 3.2em; font: 12px/1.45 ui-monospace, Consolas, monospace; margin-top: 8px; white-space: pre-wrap; }
</style></head><body>
<h1 id="title"></h1>
<div class="sub" id="results"></div>
<div class="wrap">
  <div class="card">
    <img id="frame" alt="frame">
    <div class="controls">
      <button id="play">Play</button><button id="back">&larr;</button><button id="fwd">&rarr;</button>
      <button data-speed="0.5">0.5&times;</button><button data-speed="1" class="on">1&times;</button><button data-speed="2">2&times;</button>
      <input type="range" id="scrub" min="0" value="0">
      <span class="now" id="now"></span>
    </div>
    <div class="here" id="here"></div>
  </div>
  <div class="card lines" id="lines"></div>
</div>
<script>
const D = ''' + data + ''';
const F = D.frames;
let i = 0, playing = false, speed = 1, timer = null;
document.getElementById('title').textContent = D.title;
document.getElementById('results').textContent = D.results.join('  |  ') || 'No result line in the log.';
const scrub = document.getElementById('scrub');
scrub.max = Math.max(0, F.length - 1);
const list = document.getElementById('lines');
const rows = [];
F.forEach((f, k) => f.lines.forEach((text) => {
  const el = document.createElement('div');
  el.className = 'ev' + (text.startsWith('AIBUNKER') ? ' res' : '');
  el.innerHTML = '<span class="t">' + f.t.toFixed(1) + 's</span>';
  el.appendChild(document.createTextNode(text.replace(/^AITRACE /, '')));
  el.onclick = () => show(k);
  list.appendChild(el);
  rows.push({ k, el });
}));
function show(k) {
  i = Math.max(0, Math.min(F.length - 1, k));
  const f = F[i];
  if (!f) return;
  document.getElementById('frame').src = 'data:image/jpeg;base64,' + f.img;
  scrub.value = i;
  document.getElementById('now').textContent = 'frame ' + (i + 1) + '/' + F.length + ' \\u00b7 ' + f.t.toFixed(1) + ' s \\u00b7 at ' + f.pos.join(',') + ' \\u00b7 vel ' + f.vel.join(',');
  document.getElementById('here').textContent = f.lines.map((x) => x.replace(/^AITRACE /, '')).join('\\n') || ' ';
  let last = null;
  rows.forEach((r) => { const on = r.k === i; r.el.classList.toggle('cur', on); if (r.k <= i) last = r.el; });
  if (last) { const top = last.offsetTop - list.offsetTop; if (top < list.scrollTop || top > list.scrollTop + list.clientHeight - 40) list.scrollTop = top - list.clientHeight / 2; }
}
function tick() { if (i >= F.length - 1) { stop(); return; } show(i + 1); }
function start() { playing = true; document.getElementById('play').textContent = 'Pause'; clearInterval(timer); timer = setInterval(tick, 200 / speed); }
function stop() { playing = false; document.getElementById('play').textContent = 'Play'; clearInterval(timer); }
document.getElementById('play').onclick = () => playing ? stop() : start();
document.getElementById('back').onclick = () => { stop(); show(i - 1); };
document.getElementById('fwd').onclick = () => { stop(); show(i + 1); };
scrub.oninput = () => { stop(); show(+scrub.value); };
document.querySelectorAll('[data-speed]').forEach((b) => b.onclick = () => {
  speed = +b.dataset.speed; document.querySelectorAll('[data-speed]').forEach((x) => x.classList.toggle('on', x === b)); if (playing) start();
});
document.addEventListener('keydown', (e) => { if (e.key === ' ') { e.preventDefault(); playing ? stop() : start(); } if (e.key === 'ArrowLeft') { stop(); show(i - 1); } if (e.key === 'ArrowRight') { stop(); show(i + 1); } });
show(0);
</script></body></html>'''

with open(out_path, 'w', encoding='utf-8') as f:
    f.write(page)
print(f'{len(kept)} frames, {sum(len(x["lines"]) for x in kept)} trace lines -> {out_path} ({os.path.getsize(out_path) // 1024} KB)')
