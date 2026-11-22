// HySW explainer engine (adapted from the Faceless Shorts "Inside Out" engine to 16:9 long form).
// Canvas, palette (the deck's palette), math and drawing primitives. Every function draws on the shared `ctx`.
export const W = 1920, H = 1080, FPS = 30;
export let ctx = null;
export function setCtx(c) { ctx = c; }

export const C = {
  ink: '#1D2433', muted: '#5B6475', soft: '#EEF2F7', bg: '#F7F8FB', white: '#FFFFFF', grey: '#C9D1DC', night: '#2B3550',
  coral: '#E4572E', teal: '#17A398', yellow: '#FFC857', lav: '#8E7CC3', blue: '#3B6FD8', orange: '#F28C28', red: '#D64545',
  green: '#2E9E5B',
};
export const BEADS = [C.coral, C.teal, C.yellow, C.lav, C.blue, C.orange];

export const clamp = (v, a = 0, b = 1) => Math.max(a, Math.min(b, v));
export const lerp = (a, b, t) => a + (b - a) * t;
export const seg = (t, a, b) => clamp((t - a) / ((b - a) || 1e-6));
export const eo = (t) => 1 - Math.pow(1 - t, 3);
export const eio = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
export const eob = (t) => { const c1 = 1.70158, c3 = c1 + 1; return 1 + c3 * Math.pow(t - 1, 3) + c1 * Math.pow(t - 1, 2); };
export const rnd = (i) => { const x = Math.sin(i * 127.1 + 311.7) * 43758.5453; return x - Math.floor(x); };
export const TAU = Math.PI * 2;

export function rr(x, y, w, h, r) { ctx.beginPath(); ctx.roundRect(x, y, w, h, r); }
export function circle(x, y, r, fill) { ctx.beginPath(); ctx.arc(x, y, r, 0, TAU); ctx.fillStyle = fill; ctx.fill(); }
export function line(pts, color, w, dash) {
  ctx.beginPath(); ctx.moveTo(pts[0][0], pts[0][1]);
  for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0], pts[i][1]);
  ctx.setLineDash(dash || []); ctx.strokeStyle = color; ctx.lineWidth = w; ctx.lineCap = 'round'; ctx.lineJoin = 'round'; ctx.stroke(); ctx.setLineDash([]);
}
export function fillAll(color) { ctx.fillStyle = color; ctx.fillRect(-400, -400, W + 800, H + 800); }
/** text, baseline middle. weight 700/800/900 */
export function text(s, x, y, size, color = C.ink, weight = 800, align = 'center', alpha = 1) {
  ctx.save(); ctx.globalAlpha *= alpha; ctx.font = `${weight} ${size}px Inter`; ctx.textAlign = align; ctx.textBaseline = 'middle';
  ctx.fillStyle = color; ctx.fillText(s, x, y); ctx.restore();
}
export const tw = (s, size, weight = 800) => { ctx.font = `${weight} ${size}px Inter`; return ctx.measureText(s).width; };
/** a sticker card: rounded box, ink outline, hard offset shadow (the deck's look) */
export function card(x, y, w, h, fill = C.white, r = 22, lw = 5, stroke = C.ink) {
  ctx.save(); rr(x + 8, y + 10, w, h, r); ctx.fillStyle = 'rgba(29,36,51,0.16)'; ctx.fill();
  rr(x, y, w, h, r); ctx.fillStyle = fill; ctx.fill(); if (lw) { ctx.lineWidth = lw; ctx.strokeStyle = stroke; ctx.stroke(); } ctx.restore();
}
export function bead(x, y, r, fill, letter, stroke = C.ink) {
  ctx.save(); ctx.beginPath(); ctx.arc(x + r * 0.12, y + r * 0.16, r, 0, TAU); ctx.fillStyle = 'rgba(29,36,51,0.16)'; ctx.fill();
  ctx.beginPath(); ctx.arc(x, y, r, 0, TAU); ctx.fillStyle = fill; ctx.fill(); ctx.lineWidth = Math.max(2, r / 9); ctx.strokeStyle = stroke; ctx.stroke();
  ctx.beginPath(); ctx.ellipse(x - r * 0.35, y - r * 0.4, r * 0.28, r * 0.16, -0.5, 0, TAU); ctx.fillStyle = 'rgba(255,255,255,0.45)'; ctx.fill();
  if (letter) text(letter, x, y + r * 0.04, r * 1.05, fill === C.yellow ? C.ink : C.white, 900);
  ctx.restore();
}
export const fmt = (v, d = 0) => v.toLocaleString('en-US', { minimumFractionDigits: d, maximumFractionDigits: d });
