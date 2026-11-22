// Flat "sticker" clip-art for the HySW deck. Every function returns an SVG string.
// viewBox units: 100 per inch of the placed image, so aspect ratios match the slide boxes.
const K = { ink: "#1D2433", muted: "#5B6475", soft: "#EEF2F7", coral: "#E4572E", teal: "#17A398",
  blue: "#3B6FD8", orange: "#F28C28", yellow: "#FFC857", lav: "#8E7CC3", red: "#D64545",
  white: "#FFFFFF", grey: "#C9D1DC", night: "#2B3550" };
const BEADS = [K.coral, K.teal, K.yellow, K.lav, K.blue, K.orange];
const F = "font-family='Liberation Sans, Arial, sans-serif'";
const defs = `<defs><filter id="sh" x="-10%" y="-10%" width="130%" height="130%"><feFlood flood-color="${K.ink}" flood-opacity=".18"/><feComposite in2="SourceAlpha" operator="in"/><feOffset dx="5" dy="6"/><feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>
<pattern id="hatch" width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="10" height="10" fill="#F6F7F9"/><line x1="0" y1="0" x2="0" y2="10" stroke="${K.grey}" stroke-width="4"/></pattern></defs>`;
const svg = (w, h, body) => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${w} ${h}" width="${w}" height="${h}">${defs}${body}</svg>`;
const txt = (x, y, s, size, fill = K.ink, weight = 700, anchor = "middle") =>
  `<text x="${x}" y="${y}" ${F} font-size="${size}" font-weight="${weight}" fill="${fill}" text-anchor="${anchor}" dominant-baseline="central">${s}</text>`;
function bead(cx, cy, r, fill, letter, stroke = K.ink) {
  const lc = fill === K.yellow ? K.ink : K.white;
  return `<circle cx="${cx}" cy="${cy}" r="${r}" fill="${fill}" stroke="${stroke}" stroke-width="${Math.max(2, r / 9)}"/>
  <ellipse cx="${cx - r * .35}" cy="${cy - r * .4}" rx="${r * .28}" ry="${r * .16}" fill="#fff" opacity=".45" transform="rotate(-30 ${cx - r * .35} ${cy - r * .4})"/>
  ${letter ? txt(cx, cy + 1, letter, r * 1.05, lc) : ""}`;
}
function chain(pts, r, letters, stroke = K.ink) {
  let s = `<polyline points="${pts.map(p => p.join(",")).join(" ")}" fill="none" stroke="${stroke}" stroke-width="${r / 4}" stroke-linecap="round" stroke-linejoin="round"/>`;
  pts.forEach((p, i) => s += bead(p[0], p[1], r, BEADS[i % 6], letters ? letters[i] : "", stroke));
  return s;
}
const wave = (n, x0, dx, y0, amp, ph = 0) => Array.from({ length: n }, (_, i) => [x0 + i * dx, y0 + amp * Math.sin(i * .9 + ph)]);

const art = {};
// 1. Title: laptop with a protein on screen and a magnifier
art.title = () => svg(440, 380, `<g filter="url(#sh)">
  <rect x="40" y="40" width="330" height="215" rx="16" fill="${K.night}" stroke="${K.white}" stroke-width="5"/>
  <rect x="58" y="58" width="294" height="179" rx="6" fill="#EAF6F3"/>
  <path d="M10 268 h390 l-22 32 h-346 z" fill="${K.grey}" stroke="${K.white}" stroke-width="5" stroke-linejoin="round"/>
  <rect x="170" y="272" width="70" height="10" rx="5" fill="${K.muted}"/></g>
  ${chain(wave(9, 82, 31, 150, 34), 13, "")}
  <g filter="url(#sh)"><line x1="345" y1="250" x2="410" y2="330" stroke="${K.yellow}" stroke-width="22" stroke-linecap="round"/>
  <circle cx="310" cy="205" r="70" fill="#FFFFFF" fill-opacity=".85" stroke="${K.yellow}" stroke-width="14"/></g>
  ${bead(285, 200, 22, K.teal, "V")}${bead(335, 215, 22, K.yellow, "H")}`);

// 2. A protein: beads with letters, fading into "..."
art.protein = () => {
  const L = "MVHLTPEEKSAV".split(""); const pts = wave(12, 40, 70, 95, 38, .4);
  let s = `<g filter="url(#sh)">${chain(pts.slice(0, 9), 28, L)}</g>`;
  pts.slice(9).forEach((p, i) => s += `<g opacity="${.55 - i * .17}">${bead(p[0], p[1], 28, BEADS[(9 + i) % 6], L[9 + i])}</g>`);
  return svg(880, 200, s);
};

// 3. Query vs a library of 576,073
art.query = () => {
  let s = `<g filter="url(#sh)">${chain(wave(5, 40, 34, 200, 18), 15, "")}</g>
  <g filter="url(#sh)"><path d="M70 120 q0 -40 40 -40 h50 q40 0 40 40 q0 40 -40 40 h-22 l-18 22 l-4 -22 h-6 q-40 0 -40 -40z" fill="#fff" stroke="${K.ink}" stroke-width="4"/></g>${txt(135, 121, "?", 50, K.coral)}
  <g filter="url(#sh)"><path d="M300 70 v220 a120 34 0 0 0 240 0 v-220" fill="${K.soft}" stroke="${K.ink}" stroke-width="5"/>
  <ellipse cx="420" cy="70" rx="120" ry="34" fill="#fff" stroke="${K.ink}" stroke-width="5"/></g>`;
  for (let r = 0; r < 4; r++) for (let i = 0; i < 9; i++) s += `<circle cx="${337 + i * 21}" cy="${135 + r * 42 + 6 * Math.sin(i + r)}" r="8" fill="${BEADS[(i + r) % 6]}" stroke="${K.ink}" stroke-width="2"/>`;
  s += `<path d="M215 200 h55" stroke="${K.ink}" stroke-width="6" stroke-linecap="round"/><path d="M262 188 l14 12 l-14 12" fill="none" stroke="${K.ink}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>`;
  return svg(560, 340, s);
};

// 4. Alignment: two rows of tiles + verdict row
art.align = () => {
  const a = "MVHLTPEEK", b = "MVHL-PDEK", v = "++++-+~++";
  let s = "";
  for (let i = 0; i < 9; i++) {
    const x = 20 + i * 92, col = v[i] === "+" ? K.teal : v[i] === "~" ? K.yellow : K.red;
    s += `<g filter="url(#sh)"><rect x="${x}" y="10" width="76" height="62" rx="12" fill="#fff" stroke="${K.ink}" stroke-width="4"/>
    <rect x="${x}" y="92" width="76" height="62" rx="12" fill="${b[i] === "-" ? "#FBE3E3" : "#fff"}" stroke="${K.ink}" stroke-width="4" ${b[i] === "-" ? 'stroke-dasharray="8 6"' : ""}/></g>
    ${txt(x + 38, 42, a[i], 36)}${txt(x + 38, 124, b[i] === "-" ? "" : b[i], 36)}
    <circle cx="${x + 38}" cy="196" r="22" fill="${col}" stroke="${K.ink}" stroke-width="3"/>${txt(x + 38, 196, v[i] === "+" ? "+" : v[i] === "~" ? "+" : "−", v[i] === "~" ? 24 : 32, v[i] === "~" ? K.ink : K.white)}`;
  }
  return svg(860, 230, s);
};

// 5. DP grid with three arrows into one cell
art.grid = () => {
  const n = 6, c = 46, x0 = 60, y0 = 60, top = "MVHLTP", left = "MVHLPD"; let s = `<g filter="url(#sh)"><rect x="${x0}" y="${y0}" width="${n * c}" height="${n * c}" rx="8" fill="#fff" stroke="${K.ink}" stroke-width="4"/></g>`;
  for (let i = 1; i < n; i++) s += `<line x1="${x0 + i * c}" y1="${y0}" x2="${x0 + i * c}" y2="${y0 + n * c}" stroke="${K.grey}" stroke-width="2"/><line x1="${x0}" y1="${y0 + i * c}" x2="${x0 + n * c}" y2="${y0 + i * c}" stroke="${K.grey}" stroke-width="2"/>`;
  for (let i = 0; i < n; i++) s += txt(x0 + i * c + c / 2, 35, top[i], 24, K.muted) + txt(35, y0 + i * c + c / 2, left[i], 24, K.muted);
  const gx = x0 + 3 * c, gy = y0 + 3 * c;
  s += `<rect x="${gx - c}" y="${gy - c}" width="${c}" height="${c}" fill="#D7F2EC"/><rect x="${gx}" y="${gy - c}" width="${c}" height="${c}" fill="#FDE4D6"/><rect x="${gx - c}" y="${gy}" width="${c}" height="${c}" fill="#E5E0F3"/>
  <rect x="${gx}" y="${gy}" width="${c}" height="${c}" fill="${K.yellow}" stroke="${K.ink}" stroke-width="4"/>${txt(gx + c / 2, gy + c / 2, "?", 26)}`;
  const arrow = (x1, y1, x2, y2, col) => `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${col}" stroke-width="7" stroke-linecap="round"/><circle cx="${x2}" cy="${y2}" r="7" fill="${col}"/>`;
  s += arrow(gx - c / 2, gy - c / 2, gx + 8, gy + 8, K.teal) + arrow(gx + c / 2, gy - c / 2, gx + c / 2, gy + 8, K.coral) + arrow(gx - c / 2, gy + c / 2, gx + 8, gy + c / 2, K.lav);
  return svg(360, 360, s);
};

// 6. Big wall of cells
art.cells = () => {
  let s = ""; for (let r = 0; r < 9; r++) for (let i = 0; i < 26; i++) {
    const on = (r * 26 + i) % 7 === 0; s += `<rect x="${10 + i * 33}" y="${10 + r * 33}" width="27" height="27" rx="5" fill="${on ? K.yellow : K.soft}" stroke="${on ? K.ink : K.grey}" stroke-width="2"/>`;
  }
  return svg(880, 310, s);
};

// 7. Snail on a CPU chip
art.snail = () => {
  let s = `<g filter="url(#sh)">`;
  for (let i = 0; i < 6; i++) s += `<rect x="${92 + i * 36}" y="166" width="12" height="22" rx="3" fill="${K.muted}"/><rect x="${92 + i * 36}" y="372" width="12" height="22" rx="3" fill="${K.muted}"/>`;
  s += `<rect x="70" y="182" width="250" height="194" rx="18" fill="${K.night}" stroke="${K.ink}" stroke-width="4"/>${txt(195, 300, "1 core", 28, K.white)}</g>
  <g filter="url(#sh)"><path d="M90 182 q-10 -10 10 -14 h200 q30 0 32 -26 q0 -50 -60 -56" fill="#9CD3A8" stroke="${K.ink}" stroke-width="4"/>
  <circle cx="190" cy="110" r="66" fill="${K.orange}" stroke="${K.ink}" stroke-width="5"/>
  <path d="M190 110 m0 -8 a8 8 0 1 1 -8 8 a18 18 0 1 1 18 18 a30 30 0 1 1 -30 -30 a44 44 0 1 1 44 44" fill="none" stroke="${K.ink}" stroke-width="4"/>
  <path d="M300 92 l-6 -40 M322 96 l10 -38" stroke="${K.ink}" stroke-width="4" stroke-linecap="round"/><circle cx="293" cy="50" r="8" fill="${K.ink}"/><circle cx="333" cy="56" r="8" fill="${K.ink}"/></g>
  <path d="M30 150 h-20 M40 120 h-30 M30 90 h-18" stroke="${K.grey}" stroke-width="5" stroke-linecap="round"/>`;
  return svg(380, 400, s);
};

// 8. Many independent little grids
art.many = () => {
  let s = ""; for (let r = 0; r < 2; r++) for (let i = 0; i < 6; i++) {
    const x = 20 + i * 140, y = 20 + r * 150, col = BEADS[(i + r * 2) % 6];
    s += `<g filter="url(#sh)"><rect x="${x}" y="${y + 26}" width="104" height="96" rx="10" fill="#fff" stroke="${K.ink}" stroke-width="3"/></g>`;
    for (let k = 1; k < 4; k++) s += `<line x1="${x + k * 26}" y1="${y + 26}" x2="${x + k * 26}" y2="${y + 122}" stroke="${K.grey}" stroke-width="2"/><line x1="${x}" y1="${y + 26 + k * 24}" x2="${x + 104}" y2="${y + 26 + k * 24}" stroke="${K.grey}" stroke-width="2"/>`;
    for (let k = 0; k < 5; k++) s += `<circle cx="${x + 12 + k * 20}" cy="${y + 10}" r="8" fill="${col}" stroke="${K.ink}" stroke-width="2"/>`;
  }
  return svg(860, 320, s);
};

// Divider 1: a wide register
art.reg = (dark = true) => {
  const st = dark ? K.white : K.ink; let s = `<g filter="url(#sh)"><rect x="10" y="60" width="840" height="120" rx="22" fill="${dark ? K.night : "#fff"}" stroke="${st}" stroke-width="5"/></g>`;
  for (let i = 0; i < 16; i++) {
    const x = 32 + i * 51.5; s += `<rect x="${x}" y="82" width="40" height="76" rx="9" fill="${BEADS[i % 6]}" stroke="${st}" stroke-width="3"/>`;
    for (let k = 0; k < 3; k++) s += `<circle cx="${x + 20}" cy="${20 - k * 0 + (k - 1) * 0}" r="0"/>`;
  }
  return svg(860, 240, s);
};
// 10. Sixteen proteins dropping into sixteen lanes
art.lanes = () => {
  let s = `<g filter="url(#sh)"><rect x="10" y="200" width="840" height="110" rx="22" fill="#fff" stroke="${K.ink}" stroke-width="5"/></g>`;
  for (let i = 0; i < 16; i++) {
    const x = 34 + i * 51.5, col = BEADS[i % 6];
    s += `<rect x="${x}" y="220" width="40" height="70" rx="9" fill="${col}" stroke="${K.ink}" stroke-width="3"/>${txt(x + 20, 255, i + 1, 18, col === K.yellow ? K.ink : K.white)}`;
    for (let k = 0; k < 4; k++) s += `<circle cx="${x + 20 + 5 * Math.sin(k + i)}" cy="${30 + k * 36}" r="12" fill="${col}" stroke="${K.ink}" stroke-width="2.5"/>`;
    s += `<line x1="${x + 20}" y1="168" x2="${x + 20}" y2="190" stroke="${K.muted}" stroke-width="3" stroke-dasharray="4 4"/>`;
  }
  return svg(860, 320, s);
};

// 11. Unsorted vs sorted batches
art.sort = () => {
  const un = [.9, .35, .7, .5, 1, .3, .8, .45], so = [.97, .96, .98, 1, .97, .99, .96, .98];
  const panel = (x0, arr, label) => {
    let s = `<g filter="url(#sh)"><rect x="${x0}" y="10" width="400" height="300" rx="16" fill="#fff" stroke="${K.ink}" stroke-width="4"/></g>`;
    arr.forEach((v, i) => { const y = 34 + i * 33, W = 340;
      s += `<rect x="${x0 + 30}" y="${y}" width="${W}" height="22" rx="6" fill="url(#hatch)" stroke="${K.grey}" stroke-width="2"/><rect x="${x0 + 30}" y="${y}" width="${W * v}" height="22" rx="6" fill="${BEADS[i % 6]}" stroke="${K.ink}" stroke-width="2.5"/>`; });
    return s;
  };
  return svg(860, 320, panel(10, un) + panel(450, so));
};

// Divider 2 / 14. The hybrid chip
art.chip = (dark = false) => {
  const st = dark ? K.white : K.ink; let s = `<g filter="url(#sh)">`;
  for (let i = 0; i < 9; i++) s += `<rect x="${70 + i * 48}" y="6" width="14" height="26" rx="4" fill="${K.muted}"/><rect x="${70 + i * 48}" y="388" width="14" height="26" rx="4" fill="${K.muted}"/>`;
  s += `<rect x="20" y="28" width="500" height="364" rx="26" fill="${dark ? K.night : K.soft}" stroke="${st}" stroke-width="5"/></g>`;
  [0, 1].forEach(i => { const x = 50 + i * 120;
    s += `<g filter="url(#sh)"><rect x="${x}" y="60" width="100" height="140" rx="16" fill="${K.blue}" stroke="${st}" stroke-width="4"/></g>${txt(x + 50, 118, "P", 52, K.white)}
    <circle cx="${x + 32}" cy="172" r="10" fill="#fff"/><circle cx="${x + 68}" cy="172" r="10" fill="#fff"/>`; });
  [0, 1].forEach(g => { const gx = 50 + g * 240;
    s += `<rect x="${gx - 12}" y="230" width="${226}" height="140" rx="16" fill="none" stroke="${dark ? K.grey : K.muted}" stroke-width="3" stroke-dasharray="9 7"/>`;
    for (let i = 0; i < 4; i++) s += `<g filter="url(#sh)"><rect x="${gx + i * 52}" y="250" width="42" height="56" rx="9" fill="${K.orange}" stroke="${st}" stroke-width="3"/></g>${txt(gx + i * 52 + 21, 278, "E", 24, K.white)}`;
    s += txt(gx + 101, 340, "shared L2", 20, dark ? K.grey : K.muted, 700); });
  s += txt(400, 115, "AVX2", 30, dark ? K.grey : K.muted) + txt(400, 150, "15 W", 30, dark ? K.yellow : K.coral);
  return svg(540, 420, s);
};

// 15. Static vs dynamic timelines
art.sched = () => {
  const rows = ["P", "P", "E", "E", "E", "E", "E", "E", "E", "E"];
  const panel = (y0, dyn) => {
    let s = `<g filter="url(#sh)"><rect x="10" y="${y0}" width="840" height="180" rx="16" fill="#fff" stroke="${K.ink}" stroke-width="4"/></g>`;
    const X = 70, full = 560; // one unit of ideal balanced time
    rows.forEach((r, i) => { const y = y0 + 40 + i * 13.5, col = r === "P" ? K.blue : K.orange;
      s += txt(48, y + 6, r, 13, K.muted);
      if (!dyn) { const len = r === "P" ? full * 1.31 / 2 : full * 1.31;
        s += `<rect x="${X}" y="${y}" width="${len}" height="11" rx="4" fill="${col}"/>`;
        if (r === "P") s += `<rect x="${X + len}" y="${y}" width="${full * 1.31 - len}" height="11" rx="4" fill="url(#hatch)"/>`;
      } else { const seg = r === "P" ? 7 : 3.5; let x = X;
        const n = r === "P" ? 8 : 4; for (let k = 0; k < n; k++) { const w = full / n - 3; s += `<rect x="${x}" y="${y}" width="${w}" height="11" rx="4" fill="${col}"/>`; x += full / n; } }
    });
    const endX = X + (dyn ? full : full * 1.31);
    s += `<line x1="${endX}" y1="${y0 + 34}" x2="${endX}" y2="${y0 + 172}" stroke="${dyn ? K.teal : K.red}" stroke-width="4" stroke-dasharray="8 6"/>`;
    return s;
  };
  return svg(860, 400, panel(10, false) + panel(210, true));
};

// 16. Two threads, one vector unit
art.ht = () => svg(420, 340, `<g filter="url(#sh)"><rect x="20" y="30" width="380" height="290" rx="24" fill="${K.blue}" stroke="${K.ink}" stroke-width="5"/></g>
  ${txt(210, 66, "one P-core", 24, K.white)}
  <path d="M80 110 c0 60 60 70 110 100" fill="none" stroke="${K.yellow}" stroke-width="12" stroke-linecap="round"/>
  <path d="M340 110 c0 60 -60 70 -110 100" fill="none" stroke="#FFFFFF" stroke-width="12" stroke-linecap="round"/>
  <circle cx="80" cy="110" r="22" fill="${K.yellow}" stroke="${K.ink}" stroke-width="4"/>${txt(80, 111, "1", 22)}
  <circle cx="340" cy="110" r="22" fill="#fff" stroke="${K.ink}" stroke-width="4"/>${txt(340, 111, "2", 22)}
  <g filter="url(#sh)"><rect x="130" y="205" width="160" height="90" rx="16" fill="${K.night}" stroke="${K.ink}" stroke-width="4"/></g>
  ${txt(210, 238, "1 vector", 22, K.white)}${txt(210, 266, "unit", 22, K.white)}`);

// 17. Power plug + thermometer
art.power = () => svg(300, 380, `<g filter="url(#sh)"><rect x="110" y="20" width="80" height="250" rx="40" fill="#fff" stroke="${K.ink}" stroke-width="5"/>
  <circle cx="150" cy="300" r="62" fill="${K.coral}" stroke="${K.ink}" stroke-width="5"/></g>
  <rect x="135" y="90" width="30" height="190" fill="${K.coral}"/><line x1="135" y1="90" x2="165" y2="90" stroke="${K.ink}" stroke-width="3"/>
  ${[60, 110, 160, 210].map(y => `<line x1="190" y1="${y}" x2="215" y2="${y}" stroke="${K.ink}" stroke-width="4" stroke-linecap="round"/>`).join("")}
  ${txt(150, 302, "15 W", 30, K.white)}
  <path d="M40 60 l26 -10 l-10 30 l26 -10" fill="none" stroke="${K.yellow}" stroke-width="8" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M235 140 l26 -10 l-10 30 l26 -10" fill="none" stroke="${K.yellow}" stroke-width="8" stroke-linecap="round" stroke-linejoin="round"/>`);

// 18. Ideal vs real bar with three brakes
art.brakes = () => {
  let s = `<rect x="20" y="40" width="820" height="66" rx="14" fill="none" stroke="${K.ink}" stroke-width="4" stroke-dasharray="12 8"/>
  <g filter="url(#sh)"><rect x="20" y="150" width="${820 * 2.52 / 10}" height="66" rx="14" fill="${K.teal}" stroke="${K.ink}" stroke-width="4"/></g>`;
  for (let i = 1; i < 10; i++) s += `<line x1="${20 + i * 82}" y1="44" x2="${20 + i * 82}" y2="102" stroke="${K.grey}" stroke-width="2"/>`;
  return svg(860, 240, s);
};

// 19. L1 bucket overflowing into shared L2 tank
art.cache = () => svg(860, 330, `
  <g filter="url(#sh)"><path d="M60 40 h150 l-16 140 h-118 z" fill="#fff" stroke="${K.ink}" stroke-width="5" stroke-linejoin="round"/></g>
  <path d="M74 70 h122 l-12 106 h-98 z" fill="${K.teal}"/>
  <path d="M206 64 c40 0 50 40 60 110 c4 30 14 46 30 56" fill="none" stroke="${K.teal}" stroke-width="16" stroke-linecap="round"/>
  ${txt(135, 210, "L1 (per core)", 22, K.ink)}
  <g filter="url(#sh)"><rect x="270" y="200" width="300" height="110" rx="18" fill="#fff" stroke="${K.ink}" stroke-width="5"/></g>
  <rect x="284" y="240" width="272" height="56" rx="10" fill="${K.teal}" opacity=".55"/>${txt(420, 222, "L2 shared by 4 E-cores", 20, K.ink)}
  ${[0, 1, 2, 3].map(i => `<rect x="${300 + i * 68}" y="150" width="36" height="34" rx="7" fill="${K.orange}" stroke="${K.ink}" stroke-width="3"/><line x1="${318 + i * 68}" y1="184" x2="${318 + i * 68}" y2="200" stroke="${K.ink}" stroke-width="4"/>`).join("")}
  <path d="M600 170 h40" stroke="${K.ink}" stroke-width="6" stroke-linecap="round"/><path d="M632 158 l14 12 l-14 12" fill="none" stroke="${K.ink}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>
  <g filter="url(#sh)"><rect x="670" y="50" width="170" height="240" rx="10" fill="#fff" stroke="${K.ink}" stroke-width="4"/></g>
  ${Array.from({ length: 9 }, (_, i) => `<line x1="670" y1="${50 + (i + 1) * 24}" x2="840" y2="${50 + (i + 1) * 24}" stroke="${K.grey}" stroke-width="2"/>`).join("")}
  <rect x="712" y="52" width="44" height="236" fill="${K.yellow}" opacity=".9" stroke="${K.ink}" stroke-width="3"/>
  ${txt(755, 312, "16 columns at a time", 20, K.ink)}`);

// 21. Equal scores + check badge
art.check = () => {
  const col = (x, c) => [0, 1, 2].map(i => `<g filter="url(#sh)"><rect x="${x}" y="${40 + i * 78}" width="150" height="58" rx="12" fill="#fff" stroke="${K.ink}" stroke-width="4"/></g>${txt(x + 75, 69 + i * 78, ["1,342", "87", "29,610"][i], 26, c)}`).join("");
  return svg(640, 300, col(20, K.teal) + txt(240, 147, "=", 70, K.ink) + col(290, K.lav) +
    `<g filter="url(#sh)"><circle cx="560" cy="150" r="64" fill="${K.teal}" stroke="${K.ink}" stroke-width="5"/></g><path d="M528 152 l22 22 l42 -46" fill="none" stroke="#fff" stroke-width="14" stroke-linecap="round" stroke-linejoin="round"/>
    ${txt(95, 18, "HySW", 20, K.muted)}${txt(365, 18, "Parasail", 20, K.muted)}`);
};

// 23. Lesson icons (on dark cards)
art.icon = (kind) => {
  let s = `<circle cx="60" cy="60" r="54" fill="${kind === "a" ? K.teal : kind === "b" ? K.orange : K.coral}" stroke="#fff" stroke-width="5"/>`;
  if (kind === "a") s += `<circle cx="52" cy="52" r="22" fill="none" stroke="#fff" stroke-width="8"/><line x1="68" y1="68" x2="86" y2="86" stroke="#fff" stroke-width="10" stroke-linecap="round"/>`;
  if (kind === "b") s += `${[0, 1, 2].map(i => `<rect x="${30 + i * 14}" y="${34 + i * 6}" width="34" height="46" rx="6" fill="#fff" stroke="${K.ink}" stroke-width="3" transform="rotate(${-12 + i * 12} 60 70)"/>`).join("")}`;
  if (kind === "c") s += `<rect x="44" y="34" width="32" height="44" rx="8" fill="#fff"/><rect x="50" y="22" width="6" height="16" fill="#fff"/><rect x="64" y="22" width="6" height="16" fill="#fff"/><path d="M60 78 v20" stroke="#fff" stroke-width="7" stroke-linecap="round"/>`;
  if (kind === "d") s += `<path d="M60 26 l30 54 h-60 z" fill="none" stroke="#fff" stroke-width="7" stroke-linejoin="round"/><line x1="60" y1="46" x2="60" y2="64" stroke="#fff" stroke-width="7" stroke-linecap="round"/><circle cx="60" cy="73" r="4" fill="#fff"/>`;
  return svg(120, 120, s);
};
export { art, K };
