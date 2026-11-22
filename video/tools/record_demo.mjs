// Records a REAL session of the HySW app (http://127.0.0.1:8372) in headless Chrome via the DevTools screencast.
// Every frame is the live app; only a drawn cursor (with click ripples) is added so viewers can follow the clicks.
//   node tools/record_demo.mjs demo        → demo/frames/*.jpg + demo/frames.json (timestamps) + demo/events.json
// Then tools/demo_to_mp4.sh builds demo/demo.mp4 at a constant 30 fps from the timestamps.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const OUT = path.resolve(process.argv[2] || 'demo'), FR = path.join(OUT, 'frames');
fs.rmSync(FR, { recursive: true, force: true }); fs.mkdirSync(FR, { recursive: true });
const URL0 = process.env.APP || 'http://127.0.0.1:8372/';
const CHROME = process.env.CHROME || path.join(os.homedir(), '.local/bin/google-chrome');
const VW = 1440, VH = 810, DPR = 1920 / 1440;                 // a laptop-sized layout, rendered at 1920x1080
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const prof = fs.mkdtempSync(path.join(os.tmpdir(), 'demo-chrome-'));
const chrome = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${prof}`, '--no-first-run',
  '--hide-scrollbars', `--window-size=${VW},${VH}`, '--ignore-gpu-blocklist', '--enable-unsafe-swiftshader', '--use-angle=swiftshader',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding', 'about:blank'], { stdio: 'ignore' });
let dbg; for (let i = 0; i < 200 && !dbg; i++) { try { dbg = fs.readFileSync(path.join(prof, 'DevToolsActivePort'), 'utf8').split('\n')[0].trim(); } catch { await sleep(100); } }
const target = await (await fetch(`http://127.0.0.1:${dbg}/json/new?about:blank`, { method: 'PUT' })).json();
const ws = new WebSocket(target.webSocketDebuggerUrl); await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
let id = 0; const pend = new Map(), frames = [], events = [];
const send = (method, params = {}) => new Promise((res, rej) => { const i = ++id; pend.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params })); });
ws.onmessage = async (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pend.has(m.id)) { const p = pend.get(m.id); pend.delete(m.id); m.error ? p.rej(new Error(m.error.message)) : p.res(m.result); return; }
  if (m.method === 'Page.screencastFrame') {
    const n = frames.length, f = `f${String(n).padStart(5, '0')}.jpg`;
    fs.writeFileSync(path.join(FR, f), Buffer.from(m.params.data, 'base64'));
    frames.push({ f, t: m.params.metadata.timestamp });
    send('Page.screencastFrameAck', { sessionId: m.params.sessionId }).catch(() => {});
  }
};
const ev = async (expr) => { const x = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }); if (x.exceptionDetails) throw new Error(x.exceptionDetails.text + ' ' + (x.exceptionDetails.exception?.description || '')); return x.result.value; };
const mark = (name) => { events.push({ name, t: Date.now() / 1000 }); console.log(name); };

// the drawn cursor: an arrow that follows real mouse events, plus a ring on every click
const CURSOR = `addEventListener('DOMContentLoaded', () => {
  const c = document.createElement('div'); c.id = '__cur';
  c.innerHTML = '<svg width="26" height="34" viewBox="0 0 26 34"><path d="M2 2 L2 27 L8.5 21 L13 31.5 L17.5 29.5 L13 19.5 L22 19.5 Z" fill="#fff" stroke="#111" stroke-width="2.2" stroke-linejoin="round"/></svg>';
  Object.assign(c.style, { position: 'fixed', left: '0', top: '0', zIndex: 2147483647, pointerEvents: 'none', transform: 'translate(-100px,-100px)', filter: 'drop-shadow(1px 2px 2px rgba(0,0,0,.35))' });
  document.body.append(c);
  addEventListener('mousemove', (e) => { c.style.transform = 'translate(' + (e.clientX - 2) + 'px,' + (e.clientY - 2) + 'px)'; }, true);
  addEventListener('mousedown', (e) => { const r = document.createElement('div');
    Object.assign(r.style, { position: 'fixed', left: e.clientX - 22 + 'px', top: e.clientY - 22 + 'px', width: '44px', height: '44px', borderRadius: '50%', border: '3px solid #FFC857', zIndex: 2147483646, pointerEvents: 'none', transition: 'transform .45s ease-out, opacity .45s ease-out' });
    document.body.append(r); requestAnimationFrame(() => { r.style.transform = 'scale(1.8)'; r.style.opacity = '0'; }); setTimeout(() => r.remove(), 600); }, true);
});`;

let mx = VW * 0.6, my = VH * 0.7;
async function moveTo(x, y, ms = 650) {
  const n = Math.max(8, Math.round(ms / 16)), x0 = mx, y0 = my;
  for (let i = 1; i <= n; i++) { const u = i / n, e = u < 0.5 ? 2 * u * u : 1 - Math.pow(-2 * u + 2, 2) / 2;
    mx = x0 + (x - x0) * e; my = y0 + (y - y0) * e; await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: mx, y: my }); await sleep(16); }
}
async function click() { await send('Input.dispatchMouseEvent', { type: 'mousePressed', x: mx, y: my, button: 'left', clickCount: 1 }); await sleep(90);
  await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: mx, y: my, button: 'left', clickCount: 1 }); }
const box = (sel) => ev(`(() => { const e = ${sel}; if (!e) return null; const r = e.getBoundingClientRect(); return [r.x + r.width / 2, r.y + r.height / 2, r.width, r.height]; })()`);
async function clickEl(sel, ms) { const b = await box(sel); if (!b) throw new Error('not found: ' + sel); await moveTo(b[0], b[1], ms); await sleep(180); await click(); }
const waitFor = async (expr, sec = 60) => { for (let i = 0; i < sec * 10; i++) { if (await ev(expr).catch(() => false)) return true; await sleep(100); } throw new Error('timeout: ' + expr); };
async function scrollTo(expr, ms = 900) {   // smooth scroll so the motion reads on video
  const y0 = await ev('scrollY'), y1 = await ev(expr), n = Math.round(ms / 16);
  for (let i = 1; i <= n; i++) { const u = i / n, e = u < 0.5 ? 2 * u * u : 1 - Math.pow(-2 * u + 2, 2) / 2; await ev(`scrollTo(0, ${y0 + (y1 - y0) * e})`); await sleep(16); }
}

try {
  await send('Page.enable'); await send('Runtime.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: VW, height: VH, deviceScaleFactor: DPR, mobile: false });
  await send('Page.addScriptToEvaluateOnNewDocument', { source: CURSOR });
  await send('Page.navigate', { url: URL0 });
  await waitFor(`document.querySelectorAll('#examples button').length > 0`, 30);
  await sleep(800);
  await send('Page.startScreencast', { format: 'jpeg', quality: 88, maxWidth: 1920, maxHeight: 1080, everyNthFrame: 1 });
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: mx, y: my });
  mark('start'); await sleep(1800);
  await clickEl(`[...document.querySelectorAll('#examples button')].find((b) => /hemoglobin subunit beta/i.test(b.textContent))`, 900);
  mark('example'); await sleep(1300);
  await clickEl(`document.getElementById('go')`, 800);
  mark('search');
  await waitFor(`!document.getElementById('results').hidden && document.querySelectorAll('#hits tbody tr').length > 0`, 120);
  mark('results'); await sleep(1600);
  const ms = await ev(`document.getElementById('elapsed').textContent`); console.log('elapsed shown:', ms);
  await scrollTo(`document.getElementById('results').getBoundingClientRect().top + scrollY - 70`, 1100);
  mark('table'); await sleep(2600);
  await scrollTo(`document.getElementById('detail').getBoundingClientRect().top + scrollY - 60`, 1200);
  mark('detail');
  await waitFor(`/./.test(document.getElementById('viewer-state')?.textContent || '') || document.querySelector('#viewer canvas')`, 30).catch(() => {});
  await sleep(2500);
  // point along the alignment: the structure highlights the residue under the cursor
  const res = await ev(`(() => { const s = [...document.querySelectorAll('#aln [data-i], #aln span[data-q], #aln span')].filter((e) => e.textContent.trim().length === 1).slice(10, 70); return s.map((e) => { const r = e.getBoundingClientRect(); return [r.x + r.width / 2, r.y + r.height / 2]; }); })()`);
  if (res && res.length) { for (const [x, y] of res.filter((_, i) => i % 6 === 0)) { await moveTo(x, y, 260); await sleep(260); } }
  mark('hover'); await sleep(1500);
  await scrollTo(`document.getElementById('viewer').getBoundingClientRect().top + scrollY - 140`, 1000);
  mark('structure'); await sleep(3500);
  await scrollTo('0', 900);
  await clickEl(`document.querySelector('nav .tab[data-view="lab"]')`, 800);
  mark('lab'); await sleep(2500);
  await scrollTo(`document.getElementById('pair-panel').getBoundingClientRect().top + scrollY - 70`, 1000);
  mark('pair'); await sleep(2500);
  await clickEl(`document.getElementById('pair-run')`, 800);
  mark('run');
  await waitFor(`/again|run/i.test(document.getElementById('pair-run').textContent) && !document.getElementById('pair-run').disabled && document.getElementById('pair-status').textContent.length > 0`, 90).catch(() => {});
  await sleep(1500);
  await waitFor(`!document.getElementById('pair-run').disabled`, 90).catch(() => {});
  mark('lab-done'); await sleep(3000);
  mark('end');
  await send('Page.stopScreencast');
} finally {
  fs.writeFileSync(path.join(OUT, 'frames.json'), JSON.stringify(frames));
  fs.writeFileSync(path.join(OUT, 'events.json'), JSON.stringify(events));
  console.log(`${frames.length} frames over ${frames.length ? (frames.at(-1).t - frames[0].t).toFixed(1) : 0} s`);
  try { ws.close(); } catch {} chrome.kill('SIGKILL');
}
