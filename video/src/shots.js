// The HySW explainer: one shot per narration line (77 lines, ~6:45). Every shot is a pure function of time.
// Measured data drawn directly: data/e4_passes.json (E4 burst→sustained), data/e3_trace.json (E3 per-thread finish times).
// The demo shots show demo/f/*.jpg: a real headless-Chrome recording of the app (tools/record_demo.mjs).
import { W, H, ctx, C, BEADS, clamp, lerp, seg, eo, eio, eob, rnd, rr, circle, line, fillAll, text, tw, card, bead, fmt, TAU } from './core/lib.js';
import { T, L, Le, Wd } from './core/timing.js';
import { art } from './art.js';

const IMG = {}, DATA = {};
const svgImg = (s) => new Promise((res, rej) => { const i = new Image(); i.onload = () => res(i); i.onerror = rej; i.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(s); });
export async function loadAssets() {
  const list = { laptop: art.title(), query: art.query(), snail: art.snail(), chip: art.chip(false), chipD: art.chip(true), ht: art.ht(), power: art.power(),
    cache: art.cache(), check: art.check(), iconA: art.icon('a'), iconB: art.icon('b'), iconC: art.icon('c') };
  await Promise.all(Object.entries(list).map(async ([k, s]) => { IMG[k] = await svgImg(s); }));
  DATA.e4 = await (await fetch('data/e4_passes.json')).json();
  DATA.e3 = await (await fetch('data/e3_trace.json')).json();
}
const img = (k, x, y, w, a = 1) => { const i = IMG[k]; const h = w * i.height / i.width; ctx.save(); ctx.globalAlpha *= a; ctx.drawImage(i, x - w / 2, y - h / 2, w, h); ctx.restore(); };

// ---------- recorded demo frames (loaded on demand by OP.frame) ----------
const DEMO = new Map(); let DEMO_SEGS = [];
const demoIdx = (t) => { const s = DEMO_SEGS.find((d) => t >= d.a && t < d.b); return s ? Math.min(3003, Math.max(0, Math.round((s.src + (t - s.a) * s.rate) * 30))) : -1; };
export async function needs(t) {
  const i = demoIdx(t); if (i < 0 || DEMO.has(i)) return;
  const im = new Image(); im.src = `demo/f/${String(i).padStart(5, '0')}.jpg`; await im.decode(); DEMO.set(i, im);
  if (DEMO.size > 90) DEMO.delete(DEMO.keys().next().value);
}

// ---------- shared components ----------
const bg = (c = C.bg) => fillAll(c);
function dots(c = '#DCE2EA') { ctx.fillStyle = c; for (let y = 30; y < H; y += 60) for (let x = 30; x < W; x += 60) { ctx.beginPath(); ctx.arc(x, y, 2.4, 0, TAU); ctx.fill(); } }
function chapter(n, label, lt, dark) {
  const a = eo(seg(lt, 0, 0.4)); ctx.save(); ctx.globalAlpha *= a;
  const w = tw(label, 26, 800) + 96; rr(60, 46, w, 52, 26); ctx.fillStyle = dark ? 'rgba(255,255,255,0.12)' : C.white; ctx.fill();
  if (!dark) { ctx.lineWidth = 3; ctx.strokeStyle = C.ink; ctx.stroke(); }
  circle(86, 72, 17, C.coral); text(String(n), 86, 73, 20, C.white, 900); text(label, 114, 73, 26, dark ? C.white : C.ink, 800, 'left'); ctx.restore();
}
function title(s, lt, y = 170, size = 64, color = C.ink) { const a = eo(seg(lt, 0.05, 0.5)); text(s, W / 2, y + (1 - a) * 24, size, color, 900, 'center', a); }
function pop(lt, t0, d = 0.35) { return lt < t0 ? 0 : eob(seg(lt, t0, t0 + d)); }
function chain(x0, y0, n, dx, amp, r, letters, prog = 1, ph = 0, wig = 0) {
  const pts = []; for (let i = 0; i < n; i++) pts.push([x0 + i * dx, y0 + amp * Math.sin(i * 0.9 + ph) + wig * Math.sin(i * 1.7 + ph * 3)]);
  const k = Math.max(0, Math.min(n, prog * n));
  ctx.save(); ctx.lineWidth = r / 4; ctx.strokeStyle = C.ink; ctx.lineCap = 'round'; ctx.beginPath();
  for (let i = 0; i < Math.ceil(k); i++) i ? ctx.lineTo(...pts[i]) : ctx.moveTo(...pts[i]); ctx.stroke(); ctx.restore();
  for (let i = 0; i < Math.ceil(k); i++) { const s = clamp(k - i); bead(pts[i][0], pts[i][1], r * eob(s), BEADS[i % 6], letters ? letters[i] : ''); }
  return pts;
}
function tile(x, y, s, ch, fill = C.white, a = 1, stroke = C.ink, dashed) {
  ctx.save(); ctx.globalAlpha *= a; rr(x - s / 2 + 5, y - s / 2 + 6, s, s, s * 0.18); ctx.fillStyle = 'rgba(29,36,51,0.15)'; ctx.fill();
  rr(x - s / 2, y - s / 2, s, s, s * 0.18); ctx.fillStyle = fill; ctx.fill(); ctx.setLineDash(dashed ? [8, 6] : []); ctx.lineWidth = 4; ctx.strokeStyle = stroke; ctx.stroke(); ctx.setLineDash([]);
  if (ch) text(ch, x, y + 2, s * 0.52, C.ink, 900); ctx.restore();
}
function bigStat(s, x, y, size, color, lt, t0 = 0, sub, subColor = C.muted) {
  const p = pop(lt, t0, 0.45); if (!p) return; ctx.save(); ctx.translate(x, y); ctx.scale(p, p); text(s, 0, 0, size, color, 900); ctx.restore();
  if (sub) text(sub, x, y + size * 0.72, Math.round(size * 0.3), subColor, 700, 'center', clamp((lt - t0 - 0.2) / 0.4));
}
function hbars(x, y, w, rowH, items, max, prog, unit = '') {
  items.forEach((it, i) => {
    const yy = y + i * rowH, p = eo(clamp(prog * items.length - i * 0.6)), bw = (w - 330) * it.v / max * p;
    text(it.label, x + 300, yy + rowH * 0.36, 30, C.ink, 800, 'right');
    rr(x + 330, yy + 6, Math.max(bw, 2), rowH * 0.62, 12); ctx.fillStyle = it.color; ctx.fill(); ctx.lineWidth = 4; ctx.strokeStyle = C.ink; ctx.stroke();
    if (p > 0.1) text(fmt(it.v * p, it.d ?? 2) + unit, x + 350 + bw, yy + rowH * 0.36, 34, C.ink, 900, 'left', clamp(p * 2 - 0.2));
  });
}
function gridAt(x, y, n, m, c, fillFn, labelsTop, labelsLeft, a = 1) {
  ctx.save(); ctx.globalAlpha *= a; card(x, y, m * c, n * c, C.white, 10, 4);
  for (let i = 0; i < n; i++) for (let j = 0; j < m; j++) { const f = fillFn && fillFn(i, j); if (f) { ctx.fillStyle = f.fill || f; ctx.fillRect(x + j * c + 2, y + i * c + 2, c - 4, c - 4); if (f.txt != null) text(String(f.txt), x + j * c + c / 2, y + i * c + c / 2 + 1, c * 0.38, f.color || C.ink, 800); } }
  ctx.strokeStyle = C.grey; ctx.lineWidth = 2; for (let i = 1; i < n; i++) line([[x, y + i * c], [x + m * c, y + i * c]], C.grey, 2); for (let j = 1; j < m; j++) line([[x + j * c, y], [x + j * c, y + n * c]], C.grey, 2);
  rr(x, y, m * c, n * c, 10); ctx.lineWidth = 4; ctx.strokeStyle = C.ink; ctx.stroke();
  if (labelsTop) [...labelsTop].forEach((s, j) => text(s, x + j * c + c / 2, y - c * 0.45, c * 0.5, C.muted, 900));
  if (labelsLeft) [...labelsLeft].forEach((s, i) => text(s, x - c * 0.45, y + i * c + c / 2, c * 0.5, C.muted, 900));
  ctx.restore();
}
function arrow(x1, y1, x2, y2, color, w = 8) {
  line([[x1, y1], [x2, y2]], color, w); const a = Math.atan2(y2 - y1, x2 - x1), s = w * 2.4;
  ctx.beginPath(); ctx.moveTo(x2 + Math.cos(a) * s * 0.4, y2 + Math.sin(a) * s * 0.4); ctx.lineTo(x2 - Math.cos(a - 0.6) * s, y2 - Math.sin(a - 0.6) * s); ctx.lineTo(x2 - Math.cos(a + 0.6) * s, y2 - Math.sin(a + 0.6) * s); ctx.closePath(); ctx.fillStyle = color; ctx.fill();
}
function chipLabel(s, x, y, fill = C.ink, fg = C.white, size = 24) { const w = tw(s, size, 800) + 36; rr(x - w / 2, y - size * 0.9, w, size * 1.8, size * 0.9); ctx.fillStyle = fill; ctx.fill(); text(s, x, y + 1, size, fg, 800); }
function check(x, y, r, p) { if (p <= 0) return; ctx.save(); ctx.translate(x, y); ctx.scale(eob(p), eob(p)); circle(0, 0, r, C.teal); ctx.lineWidth = 5; ctx.strokeStyle = C.ink; ctx.stroke(); line([[-r * 0.42, 0], [-r * 0.1, r * 0.32], [r * 0.45, -r * 0.35]], C.white, r * 0.22); ctx.restore(); }
function redX(x, y, r, p) { if (p <= 0) return; ctx.save(); ctx.translate(x, y); ctx.scale(eob(p), eob(p)); line([[-r, -r], [r, r]], C.red, r * 0.32); line([[r, -r], [-r, r]], C.red, r * 0.32); ctx.restore(); }

// the 16-lane register
function register(x, y, w, h, lanes, ink = C.ink, fillFn) {
  card(x, y, w, h, ink === C.white ? C.night : C.white, 22, 5, ink);
  const lw = (w - 40) / lanes; for (let i = 0; i < lanes; i++) { const f = fillFn ? fillFn(i) : BEADS[i % 6]; if (!f) continue; rr(x + 20 + i * lw + 4, y + 18, lw - 8, h - 36, 10); ctx.fillStyle = f; ctx.fill(); ctx.lineWidth = 3; ctx.strokeStyle = ink; ctx.stroke(); }
}

// ---------- the film ----------
export function buildShots() {
  const shots = [], cues = [];
  const add = (s) => shots.push(s), cue = (t, type, o = {}) => cues.push({ t, type, ...o });
  const cut = (i) => Math.max(0, L(i) - 0.3);
  const nrm = (s) => String(s).toLowerCase().replace(/[^a-z0-9']+/g, '');
  const wx = (word, li) => { const w = T.words.find((w) => w.line === li && nrm(w.w) === nrm(word)); return w ? w.s : L(li); };
  const at = (i, word) => Wd(word, i) - cut(i);            // a word's time inside the shot that starts at cut(i)
  const sh = (i, note, draw, o = {}) => { add({ id: 'L' + i, at: i === 0 ? 0 : cut(i), note, draw, ...o }); if (i) cue(cut(i), 'whoosh', { gain: 0.35 }); };

  // ===== 0. hook =====
  sh(0, 'a mystery protein wiggles in', (lt) => { bg(); dots(); const ph = lt * 1.6;
    const p = eo(seg(lt, 0.2, 1.6)); chain(560, 520, 11, 82, 44, 30, null, p, ph);
    const q = pop(lt, 1.8); if (q) { ctx.save(); ctx.translate(1440, 380); ctx.scale(q, q); card(-80, -70, 160, 140, C.white, 40); text('?', 0, 4, 96, C.coral, 900); ctx.restore(); }
    text('What does it do?', W / 2, 800, 48, C.muted, 800, 'center', clamp((lt - 2.2) / 0.5)); });
  sh(1, 'relatives appear around it', (lt) => { bg(); dots(); chain(660, 460, 8, 80, 30, 26, null, 1, lt * 1.6);
    const rel = [[300, 220, C.lav], [300, 760, C.teal], [1620, 220, C.orange], [1620, 760, C.blue]];
    rel.forEach(([x, y, c], k) => { const p = eo(seg(lt, 0.4 + k * 0.35, 1.1 + k * 0.35)); if (!p) return; ctx.save(); ctx.globalAlpha = p;
      line([[960, 470], [lerp(960, x, p), lerp(470, y, p)]], C.grey, 5, [10, 10]); for (let i = 0; i < 5; i++) bead(x - 120 + i * 60, y + 18 * Math.sin(i + k), 20, i % 2 ? c : BEADS[(i + k) % 6]); ctx.restore(); });
    title('Find its relatives', lt, 880, 52); });
  sh(2, 'similar shape, similar job', (lt) => { bg(); dots();
    const cols = [C.teal, C.teal, C.coral]; const ys = [300, 520];
    [0, 1].forEach((r) => { const p = eo(seg(lt, r * 0.5, r * 0.5 + 0.7)); ctx.save(); ctx.globalAlpha = p;
      for (let i = 0; i < 7; i++) bead(420 + i * 72, ys[r] + 20 * Math.sin(i * 0.9), 26, BEADS[i % 6]);
      arrow(980, ys[r], 1160, ys[r], C.ink, 7);
      // a gear = "the job"
      ctx.save(); ctx.translate(1290, ys[r]); ctx.rotate(lt * 0.8 * (r ? -1 : 1)); for (let k = 0; k < 8; k++) { ctx.rotate(TAU / 8); rr(-14, -78, 28, 30, 6); ctx.fillStyle = C.teal; ctx.fill(); }
      circle(0, 0, 62, C.teal); ctx.lineWidth = 5; ctx.strokeStyle = C.ink; ctx.stroke(); circle(0, 0, 22, C.bg); ctx.restore(); ctx.restore(); });
    text('Similar proteins usually do similar jobs', W / 2, 760, 46, C.ink, 800, 'center', clamp((lt - 1.4) / 0.5)); });
  sh(3, 'Swiss-Prot counter 0 → 576,073', (lt, d) => { bg(); dots(); img('query', 560, 470, 760, eo(seg(lt, 0, 0.5)));
    const p = eio(seg(lt, 0.6, d - 1.2)); bigStat(fmt(Math.round(576073 * p)), 1380, 420, 120, C.ink, lt, 0.4, 'reviewed proteins in Swiss-Prot');
    text('209 million letters', 1380, 600, 34, C.muted, 700, 'center', clamp((lt - 2.5) / 0.5)); }, { zoom: 0.04 });
  sh(4, 'every one, exactly: a supercomputer?', (lt) => { bg('#E9EDF4');
    for (let k = 0; k < 5; k++) { const p = eo(seg(lt, k * 0.18, k * 0.18 + 0.5)), x = 460 + k * 250, y = 330 + (1 - p) * 300; ctx.save(); ctx.globalAlpha = p;
      card(x - 95, y, 190, 440, C.night, 14, 5); for (let s = 0; s < 9; s++) { rr(x - 70, y + 24 + s * 46, 140, 30, 6); ctx.fillStyle = '#3A4766'; ctx.fill(); circle(x + 50, y + 39 + s * 46, 6, (s + k + Math.floor(lt * 6)) % 3 ? C.teal : C.yellow); } ctx.restore(); }
    title('A job for a supercomputer?', lt, 190, 60); });
  sh(5, 'no: this laptop, about a second', (lt) => { bg(); dots(); img('laptop', 760, 500, 720, eo(seg(lt, 0, 0.5)));
    bigStat('≈ 1 s', 1430, 430, 150, C.teal, lt, Wd('second', 5) - cut(5) - 0.1, 'exact search, all of Swiss-Prot'); });
  sh(6, 'title card', (lt) => { bg(C.ink); dots('#27304A'); const p = eo(seg(lt, 0, 0.6));
    text('HySW', W / 2, 400 - (1 - p) * 30, 160, C.white, 900, 'center', p);
    text('Exact protein search with SIMD and threads, from first principles', W / 2, 540, 40, C.grey, 700, 'center', clamp((lt - 0.4) / 0.5));
    ['Proteins', 'The grid', 'Go wider', 'Go more', 'The limits', 'Live demo'].forEach((s, k) => { const a = clamp((lt - 0.9 - k * 0.18) / 0.4); ctx.save(); ctx.globalAlpha *= a; chipLabel(s, 410 + k * 220, 700, k === 2 || k === 3 ? C.yellow : 'rgba(255,255,255,0.14)', k === 2 || k === 3 ? C.ink : C.white, 26); ctx.restore(); }); }, { zoom: 0.02 });

  // ===== 1. proteins =====
  sh(7, 'beads join into a chain', (lt, d) => { bg(); dots(); chapter(1, 'What is a protein?', lt);
    chain(260, 520, 18, 82, 70, 30, null, eio(seg(lt, 0.3, d - 0.4)), 0.3);
    text('amino acids', 1160, 300, 40, C.coral, 900, 'center', clamp((lt - Wd('amino', 7) + cut(7)) / 0.4)); });
  sh(8, '20 letters', (lt) => { bg(); dots(); chapter(1, 'What is a protein?', lt); const A = 'ACDEFGHIKLMNPQRSTVWY';
    [...A].forEach((ch, k) => { const p = pop(lt, 0.2 + k * 0.09, 0.3); if (!p) return; const x = 380 + (k % 10) * 128, y = 380 + Math.floor(k / 10) * 170; ctx.save(); ctx.translate(x, y); ctx.scale(p, p); bead(0, 0, 50, BEADS[k % 6], ch); ctx.restore(); });
    bigStat('20 kinds', W / 2, 760, 60, C.ink, lt, 2.3); });
  sh(9, 'chain becomes text', (lt) => { bg(); dots(); chapter(1, 'What is a protein?', lt); const s = 'MVHLTPEEKSAVTALWGKVNVDEVGGEALGRLLVVYPWTQRFFESFGDLSTPDAVMGNPKVKAHGKKVLGAFSDGLAHLDNLKGT';
    const p = eo(seg(lt, 0.2, 1.0)); chain(420, 360, 12, 90, 30, 30, s, 1 - p * 0.0, 0.3);
    const n = Math.floor(seg(lt, 0.8, 3.2) * 46); ctx.save(); rr(240, 560, 1440, 120, 18); ctx.fillStyle = C.white; ctx.fill(); ctx.lineWidth = 4; ctx.strokeStyle = C.ink; ctx.stroke();
    ctx.font = '800 50px Inter'; ctx.textAlign = 'left'; ctx.textBaseline = 'middle'; ctx.fillStyle = C.ink; ctx.fillText(s.slice(0, n) + (n < 46 && Math.floor(lt * 3) % 2 ? '|' : ''), 280, 622); ctx.restore(); });
  sh(10, 'hemoglobin begins MVHLTP', (lt) => { bg(); dots(); chapter(1, 'What is a protein?', lt);
    // a red blood cell
    ctx.save(); ctx.translate(380, 450); ctx.rotate(Math.sin(lt) * 0.08); ctx.beginPath(); ctx.ellipse(0, 0, 190, 150, 0, 0, TAU); ctx.fillStyle = C.red; ctx.fill(); ctx.lineWidth = 6; ctx.strokeStyle = C.ink; ctx.stroke(); ctx.beginPath(); ctx.ellipse(0, 0, 100, 70, 0, 0, TAU); ctx.fillStyle = '#B83A3A'; ctx.fill(); ctx.restore();
    text('Hemoglobin β', 380, 700, 44, C.ink, 900);
    const L6 = 'MVHLTP'; [...L6].forEach((ch, k) => { const tt = wx(['M', 'V', 'H', 'L', 'T', 'P'][k], 10) - cut(10); const p = pop(lt, tt - 0.05, 0.3); if (!p) return; ctx.save(); ctx.translate(780 + k * 165, 440); ctx.scale(p, p); bead(0, 0, 62, BEADS[k % 6], ch); ctx.restore(); }); });
  sh(11, '147 letters', (lt, d) => { bg(); dots(); chapter(1, 'What is a protein?', lt); const s = 'MVHLTPEEKSAVTALWGKVNVDEVGGEALGRLLVVYPWTQRFFESFGDLSTPDAVMGNPKVKAHGKKVLGAFSDGLAHLDNLKGTFATLSELHCDKLHVDPENFRLLGNVLVCVLAHHFGKEFTPPVQAAYQKVVAGVANALAHKYH';
    const n = Math.round(147 * eio(seg(lt, 0.2, d - 0.6))); for (let k = 0; k < n; k++) { const x = 160 + (k % 21) * 76, y = 220 + Math.floor(k / 21) * 86; bead(x, y, 30, BEADS[k % 6], s[k]); }
    chipLabel(`${n} / 147 letters`, W / 2, 870, C.ink, C.white, 30); });

  // ===== 2. similarity =====
  const A1 = 'MVHLTPEEK', B1 = 'MVHL-PDEK', V1 = '++++-+~++';
  const alignRows = (lt, shift, verdict, gapP, scoreP) => { const s = 120, x0 = W / 2 - 4 * 150;
    for (let i = 0; i < 9; i++) { tile(x0 + i * 150, 330, s, A1[i]); const isGap = B1[i] === '-';
      if (isGap) { if (gapP > 0) tile(x0 + i * 150, 500, s, '', '#FBE3E3', gapP, C.red, true); }
      else { const sh2 = (i > 4 ? (1 - gapP) * 150 : 0); tile(x0 + i * 150 - sh2 + shift, 500, s, B1[i]); }
      const vp = clamp(verdict * 9 - i); if (vp > 0) { const col = V1[i] === '+' ? C.teal : V1[i] === '~' ? C.yellow : C.red; const r = 34 * eob(vp);
        circle(x0 + i * 150, 660, r, col); ctx.lineWidth = 4; ctx.strokeStyle = C.ink; ctx.stroke(); text(V1[i] === '-' ? '−' : '+', x0 + i * 150, 662, V1[i] === '~' ? 30 : 44, V1[i] === '~' ? C.ink : C.white, 900); } }
    if (scoreP > 0) bigStat(`score ${Math.round(38 * scoreP)}`, W / 2, 800, 56, C.ink, 1, 0); };
  sh(12, 'two proteins, similar?', (lt) => { bg(); dots(); chapter(2, 'Measuring similarity', lt); const x0 = W / 2 - 4 * 150;
    for (let i = 0; i < 9; i++) tile(x0 + i * 150 + (1 - eo(seg(lt, 0, 0.8))) * -300, 330, 120, A1[i]);
    for (let i = 0; i < 8; i++) tile(x0 + i * 150 + 60 + (1 - eo(seg(lt, 0.2, 1))) * 300, 560, 120, 'MVHLPDEK'[i]);
    text('similar?', W / 2, 780, 54, C.coral, 900, 'center', clamp((lt - 1) / 0.4)); });
  sh(13, 'line them up', (lt) => { bg(); dots(); chapter(2, 'Measuring similarity', lt); alignRows(lt, (1 - eo(seg(lt, 0.2, 1.2))) * 60, 0, 0, 0); });
  sh(14, 'matches and similar letters score', (lt) => { bg(); dots(); chapter(2, 'Measuring similarity', lt); alignRows(lt, 0, seg(lt, 0.2, 3.5), 0, 0); });
  sh(15, 'a gap opens', (lt) => { bg(); dots(); chapter(2, 'Measuring similarity', lt); alignRows(lt, 0, 1, eo(seg(lt, at(15, 'gap') - 0.6, at(15, 'gap') + 0.2)), 0); });
  sh(16, 'the score', (lt) => { bg(); dots(); chapter(2, 'Measuring similarity', lt); alignRows(lt, 0, 1, 1, eo(seg(lt, 0.4, 2))); text('(illustrative score)', W / 2, 870, 24, C.muted, 700); });

  // ===== 3. the grid =====
  const GT = 'MVHLTPEE', GL = 'MVHLPDEK', GX = 640, GY = 230, GC = 80;
  const gval = (i, j) => { let v = 0; for (let k = 0; k <= Math.min(i, j); k++) if (GT[j - k] === GL[i - k]) v += 5; else break; return v; };
  sh(17, 'Smith & Waterman 1981', (lt) => { bg(); dots(); chapter(3, 'The grid', lt); const p = eo(seg(lt, 0, 0.6));
    ctx.save(); ctx.translate(W / 2, 470 + (1 - p) * 40); ctx.rotate(-0.03); card(-330, -230, 660, 460, C.white, 16); text('J. Mol. Biol. (1981)', 0, -150, 30, C.muted, 700);
    text('Identification of common', 0, -70, 40, C.ink, 900); text('molecular subsequences', 0, -20, 40, C.ink, 900); text('T. F. Smith  ·  M. S. Waterman', 0, 60, 32, C.coral, 800);
    for (let k = 0; k < 4; k++) rr(-250, 110 + k * 22, 500 - (k % 2) * 120, 10, 5), (ctx.fillStyle = C.grey), ctx.fill(); ctx.restore(); });
  sh(18, 'draw the grid', (lt) => { bg(); dots(); chapter(3, 'The grid', lt); const p = eo(seg(lt, 0.2, 1.4));
    gridAt(GX, GY, Math.max(1, Math.round(8 * p)), Math.max(1, Math.round(8 * p)), GC, null, GT.slice(0, Math.round(8 * p)), GL.slice(0, Math.round(8 * p))); });
  const cellHi = (lt, arrows, zero) => { gridAt(GX, GY, 8, 8, GC, (i, j) => (i === 4 && j === 4 ? { fill: C.yellow, txt: zero ? '0' : '?' } : (arrows && i === 3 && j === 3 ? '#D7F2EC' : arrows > 1 && ((i === 3 && j === 4) || (i === 4 && j === 3)) ? (i === 3 ? '#FDE4D6' : '#E5E0F3') : null)), GT, GL);
    const cx = GX + 4 * GC + GC / 2, cy = GY + 4 * GC + GC / 2; return [cx, cy]; };
  sh(19, 'one cell, one question', (lt) => { bg(); dots(); chapter(3, 'The grid', lt); cellHi(lt, 0);
    const p = pop(lt, 0.8); if (p) { ctx.save(); ctx.translate(1560, 400); ctx.scale(p, p); card(-250, -110, 500, 220, C.white, 24); text('Best score that', 0, -36, 38, C.ink, 900); text('can end HERE?', 0, 20, 38, C.coral, 900); ctx.restore(); } }, { cam: [1000, 560], zoom: 0.05 });
  sh(20, 'the diagonal neighbour', (lt) => { bg(); dots(); chapter(3, 'The grid', lt); const [cx, cy] = cellHi(lt, 1); const p = eo(seg(lt, 0.3, 1));
    if (p) arrow(cx - GC, cy - GC, lerp(cx - GC, cx - 20, p), lerp(cy - GC, cy - 20, p), C.teal, 9); text('+ score for these two letters', 1560, 420, 34, C.teal, 900, 'center', p); });
  sh(21, 'above and left: gaps', (lt) => { bg(); dots(); chapter(3, 'The grid', lt); const [cx, cy] = cellHi(lt, 2); arrow(cx - GC, cy - GC, cx - 20, cy - 20, C.teal, 9);
    const p = eo(seg(lt, 0.2, 0.9)), q = eo(seg(lt, 0.8, 1.5)); if (p) arrow(cx, cy - GC, cx, lerp(cy - GC, cy - 26, p), C.coral, 9); if (q) arrow(cx - GC, cy, lerp(cx - GC, cx - 26, q), cy, C.lav, 9);
    text('− gap penalty', 1560, 420, 38, C.coral, 900, 'center', p); });
  sh(22, 'or zero', (lt) => { bg(); dots(); chapter(3, 'The grid', lt); cellHi(lt, 2, lt > 1); bigStat('0', 1560, 420, 140, C.ink, lt, 1, 'start fresh here'); });
  sh(23, 'fill every cell, find the max', (lt, d) => { bg(); dots(); chapter(3, 'The grid', lt); const p = seg(lt, 0.2, d - 1.6) * 16;
    gridAt(GX, GY, 8, 8, GC, (i, j) => { if (i + j > p) return null; const v = gval(i, j); return { fill: v >= 20 && lt > d - 1.4 ? C.yellow : v ? '#D7F2EC' : C.soft, txt: v }; }, GT, GL);
    if (lt > d - 1.4) text('biggest number = the answer', 1590, 420, 34, C.ink, 900, 'center', clamp((lt - d + 1.4) / 0.4)); }, { cam: [960, 560] });

  // ===== 4. the cost =====
  sh(24, 'query × all of Swiss-Prot', (lt) => { bg(); dots(); chapter(4, 'The cost', lt); const p = eo(seg(lt, 0.2, 1.4));
    bead(380, 500, 40, C.coral); text('147', 380, 600, 44, C.ink, 900); text('letters', 380, 650, 28, C.muted, 700); text('×', 620, 500, 90, C.ink, 900, 'center', p);
    ctx.save(); ctx.globalAlpha = p; img('query', 1180, 480, 760); ctx.restore(); text('209 million letters', 1300, 800, 34, C.muted, 700, 'center', p); });
  sh(25, '30.8 billion cells', (lt) => { bg('#EEF2F7'); chapter(4, 'The cost', lt); const z = lerp(3, 1, eio(seg(lt, 0, 2.2)));
    ctx.save(); ctx.translate(W / 2, 470); ctx.scale(z, z); for (let r = -14; r <= 14; r++) for (let c = -26; c <= 26; c++) { ctx.fillStyle = (r * 53 + c) % 7 ? '#DCE3EC' : C.yellow; ctx.fillRect(c * 34 - 14, r * 34 - 14, 28, 28); } ctx.restore();
    card(560, 330, 800, 260, C.white, 26); bigStat('30.8 billion', W / 2, 430, 110, C.ink, lt, 0.6); text('cells for one search', W / 2, 525, 34, C.muted, 700, 'center', clamp((lt - 1) / 0.4)); });
  sh(26, 'one plain core: 0.71 GCUPS', (lt) => { bg(); dots(); chapter(4, 'The cost', lt); img('snail', 560, 500, 420, eo(seg(lt, 0, 0.5)));
    bigStat('0.71', 1260, 400, 160, C.coral, lt, 0.5); text('billion cells per second', 1260, 520, 38, C.muted, 700, 'center', clamp((lt - 0.8) / 0.4)); text('plain C, one core (measured)', 1260, 580, 30, C.muted, 700, 'center', clamp((lt - 1.2) / 0.4)); });
  sh(27, '43 seconds', (lt, d) => { bg(); dots(); chapter(4, 'The cost', lt); const p = seg(lt, 0.2, d - 0.3);
    ctx.save(); ctx.translate(700, 470); circle(0, 0, 230, C.white); ctx.lineWidth = 10; ctx.strokeStyle = C.ink; ctx.stroke(); rr(-30, -290, 60, 50, 10); ctx.fillStyle = C.ink; ctx.fill();
    ctx.beginPath(); ctx.moveTo(0, 0); ctx.arc(0, 0, 200, -Math.PI / 2, -Math.PI / 2 + TAU * p * 0.72); ctx.closePath(); ctx.fillStyle = 'rgba(228,87,46,0.25)'; ctx.fill();
    const a = -Math.PI / 2 + TAU * p * 0.72; line([[0, 0], [Math.cos(a) * 180, Math.sin(a) * 180]], C.coral, 10); circle(0, 0, 16, C.ink); ctx.restore();
    bigStat(`${Math.round(43 * p)} s`, 1380, 400, 150, C.ink, lt, 0.1, 'per search'); text('estimate from the measured rate', 1380, 600, 28, C.muted, 700, 'center', clamp((lt - 0.5) / 0.4)); });
  sh(28, 'BLAST skips most of the grid', (lt) => { bg(); dots(); chapter(4, 'The cost', lt); const p = seg(lt, 0.3, 2.2);
    gridAt(560, 200, 12, 20, 40, (i, j) => (Math.abs(i - j * 0.6) < 1.6 && j / 20 < p ? '#D7F2EC' : C.soft)); text('BLAST: check a narrow band, skip the rest', W / 2, 760, 38, C.ink, 800, 'center', clamp((lt - 0.6) / 0.4)); });
  sh(29, 'can miss distant relatives; we skip nothing', (lt) => { bg(); dots(); chapter(4, 'The cost', lt);
    gridAt(560, 200, 12, 20, 40, (i, j) => (Math.abs(i - j * 0.6) < 1.6 ? '#D7F2EC' : (Math.abs(i - (j - 9) * 0.9 - 2) < 1.2 && j > 9 ? (lt > 1 ? '#FDE4D6' : C.soft) : C.soft)));
    redX(1280, 420, 40, seg(lt, 1.2, 1.6)); text('a distant relative, missed', 1280, 520, 30, C.red, 800, 'center', clamp((lt - 1.4) / 0.4));
    chipLabel('HySW: every cell, no skipping', W / 2, 800, C.teal, C.white, 32); }, { zoom: 0.02 });

  // ===== 5. the insight =====
  sh(30, 'protein 1 tells you nothing about protein 2', (lt) => { bg(); dots(); chapter(5, 'The key insight', lt);
    [0, 1].forEach((k) => { const x = 560 + k * 800, p = eo(seg(lt, k * 0.5, k * 0.5 + 0.6)); ctx.save(); ctx.globalAlpha = p; gridAt(x - 160, 300, 6, 8, 40, (i, j) => (i + j < lt * 4 ? '#D7F2EC' : null)); text(`vs protein ${k + 1}`, x, 600, 38, C.ink, 900); ctx.restore(); });
    const q = seg(lt, 2.2, 2.8); if (q) { line([[760, 420], [1160, 420]], C.grey, 8, [16, 12]); redX(960, 420, 36, q); text('no dependence', 960, 330, 32, C.red, 900, 'center', q); } });
  sh(31, 'all comparisons in parallel', (lt) => { bg(); dots(); chapter(5, 'The key insight', lt);
    for (let r = 0; r < 3; r++) for (let c = 0; c < 8; c++) { const x = 230 + c * 200, y = 210 + r * 190, k = r * 8 + c, p = eo(seg(lt, k * 0.04, k * 0.04 + 0.4)); ctx.save(); ctx.globalAlpha = p;
      gridAt(x, y, 4, 5, 26, (i, j) => (i + j < (lt - 0.8) * 3 + rnd(k) * 2 ? '#D7F2EC' : null)); for (let b = 0; b < 4; b++) circle(x + 14 + b * 30, y - 18, 9, BEADS[(k + b) % 6]); ctx.restore(); }
    text('independent work = parallel work', W / 2, 850, 40, C.ink, 900, 'center', clamp((lt - 2) / 0.5)); });
  sh(32, 'wider and more', (lt) => { bg(C.ink); dots('#27304A'); chapter(5, 'The key insight', lt, true); title('Two ways to go faster', lt, 200, 60, C.white);
    const p = pop(lt, at(32, 'wider') - 0.1), q = pop(lt, at(32, 'more') - 0.1);
    if (p) { ctx.save(); ctx.translate(580, 470); ctx.scale(p, p); register(-330, -60, 660, 120, 16, C.white); text('Go wider: SIMD', 0, 150, 48, C.yellow, 900); ctx.restore(); }
    if (q) { ctx.save(); ctx.translate(1360, 470); ctx.scale(q, q); for (let k = 0; k < 4; k++) card(-200 + k * 26, -150 + k * 34, 320, 110, k % 2 ? C.orange : C.blue, 16, 4, C.white); text('Go more: threads', 30, 150, 48, C.yellow, 900); ctx.restore(); } });

  // ===== 6. SIMD =====
  sh(33, '256-bit register', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); const p = eo(seg(lt, 0.2, 1.4));
    card(160, 380, 1600 * p, 130, C.white, 22); for (let b = 0; b < 64 * p; b++) line([[180 + b * 24.7, 520], [180 + b * 24.7, b % 4 ? 532 : 545]], C.muted, 3);
    text('256 bits', W / 2, 620, 46, C.ink, 900, 'center', clamp((lt - 1.4) / 0.4)); text('one vector register', W / 2, 300, 38, C.muted, 700, 'center', p); });
  sh(34, '16 lanes of 16 bits', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); const p = seg(lt, 0.3, 2.2);
    register(160, 380, 1600, 130, 16, C.ink, (i) => (i < p * 16 ? BEADS[i % 6] : null)); text('16 scores × 16 bits', W / 2, 620, 46, C.ink, 900, 'center', clamp((lt - 1.6) / 0.4)); });
  sh(35, 'a different protein per lane', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); register(160, 600, 1600, 130, 16);
    for (let i = 0; i < 16; i++) { const p = eo(seg(lt, 0.2 + i * 0.12, 0.7 + i * 0.12)), x = 160 + 20 + (i + 0.5) * 97.5; for (let k = 0; k < 4; k++) bead(x + 6 * Math.sin(k + i), lerp(-80, 220 + k * 72, p), 22, BEADS[i % 6]); } });
  sh(36, 'one add moves 16 grids', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); const s = (lt * 1.6) % 1;
    chipLabel('ADD', 160, 160, C.coral, C.white, 34); for (let i = 0; i < 16; i++) { const x = 190 + i * 98; gridAt(x, 300, 6, 1, 70, (r) => (r < Math.floor(lt * 1.6) % 7 ? BEADS[i % 6] : null)); }
    line([[150, 280 + s * 440], [1770, 280 + s * 440]], C.coral, 5, [14, 10]); text('one instruction → 16 cells', W / 2, 820, 42, C.ink, 900, 'center', clamp((lt - 0.6) / 0.4)); }, { zoom: 0.02 });
  const lens0 = [0.9, 0.35, 0.7, 0.5, 1, 0.3, 0.8, 0.45, 0.6, 0.95, 0.4, 0.75, 0.55, 0.85, 0.65, 0.5];
  const lanesView = (lt, sortP, fillP, label) => { const lens = lens0.map((v, i) => lerp(v, 0.94 + 0.06 * rnd(i), eio(sortP)));
    lens.forEach((v, i) => { const y = 200 + i * 40, W1 = 1200; rr(360, y, W1, 28, 8); ctx.fillStyle = C.soft; ctx.fill();
      if (sortP < 0.5) { ctx.save(); ctx.clip(); for (let k = -20; k < 80; k++) line([[360 + k * 22, y + 30], [380 + k * 22, y - 2]], '#D6DCE5', 5); ctx.restore(); }
      rr(360, y, W1 * v * fillP, 28, 8); ctx.fillStyle = BEADS[i % 6]; ctx.fill(); ctx.lineWidth = 2.5; ctx.strokeStyle = C.ink; ctx.stroke(); text(`lane ${i + 1}`, 330, y + 14, 20, C.muted, 700, 'right'); });
    if (label) text(label, W / 2, 870, 40, C.ink, 900); };
  sh(37, 'lockstep: wait for the longest', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); lanesView(lt, 0, eo(seg(lt, 0.2, 2)), lt > 2.5 ? 'hatched = lanes waiting for the longest protein' : ''); });
  sh(38, 'sort by length', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); lanesView(lt, seg(lt, 1.2, 3.6), 1, 'sorted by length: a batch = proteins of nearly equal length'); });
  sh(39, '0.19% wasted', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); bigStat('0.19%', W / 2, 400, 200, C.teal, lt, 0.2, 'of lane work wasted on padding, all of Swiss-Prot'); });
  sh(40, '0.71 → 9.00 GCUPS', (lt, d) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt);
    hbars(260, 300, 1400, 170, [{ label: 'Plain C', v: 0.71, color: C.grey }, { label: 'SIMD, 16 lanes', v: 9.0, color: C.teal }], 10, seg(lt, 0.3, 2.6)); text('billion cells per second · one core · 400-letter query · median of 5 runs', W / 2, 760, 28, C.muted, 700); });
  sh(41, '12.7x', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); bigStat('12.7×', W / 2, 430, 240, C.teal, lt, 0.05, 'faster on one core (measured)'); });
  sh(42, 'Amdahl: some work does not get wider', (lt) => { bg(); dots(); chapter(6, 'Go wider: SIMD', lt); const p = eio(seg(lt, 1, 3.4));
    const fixed = 160, vec = lerp(1200, 1200 / 16, p); rr(300, 300, fixed, 120, 14); ctx.fillStyle = C.coral; ctx.fill(); ctx.lineWidth = 4; ctx.strokeStyle = C.ink; ctx.stroke();
    rr(300 + fixed + 10, 300, vec, 120, 14); ctx.fillStyle = C.teal; ctx.fill(); ctx.stroke();
    text('loading data, batch set-up', 380, 480, 28, C.coral, 800, 'left'); text('the cell work: 16× narrower', 300 + fixed + 10, 260, 28, C.teal, 800, 'left');
    text("Amdahl's law: the part that can't go wider limits the speedup", W / 2, 700, 38, C.ink, 900, 'center', clamp((lt - 3) / 0.5)); });

  // ===== 7. threads =====
  sh(43, 'the chip', (lt) => { bg(); dots(); chapter(7, 'Go more: threads', lt); img('chip', W / 2, 480, 760, eo(seg(lt, 0, 0.6))); });
  sh(44, 'two P-cores, eight E-cores', (lt) => { bg(); dots(); chapter(7, 'Go more: threads', lt); img('chip', 640, 480, 720);
    const p = pop(lt, at(44, 'performance') - 0.1), q = pop(lt, at(44, 'efficiency') - 0.1);
    if (p) { ctx.save(); ctx.translate(1420, 360); ctx.scale(p, p); circle(-200, 0, 22, C.blue); text('2 performance cores', -160, 0, 38, C.ink, 900, 'left'); ctx.restore(); }
    if (q) { ctx.save(); ctx.translate(1420, 520); ctx.scale(q, q); circle(-200, 0, 22, C.orange); text('8 efficiency cores', -160, 0, 38, C.ink, 900, 'left'); text('each well under half as fast', -160, 50, 28, C.muted, 700, 'left'); ctx.restore(); } });
  const trace = (lt, key, prog, label) => { const tr = DATA.e3[key], span = 4.8, X = 330, Wd2 = 1300; text(label, 160, 210, 34, key === 'static' ? C.red : C.teal, 900, 'left');
    tr.threads.forEach((th, i) => { const y = 260 + i * 50, e = th.end / span * Wd2; text(th.type === 'P' ? 'P-core' : 'E-core', X - 20, y + 18, 22, C.muted, 800, 'right');
      rr(X, y, Wd2 * 4.72 / span * prog, 34, 8); ctx.fillStyle = '#F1F3F6'; ctx.fill();
      rr(X, y, Math.min(e, Wd2 * 4.72 / span * prog), 34, 8); ctx.fillStyle = th.type === 'P' ? C.blue : C.orange; ctx.fill(); });
    const endX = X + tr.seconds / span * Wd2; if (prog * 4.72 >= tr.seconds) { line([[endX, 245], [endX, 770]], key === 'static' ? C.red : C.teal, 4, [10, 8]); text(`${tr.seconds.toFixed(2)} s`, endX, 800, 30, key === 'static' ? C.red : C.teal, 900); }
    text('real per-core trace, 400-letter query, 10 threads', W / 2, 870, 26, C.muted, 700); };
  sh(45, 'static: equal shares', (lt, d) => { bg(); dots(); chapter(7, 'Go more: threads', lt); trace(lt, 'static', eio(seg(lt, 0.3, d - 0.6)), 'Equal shares, handed out up front'); });
  sh(46, '+31%', (lt) => { bg(); dots(); chapter(7, 'Go more: threads', lt); trace(lt, 'static', 1, 'Equal shares, handed out up front'); text('← this share holds the longest proteins', 1240, 280, 26, C.blue, 900, 'left', clamp((lt - 1) / 0.4)); const p = pop(lt, 0.3);
    if (p) { ctx.save(); ctx.translate(1580, 400); ctx.rotate(-0.08); ctx.scale(p, p); card(-170, -80, 340, 160, C.red, 18, 5); text('+31%', 0, 0, 84, C.white, 900); ctx.restore(); } });
  sh(47, 'dynamic: grab the next batch', (lt, d) => { bg(); dots(); chapter(7, 'Go more: threads', lt); trace(lt, 'dynamic', eio(seg(lt, 0.3, d + 2.5)), 'Next batch to whichever core is free'); });
  sh(48, 'all finish together', (lt) => { bg(); dots(); chapter(7, 'Go more: threads', lt); trace(lt, 'dynamic', 1, 'Next batch to whichever core is free'); check(1640, 420, 60, seg(lt, 0.3, 0.7)); });
  sh(49, 'six cleverer schemes tie', (lt) => { bg(); dots(); chapter(7, 'Go more: threads', lt);
    ['dynamic(1)', 'dynamic(chunk)', 'steal', 'cost + steal', 'costcap + steal', 'guided'].forEach((s, k) => { const p = pop(lt, 0.2 + k * 0.25); if (!p) return; const x = 380 + (k % 3) * 580, y = 330 + Math.floor(k / 3) * 230;
      ctx.save(); ctx.translate(x, y); ctx.scale(p, p); card(-240, -80, 480, 160, k === 0 ? C.yellow : C.white, 20); text(s, 0, -18, 36, C.ink, 900); text(k === 0 ? 'the simple rule' : k === 5 ? '+48% (worse)' : 'tie (within 1%)', 0, 34, 26, k === 5 ? C.red : C.muted, 800); ctx.restore(); }); });

  // ===== 8. limits =====
  sh(50, '10 cores, only 2.5x', (lt) => { bg(); dots(); chapter(8, 'The limits', lt); const p = eo(seg(lt, 0.3, 1.2));
    rr(200, 300, 1520, 110, 18); ctx.setLineDash([16, 12]); ctx.lineWidth = 5; ctx.strokeStyle = C.ink; ctx.stroke(); ctx.setLineDash([]); text('ideal: 10×', 260, 355, 36, C.muted, 900, 'left');
    rr(200, 480, 1520 * 0.252 * p, 110, 18); ctx.fillStyle = C.teal; ctx.fill(); ctx.lineWidth = 5; ctx.strokeStyle = C.ink; ctx.stroke(); text('measured: 2.52×', 620, 535, 40, C.teal, 900, 'left', p);
    bigStat('Why?', W / 2, 760, 90, C.coral, lt, at(50, 'why') - 0.1); });
  sh(51, 'hyperthreads share a vector unit', (lt) => { bg(); dots(); chapter(8, 'The limits', lt); img('ht', 600, 480, 560, eo(seg(lt, 0, 0.5)));
    bigStat('23.7', 1300, 360, 110, C.ink, lt, 1.5, '10 threads, one per core'); bigStat('23.3', 1300, 640, 110, C.ink, lt, 2.4, '12 threads, with hyperthreads'); });
  sh(52, '15 W limit', (lt) => { bg(); dots(); chapter(8, 'The limits', lt); img('power', 700, 480, 360, eo(seg(lt, 0, 0.5))); bigStat('15 W', 1250, 470, 200, C.coral, lt, at(52, 'fifteen') - 0.1, 'long-term power limit'); });
  const e4chart = (lt, prog, showDrop, dim = 1) => { ctx.save(); ctx.globalAlpha *= dim; const X = 300, Y = 220, Wc = 1400, Hc = 520, tmax = 125, gmax = 36; card(X - 40, Y - 30, Wc + 80, Hc + 110, C.white, 20, 4);
    for (const g of [10, 20, 30]) { const y = Y + Hc - g / gmax * Hc; line([[X, y], [X + Wc, y]], '#E3E8EF', 2); text(String(g), X - 14, y, 22, C.muted, 700, 'right'); }
    for (const s of [0, 30, 60, 90, 120]) text(`${s} s`, X + s / tmax * Wc, Y + Hc + 30, 22, C.muted, 700);
    const pts = DATA.e4.filter((p) => p[0] <= prog * tmax).map(([t, g]) => [X + t / tmax * Wc, Y + Hc - g / gmax * Hc]); if (pts.length > 1) line(pts, C.coral, 6);
    if (pts.length) circle(...pts[pts.length - 1], 9, C.coral);
    text('billion cells per second, 10 cores, from idle (measured, E4)', X + Wc / 2, Y + 20, 26, C.muted, 700);
    if (showDrop) { const x17 = X + 17 / tmax * Wc; line([[x17, Y + 40], [x17, Y + Hc]], C.ink, 3, [8, 8]); text('≈ 17 s', x17 + 12, Y + 70, 26, C.ink, 900, 'left');
      const p = pop(lt, 0.6); if (p) { ctx.save(); ctx.translate(1380, 610); ctx.scale(p, p); card(-150, -70, 300, 140, C.coral, 18, 5); text('−41%', 0, 0, 76, C.white, 900); ctx.restore(); } } ctx.restore(); };
  sh(53, 'flat out first', (lt, d) => { bg(); dots(); chapter(8, 'The limits', lt); e4chart(lt, lerp(0, 0.16, eio(seg(lt, 0.2, d))), false); });
  sh(54, 'then the limit: -41%', (lt, d) => { bg(); dots(); chapter(8, 'The limits', lt); e4chart(lt, lerp(0.16, 1, eio(seg(lt, 0, d - 1.5))), lt > d - 2); });
  sh(55, 'speed depends on run time', (lt) => { bg(); dots(); chapter(8, 'The limits', lt); const p = eo(seg(lt, 0.4, 1)); e4chart(lt, 1, true, 1 - 0.75 * p);
    ctx.save(); ctx.globalAlpha = p; card(330, 560, 1260, 150, C.yellow, 22); text('On a laptop, speed depends on how long', W / 2, 610, 38, C.ink, 900); text('the code has been running', W / 2, 660, 38, C.ink, 900); ctx.restore(); });

  // ===== 9. cache =====
  sh(56, 'long queries 2x slower per cell', (lt) => { bg(); dots(); chapter(9, 'A cache surprise', lt);
    hbars(260, 330, 1400, 170, [{ label: 'short query', v: 1, color: C.teal, d: 0 }, { label: 'long query', v: 2, color: C.coral, d: 0 }], 2.4, seg(lt, 0.4, 2.4), '× time per cell'); text('relative time per cell on 10 threads, before the fix', W / 2, 760, 28, C.muted, 700); });
  sh(57, 'L1 overflows into shared L2', (lt) => { bg(); dots(); chapter(9, 'A cache surprise', lt); ctx.save(); ctx.beginPath(); ctx.rect(0, 0, W * 0.62 * eo(seg(lt, 0, 1.4)) + 1, H); ctx.clip(); img('cache', 960, 470, 1500); ctx.restore(); });
  sh(58, 'tile 16 columns at a time', (lt) => { bg(); dots(); chapter(9, 'A cache surprise', lt); img('cache', 960, 470, 1500);
    const p = (lt * 0.45) % 1; ctx.save(); ctx.globalAlpha = 0.45; rr(1378 + p * 220, 269, 77, 419, 6); ctx.fillStyle = C.coral; ctx.fill(); ctx.restore();
    text('working set stays in L1', 1520, 760, 30, C.ink, 900, 'center', clamp((lt - 1) / 0.5)); });
  sh(59, '1.54x', (lt) => { bg(); dots(); chapter(9, 'A cache surprise', lt); bigStat('1.54×', W / 2, 400, 220, C.teal, lt, 0.2, 'faster on 990-letter queries, 10 threads (measured)');
    text('cost: up to 27% slower for short queries on one thread', W / 2, 720, 30, C.muted, 700, 'center', clamp((lt - 1.5) / 0.5)); });

  // ===== 10. together + correct =====
  sh(60, 'stack the speedups', (lt, d) => { bg(); dots(); chapter(10, 'All together', lt);
    hbars(200, 260, 1500, 150, [{ label: 'Plain C, 1 core', v: 0.71, color: C.grey }, { label: 'SIMD, 1 core', v: 9.0, color: C.teal }, { label: 'SIMD + 10 cores', v: 22.6, color: C.blue }], 25, seg(lt, 0.2, d)); text('billion cells per second, sustained load, 400-letter query', W / 2, 790, 28, C.muted, 700); });
  sh(61, '≈ 32x', (lt) => { bg(); dots(); chapter(10, 'All together', lt); text('12.7×  ×  2.52×', W / 2, 330, 80, C.ink, 900, 'center', eo(seg(lt, 0, 0.4))); bigStat('= 31.9×', W / 2, 520, 180, C.teal, lt, 0.5, 'faster than plain C on one core'); });
  sh(62, 'checked against Parasail', (lt) => { bg(); dots(); chapter(10, 'All together', lt); img('check', W / 2, 470, 1100, eo(seg(lt, 0, 0.6))); text('scores in the drawing are illustrative', W / 2, 820, 24, C.muted, 700); });
  sh(63, '1,152,146 / 1,152,146', (lt, d) => { bg(); dots(); chapter(10, 'All together', lt); const p = eio(seg(lt, 0.2, d - 1.2));
    bigStat(fmt(Math.round(1152146 * p)), W / 2, 380, 150, C.teal, lt, 0.1, 'of 1,152,146 scores identical to Parasail'); check(W / 2, 680, 70, seg(lt, d - 1.2, d - 0.8)); });

  // ===== 11. the demo: real recording =====
  const segs = [[64, 0.0, 0.5], [65, 3.0, 0.78], [66, 5.6, 0.155], [68, 7.2, 1.0], [69, 14.6, 1.0], [70, 49.4, 1.0], [71, 66.0, 1.0]];
  DEMO_SEGS = segs.map(([i, src, rate], k) => ({ a: cut(i), b: k < segs.length - 1 ? cut(segs[k + 1][0]) : cut(72), src, rate }));
  const demo = (i, note, chip) => sh(i, note, (lt, d, t) => { bg('#E4E9F1'); const idx = demoIdx(t), im = DEMO.get(idx) || [...DEMO.values()].at(-1);
    const X = 160, Y = 70, Wv = 1600, Hv = 900; ctx.save(); rr(X - 12 + 8, Y - 56 + 10, Wv + 24, Hv + 68, 22); ctx.fillStyle = 'rgba(29,36,51,0.2)'; ctx.fill(); rr(X - 12, Y - 56, Wv + 24, Hv + 68, 22); ctx.fillStyle = C.ink; ctx.fill();
    [C.coral, C.yellow, C.teal].forEach((c, k) => circle(X + 18 + k * 30, Y - 28, 9, c)); rr(X + 140, Y - 44, 420, 32, 16); ctx.fillStyle = '#3A4766'; ctx.fill(); text('127.0.0.1:8372  ·  HySW', X + 160, Y - 27, 20, C.white, 700, 'left');
    if (im) ctx.drawImage(im, X, Y, Wv, Hv); else { ctx.fillStyle = C.white; ctx.fillRect(X, Y, Wv, Hv); } ctx.restore();
    chipLabel('REAL RECORDING', X + Wv - 120, Y - 28, C.coral, C.white, 18); if (chip) chipLabel(chip, X + Wv - 420, Y - 28, C.yellow, C.ink, 18); }, { zoom: 0, hard: i !== 64 });
  demo(64, 'app idle', 'on AC power');
  demo(65, 'pick hemoglobin, search');
  demo(66, 'cores light up (slowed ~6x)', 'slowed down 6×');
  demo(67, 'cores (slowed)', 'slowed down 6×');
  demo(68, 'results: 1.26 s, ranked hits', 'search took 1.26 s');
  demo(69, 'alignment');
  demo(70, 'AlphaFold 3D structure, hover highlights');
  demo(71, 'Performance Lab');
  // make 66 & 67 one continuous segment (same mapping)
  DEMO_SEGS = DEMO_SEGS.map((s) => s);

  // ===== 12. lessons + close =====
  const lesson = (k, lt) => { const L3 = [['iconA', 'Find the independent work', 'it is what makes parallelism possible'], ['iconB', 'Hand out work as cores free up', 'not in equal shares'], ['iconC', 'Power is a resource', 'measure it like one']];
    L3.forEach(([ic, h, s], j) => { if (j > k) return; const p = j === k ? eo(seg(lt, 0, 0.5)) : 1, x = 330 + j * 630; ctx.save(); ctx.globalAlpha = p; card(x - 270, 260 - (1 - p) * 40, 540, 520, C.night, 26, 0);
      img(ic, x, 390, 150); text(h, x, 540, 34, C.white, 900); text(s, x, 600, 26, C.grey, 700); ctx.restore(); }); };
  sh(72, 'lesson 1', (lt) => { bg(C.ink); dots('#27304A'); chapter(12, 'Three lessons', lt, true); lesson(0, lt); }, { zoom: 0.01 });
  sh(73, 'lesson 2', (lt) => { bg(C.ink); dots('#27304A'); chapter(12, 'Three lessons', lt, true); lesson(1, lt); }, { zoom: 0.01, hard: true });
  sh(74, 'lesson 3', (lt) => { bg(C.ink); dots('#27304A'); chapter(12, 'Three lessons', lt, true); lesson(2, lt); }, { zoom: 0.01, hard: true });
  sh(75, 'exact search, about a second', (lt) => { bg(C.ink); dots('#27304A'); img('laptop', 620, 480, 640, eo(seg(lt, 0, 0.6))); bigStat('≈ 1 s', 1350, 420, 170, C.yellow, lt, 0.5, 'exact search over 576,073 proteins', C.grey); });
  sh(76, 'end card', (lt) => { bg(C.ink); dots('#27304A'); text('HySW', W / 2, 330, 130, C.white, 900, 'center', eo(seg(lt, 0, 0.5)));
    chipLabel('github.com/TARUN-2305/hysw', W / 2, 520, C.yellow, C.ink, 40); text('code · tests · every raw measurement', W / 2, 640, 34, C.grey, 700, 'center', clamp((lt - 0.6) / 0.5));
    text('Voice: Kokoro (local) · 3D model: AlphaFold DB (CC BY 4.0) · data: UniProtKB/Swiss-Prot', W / 2, 800, 22, C.grey, 700, 'center', clamp((lt - 1) / 0.5)); }, { noCaption: false });

  // soft section stings
  [7, 12, 17, 24, 30, 33, 43, 50, 56, 60, 64, 72].forEach((i) => cue(cut(i) + 0.05, 'pop', { gain: 0.4 }));
  return { shots, cues };
}
