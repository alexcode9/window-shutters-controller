#pragma once

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Persiana</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: ui-sans-serif, system-ui, sans-serif;
    background: #0e1116;
    color: #e7ecf3;
  }
  main {
    max-width: 960px;
    margin: 0 auto;
    padding: 24px 16px 40px;
  }
  h1 {
    font-size: 1.05rem;
    font-weight: 600;
    margin: 0 0 4px;
  }
  h2 {
    font-size: 0.95rem;
    font-weight: 600;
    margin: 0 0 12px;
  }
  .sub {
    margin: 0 0 18px;
    color: #8b98ab;
    font-size: 0.9rem;
  }
  .controls { display: flex; gap: 10px; flex-wrap: wrap; }
  button {
    appearance: none;
    border: 0;
    background: #243041;
    color: #e7ecf3;
    font: inherit;
    font-weight: 600;
    padding: 12px 22px;
    border-radius: 10px;
    cursor: pointer;
  }
  button.stop { background: #6b3030; }
  button.active { background: #1f6f4a; }
  .motion {
    margin: 12px 0 0;
    font-size: 1.15rem;
    font-weight: 600;
    text-transform: capitalize;
  }
  .settings {
    margin-top: 18px;
    padding: 16px;
    background: #161b22;
    border-radius: 12px;
  }
  .fields { display: flex; flex-wrap: wrap; gap: 14px; align-items: end; }
  label {
    display: flex;
    flex-direction: column;
    gap: 6px;
    color: #9aa6b8;
    font-size: 0.85rem;
  }
  input {
    background: #0e1116;
    color: #e7ecf3;
    border: 1px solid #2a3340;
    border-radius: 8px;
    padding: 8px 10px;
    font: inherit;
    width: 148px;
  }
  .hint { color: #8b98ab; font-size: 0.85rem; margin: 12px 0 0; }
  #saveNote { margin-left: 8px; color: #7dcea0; }
  .readout {
    display: flex;
    align-items: baseline;
    gap: 20px;
    flex-wrap: wrap;
    margin-top: 22px;
  }
  .amps {
    font-size: 3.4rem;
    line-height: 1;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
    letter-spacing: -0.03em;
  }
  .meta { color: #9aa6b8; line-height: 1.45; }
  .status { margin-left: auto; font-size: 0.95rem; }
  .ok { color: #7dcea0; }
  .bad { color: #e07a7a; }
  canvas {
    width: 100%;
    height: 380px;
    margin-top: 18px;
    background: #161b22;
    border-radius: 12px;
    display: block;
  }
</style>
</head>
<body>
<main>
  <h1>Persiana</h1>
  <p class="sub">Last 60 seconds, from <span id="host"></span></p>
  <div class="controls">
    <button type="button" id="btnOpen">Open</button>
    <button type="button" class="stop" id="btnStop">Stop</button>
    <button type="button" id="btnClose">Close</button>
  </div>
  <p class="motion" id="motion">stopped</p>
  <form class="settings" id="settings">
    <h2>Settings</h2>
    <div class="fields">
      <label>Current threshold (A)
        <input id="threshold" type="number" min="0.05" max="3" step="0.01" required>
      </label>
      <label>Time to open (s)
        <input id="openSeconds" type="number" min="1" max="180" step="1" required>
      </label>
      <label>Time to close (s)
        <input id="closeSeconds" type="number" min="1" max="180" step="1" required>
      </label>
      <button type="submit">Save</button>
      <span id="saveNote"></span>
    </div>
    <p class="hint">The orange lines are the threshold. Set it above the running current and below the bumper spike. The motor also stops when the travel time runs out.</p>
  </form>
  <div class="readout">
    <div class="amps" id="amps">&mdash;</div>
    <div class="meta">
      <div id="mv">&mdash; mV at OUT</div>
      <div id="dir">&mdash;</div>
    </div>
    <div class="status" id="status">connecting</div>
  </div>
  <canvas id="chart"></canvas>
</main>
<script>
const WINDOW_MS = 60000;
const points = [];
const canvas = document.getElementById("chart");
const ctx = canvas.getContext("2d");
let lastRx = 0;
let latestNow = 0;
let thresholdA = 1;

document.getElementById("host").textContent = location.host;

function age(now, then) {
  return (now - then) >>> 0;
}

function resize() {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = Math.max(1, Math.floor(rect.width * dpr));
  canvas.height = Math.max(1, Math.floor(rect.height * dpr));
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function formatSigned(amps) {
  const sign = amps >= 0 ? "+" : "";
  return sign + amps.toFixed(3) + " A";
}

function showMotion(state) {
  const name = state || "stopped";
  document.getElementById("motion").textContent = name;
  document.getElementById("btnOpen").className = name === "opening" ? "active" : "";
  document.getElementById("btnClose").className = name === "closing" ? "active" : "";
}

function draw(now) {
  const rect = canvas.getBoundingClientRect();
  const w = rect.width;
  const h = rect.height;
  ctx.clearRect(0, 0, w, h);

  const padL = 64;
  const padR = 16;
  const padT = 16;
  const padB = 28;
  const plotW = Math.max(1, w - padL - padR);
  const plotH = Math.max(1, h - padT - padB);

  let lo = 0;
  let hi = 0;
  for (let i = 0; i < points.length; i++) {
    const a = points[i].amps;
    if (a < lo) lo = a;
    if (a > hi) hi = a;
  }
  if (thresholdA > hi) hi = thresholdA;
  if (-thresholdA < lo) lo = -thresholdA;
  const pad = Math.max(0.02, (hi - lo) * 0.12);
  lo -= pad;
  hi += pad;
  if (hi - lo < 0.05) {
    const mid = (hi + lo) / 2;
    lo = mid - 0.025;
    hi = mid + 0.025;
  }
  lo = Math.min(lo, 0);
  hi = Math.max(hi, 0);
  const span = hi - lo;

  const xOf = (ms) => padL + (1 - age(now, ms) / WINDOW_MS) * plotW;
  const yOf = (a) => padT + (1 - (a - lo) / span) * plotH;

  ctx.font = "12px ui-sans-serif, system-ui, sans-serif";
  ctx.lineWidth = 1;
  ctx.textBaseline = "middle";

  for (let i = 0; i <= 4; i++) {
    const a = lo + span * (i / 4);
    const y = yOf(a);
    ctx.strokeStyle = "#2a3340";
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(w - padR, y);
    ctx.stroke();
    ctx.fillStyle = "#8b98ab";
    const digits = span < 0.5 ? 3 : 2;
    ctx.fillText(a.toFixed(digits), 8, y);
  }

  for (let s = 0; s <= 60; s += 15) {
    const x = padL + (s / 60) * plotW;
    ctx.strokeStyle = "#222a35";
    ctx.beginPath();
    ctx.moveTo(x, padT);
    ctx.lineTo(x, padT + plotH);
    ctx.stroke();
  }

  const y0 = yOf(0);
  ctx.strokeStyle = "#5c6b80";
  ctx.setLineDash([4, 4]);
  ctx.beginPath();
  ctx.moveTo(padL, y0);
  ctx.lineTo(w - padR, y0);
  ctx.stroke();
  ctx.setLineDash([]);

  ctx.strokeStyle = "#e0a15a";
  ctx.setLineDash([2, 4]);
  [thresholdA, -thresholdA].forEach((a) => {
    if (a < lo || a > hi) return;
    const y = yOf(a);
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(w - padR, y);
    ctx.stroke();
  });
  ctx.setLineDash([]);

  ctx.fillStyle = "#8b98ab";
  ctx.textBaseline = "alphabetic";
  ctx.fillText("-60 s", padL, h - 8);
  ctx.fillText("now", w - padR - 26, h - 8);

  if (points.length === 1) {
    ctx.fillStyle = "#6cb6ff";
    ctx.beginPath();
    ctx.arc(xOf(points[0].ms), yOf(points[0].amps), 3, 0, Math.PI * 2);
    ctx.fill();
    return;
  }
  if (points.length < 2) return;

  ctx.strokeStyle = "#6cb6ff";
  ctx.lineWidth = 2;
  ctx.lineJoin = "round";
  ctx.lineCap = "round";
  ctx.beginPath();
  for (let i = 0; i < points.length; i++) {
    const x = xOf(points[i].ms);
    const y = yOf(points[i].amps);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

function setStatus(text, ok) {
  const el = document.getElementById("status");
  el.textContent = text;
  el.className = ok ? "status ok" : "status bad";
}

async function poll() {
  try {
    const res = await fetch("/api/reading", { cache: "no-store" });
    if (!res.ok) throw new Error(String(res.status));
    const d = await res.json();
    lastRx = performance.now();
    latestNow = d.ms;
    const last = points[points.length - 1];
    if (!last || last.ms !== d.ms) points.push({ ms: d.ms, amps: d.amps });
    while (points.length && age(d.ms, points[0].ms) > WINDOW_MS) points.shift();
    document.getElementById("amps").textContent = formatSigned(d.amps);
    document.getElementById("mv").textContent = Math.round(d.mV) + " mV at OUT";
    document.getElementById("dir").textContent = d.reverse ? "reverse (RS- to RS+)" : "forward (RS+ to RS-)";
    showMotion(d.state);
    setStatus("live", true);
    draw(d.ms);
  } catch (err) {
    // One dropped reply is normal on this server. watchStale flags a real gap.
  }
}

function watchStale() {
  if (lastRx !== 0 && performance.now() - lastRx > 4000) setStatus("no data", false);
}

async function tick() {
  await poll();
  setTimeout(tick, 200);
}

async function loadSettings() {
  const res = await fetch("/api/settings", { cache: "no-store" });
  if (!res.ok) return;
  const d = await res.json();
  thresholdA = Number(d.threshold) || 0;
  document.getElementById("threshold").value = d.threshold;
  document.getElementById("openSeconds").value = d.openSeconds;
  document.getElementById("closeSeconds").value = d.closeSeconds;
  showMotion(d.state);
  if (latestNow) draw(latestNow);
}

async function command(cmd) {
  const body = new URLSearchParams({ cmd: cmd });
  const res = await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: body
  });
  if (!res.ok) return;
  const d = await res.json();
  showMotion(d.state);
}

document.getElementById("btnOpen").addEventListener("click", () => command("open"));
document.getElementById("btnStop").addEventListener("click", () => command("stop"));
document.getElementById("btnClose").addEventListener("click", () => command("close"));

document.getElementById("settings").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const note = document.getElementById("saveNote");
  const body = new URLSearchParams({
    threshold: document.getElementById("threshold").value,
    openSeconds: document.getElementById("openSeconds").value,
    closeSeconds: document.getElementById("closeSeconds").value
  });
  const res = await fetch("/api/settings", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: body
  });
  if (!res.ok) {
    note.textContent = "Could not save";
    return;
  }
  const d = await res.json();
  thresholdA = Number(d.threshold) || 0;
  note.textContent = "Saved";
  if (latestNow) draw(latestNow);
});

window.addEventListener("resize", () => {
  resize();
  draw(latestNow);
});
resize();
loadSettings();
tick();
setInterval(watchStale, 500);
</script>
</body>
</html>
)HTML";
