// Boot: fonts → script + timing → captions → art images → shots. draw(t) renders film time t (pure function of t).
// window.OP is the API tools/capture.mjs drives. OP.frame is async: it first loads any recorded demo frame the shot needs.
import { W, H, setCtx, ctx, clamp, eo, eio } from './core/lib.js';
import { T, loadTiming } from './core/timing.js';
import { buildCaptions, drawCaption, captionList } from './core/captions.js';
import { buildShots, loadAssets, needs } from './shots.js';

const cv = document.getElementById('cv');
setCtx(cv.getContext('2d'));
const OP = (window.OP = { ready: false });
const XF = 0.35;   // crossfade between shots (s)

function drawShot(S, i, t) {
  const sh = S[i], end = i < S.length - 1 ? S[i + 1].at : T.dur, d = end - sh.at, lt = t - sh.at;
  const drift = 1 + (sh.zoom ?? 0.03) * eio(clamp(lt / d));
  const [cx, cy] = sh.cam || [W / 2, H / 2];
  ctx.save(); ctx.translate(cx, cy); ctx.scale(drift, drift); ctx.translate(-cx, -cy);
  sh.draw(lt, d, t);
  ctx.restore();
}
function draw(t) {
  t = clamp(t, 0, T.dur - 1e-4);
  ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.globalAlpha = 1;
  ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, H);
  const S = OP._shots; let i = S.length - 1; while (i > 0 && S[i].at > t) i--;
  const lt = t - S[i].at;
  if (i > 0 && lt < XF && !S[i].hard) { drawShot(S, i - 1, t); ctx.globalAlpha = eo(lt / XF); drawShot(S, i, t); ctx.globalAlpha = 1; }
  else drawShot(S, i, t);
  const fade = Math.min(clamp(t / 0.5), clamp((T.dur - t) / 0.8));
  if (fade < 1) { ctx.fillStyle = '#000'; ctx.globalAlpha = 1 - fade; ctx.fillRect(0, 0, W, H); ctx.globalAlpha = 1; }
  drawCaption(t, S[i].noCaption);
}

(async () => {
  try {
    await Promise.all([document.fonts.load('900 100px Inter'), document.fonts.load('800 100px Inter'), document.fonts.load('700 100px Inter')]);
    const script = await (await fetch('script.json', { cache: 'no-store' })).json();
    await loadTiming();
    buildCaptions(script);
    await loadAssets();
    const { shots, cues } = buildShots();
    shots.sort((a, b) => a.at - b.at);
    OP._shots = shots; OP.script = script;
    OP.duration = () => T.dur;
    OP.draw = draw;
    OP.frame = async (t, q = 0.92) => { await needs(t); draw(t); return cv.toDataURL('image/jpeg', q); };
    OP.shots = () => shots.map((s, i) => ({ id: s.id, at: +s.at.toFixed(3), end: +(i < shots.length - 1 ? shots[i + 1].at : T.dur).toFixed(3), note: s.note || '' }));
    OP.captions = () => captionList().map((c) => ({ s: +c.s.toFixed(3), e: +c.e.toFixed(3), text: c.words.map((w) => w.txt).join(' ') }));
    OP.cues = () => cues.slice().sort((a, b) => a.t - b.t);
    OP.ready = true;
    const q = new URLSearchParams(location.search);
    if (!q.has('capture') && q.has('t')) { await needs(+q.get('t')); draw(+q.get('t')); }
  } catch (e) { OP.bootError = String(e && e.stack || e); console.error(e); }
})();
