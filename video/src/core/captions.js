// Long-form subtitles (Karaoke style from the Nafay 3D library): the spoken line in a dark rounded box at the bottom,
// at most two rows; words already said are white, the word being said sits on a yellow pill, the rest wait at 55%.
// A long line is paged: two rows per page, the page advances with the voice.
import { ctx, C, clamp, W } from './lib.js';
import { T, lineWords } from './timing.js';

const SIZE = 36, MAXW = 1460, BOTTOM = 1052, PADX = 28, PADY = 12, LH = 50;
let pages = [];

export function buildCaptions() {
  pages = [];
  T.lines.forEach((ln, i) => {
    const ws = lineWords(i); if (!ws.length) return;
    ctx.font = `700 ${SIZE}px Inter`;
    const sp = ctx.measureText(' ').width, rows = []; let cur = [], w = 0;
    for (const x of ws) { const ww = ctx.measureText(x.w).width; if (cur.length && w + sp + ww > MAXW) { rows.push(cur); cur = []; w = 0; } w += (cur.length ? sp : 0) + ww; cur.push(x); }
    if (cur.length) rows.push(cur);
    for (let r = 0; r < rows.length; r += 2) pages.push({ line: i, rows: rows.slice(r, r + 2) });
  });
  pages.forEach((p) => { p.s = p.rows[0][0].s - 0.08; });
  pages.forEach((p, k) => { const nx = pages[k + 1], lineEnd = T.lines[p.line].end;
    p.e = nx ? (nx.line === p.line ? nx.s : Math.min(nx.s, lineEnd + 0.6)) : T.dur; });
}
export const captionList = () => pages.map((p) => ({ s: p.s, e: p.e, words: p.rows.flat().map((w) => ({ txt: w.w })) }));

export function drawCaption(t, hide) {
  if (hide) return;
  const p = pages.find((p) => t >= p.s && t < p.e); if (!p) return;
  ctx.save(); ctx.font = `700 ${SIZE}px Inter`; ctx.textBaseline = 'middle'; ctx.textAlign = 'left';
  const sp = ctx.measureText(' ').width, widths = p.rows.map((r) => r.reduce((a, w) => a + ctx.measureText(w.w).width, 0) + sp * (r.length - 1));
  const bw = Math.max(...widths) + PADX * 2, bh = p.rows.length * LH + PADY * 2, by = BOTTOM - bh;
  ctx.globalAlpha = clamp((t - p.s) / 0.15) * clamp((p.e - t) / 0.15);
  ctx.beginPath(); ctx.roundRect(W / 2 - bw / 2, by, bw, bh, 16); ctx.fillStyle = 'rgba(20,25,38,0.86)'; ctx.fill();
  p.rows.forEach((row, ri) => {
    let x = W / 2 - widths[ri] / 2; const y = by + PADY + LH / 2 + ri * LH;
    for (const w of row) {
      const ww = ctx.measureText(w.w).width, now = t >= w.s - 0.03 && t < Math.max(w.e, w.s + 0.12), said = t >= w.e;
      if (now) { ctx.beginPath(); ctx.roundRect(x - 7, y - 23, ww + 14, 46, 10); ctx.fillStyle = C.yellow; ctx.fill(); }
      ctx.fillStyle = now ? C.ink : said ? '#FFFFFF' : 'rgba(255,255,255,0.55)'; ctx.fillText(w.w, x, y + 1); x += ww + sp;
    }
  });
  ctx.restore();
}
