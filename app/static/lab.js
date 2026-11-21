// Performance Lab: "Before and after". Each pair isolates one change and shows both sides on the same work.
// Live numbers come from engine runs started here; stored numbers come from results/ with their date and settings.
"use strict";

const L = { results: null, race: null, schedules: null, simd: {}, pair: "simd", task: null, progress: {}, replay: null };

const PAIRS = [
  { id: "simd", label: "Scalar vs SIMD", run: "race" },
  { id: "threads", label: "1 thread vs 10", run: "race" },
  { id: "schedule", label: "Static vs dynamic schedule", run: "schedules" },
  { id: "power", label: "Burst vs sustained", run: null },
  { id: "parasail", label: "HySW vs Parasail", run: null },
];

const ARROW = '<svg viewBox="0 0 100 22" preserveAspectRatio="none" aria-hidden="true"><path d="M2 11 H92" stroke="currentColor" stroke-width="2" fill="none"/><path d="M84 3 L96 11 L84 19" stroke="currentColor" stroke-width="2" fill="none" stroke-linejoin="round"/></svg>';

function tag(kind, text) { return el("span", { class: `ptag ${kind}`, text: text || kind }); }
function speedup(a, b) { return a / b; }
function dateOf(si) { return si && si.date ? si.date.slice(0, 10) : "date unknown"; }
function exampleName(acc) {
  const ex = S.examples.find((e) => e.accession === acc);
  return ex ? ex.header.split(" OS=")[0].replace(/^sp\|[^|]+\|\S+\s*/, "") : acc;
}

// ------------------------------------------------------------------ instruments
const POLICY = {
  static: "Static: equal shares handed out up front",
  "dynamic,1": "Dynamic: one batch at a time to whichever thread is free",
  "dynamic,chunk": "Dynamic in chunks of batches",
  guided: "Guided: chunks that shrink as work runs out",
  steal: "Equal shares, idle threads steal from busy ones",
  "cost+steal": "Shares of equal estimated cost, plus stealing",
  "costcap+steal": "Shares weighted by each core's measured speed, plus stealing",
};

// 16 thin lanes; the active ones fill from the engine's real progress for that step (1 lane for scalar, 16 for SIMD).
function laneStrip(active, step, caption) {
  const p = L.progress[step];
  const frac = p ? p.done / p.total : 0;
  const box = el("div", { class: "lanes", role: "img", "aria-label": caption, "data-step": step });
  for (let i = 0; i < 16; i++) {
    const b = el("b");
    b.style.height = `${100 * frac}%`;
    box.append(el("i", { class: i < active ? "" : "idle" }, b));
  }
  return [box, el("div", { class: "lanes-cap", text: caption })];
}

function storedLine(text) { return el("p", { class: "s-stored" }, el("b", { text: "Stored: " }), text); }
function whyLine(live, stored, race) {
  if (!live || !stored || (live / stored < 1.5 && stored / live < 1.5)) return null;
  return el("p", { class: "s-why", text: `Why the live and stored figures differ: the live runs are single short bursts from idle${race && race.on_ac === false ? ", on battery" : ""}; `
    + "the stored figure is the median of repeated runs under a sustained load, when the 15 W power limit holds every core back (see Burst vs sustained)." });
}

function miniProgress(step) {
  const p = L.progress[step];
  const bar = el("i");
  bar.style.width = `${p ? (100 * p.done) / p.total : 0}%`;
  return el("div", { class: "mini-progress", id: `mp-${step}` }, bar);
}

function coreTiles(stats) {
  const used = new Map((stats || []).map((t) => [t.cpu, t]));
  const max = Math.max(1, ...(stats || []).map((t) => t.items));
  const box = el("div", { class: "cores small" });
  for (const c of S.system.info.cpus) {
    const t = used.get(c.cpu);
    const tile = el("div", { class: `core ${(c.type || "").toLowerCase()}${t ? "" : " off"}`, title: `cpu${c.cpu} ${c.type}-core` },
      el("div", { class: "fill" }), el("span", { class: "lbl" }, el("b", { text: `cpu${c.cpu}` }), " ", c.type),
      el("span", { class: "cnt", text: t ? fmtInt(t.items) : "—" }));
    if (t) tile.querySelector(".fill").style.height = `${(100 * t.items) / max}%`;
    box.append(tile);
  }
  return box;
}

function side(label, big, bigClass, text, extra, ...body) {
  const b = el("div", { class: "s-big" });
  if (big === null) b.append(el("span", { class: "pending", text: "not run yet" }));
  else if (Array.isArray(big)) b.append(big[0], el("small", { class: bigClass, text: big[1] }));
  else b.append(big);
  // Always four children, so both sides share the rows of .duo (heading, figure, sentence, instrument) and the
  // paired instruments start on one baseline; the stored figure and its note sit under the instrument.
  return [el("h3", { class: "s-label", text: label }), b, el("p", { class: "s-text", text }),
          el("div", { class: "s-body" }, ...body, ...(extra || []).filter(Boolean))];
}

function hbars(rows, valueKey, fmt, highlight) {
  const max = Math.max(...rows.map((r) => r[valueKey]));
  const box = el("div", { class: "hbars" });
  for (const r of rows) {
    const bar = el("div", { class: "hb-bar" });
    bar.style.width = `${(100 * r[valueKey]) / max}%`;
    if (highlight && !highlight(r)) bar.style.opacity = "0.45";
    box.append(el("span", { text: r.label }), el("div", { class: "hb-track" }, bar), el("span", { class: "hb-v", text: fmt(r) }));
  }
  return box;
}

// Per-core busy intervals of a traced run, all runs drawn on one shared time scale.
function timeline(step, scale) {
  const box = el("div", { class: "timeline", role: "img",
    "aria-label": `Per-core busy time, ${step.policy} schedule: finished in ${fmtSec(step.timing.seconds)}` });
  for (const t of step.thread_stats) {
    const row = el("div", { class: `tl-row ${t.type.toLowerCase()}` });
    for (const [a, b] of t.busy || []) {
      const i = el("i");
      i.style.left = `${(100 * a) / scale}%`;
      i.style.width = `${Math.max(0.3, (100 * (b - a)) / scale)}%`;
      row.append(i);
    }
    const end = el("span", { class: "tl-end" });
    end.style.left = `${(100 * step.timing.seconds) / scale}%`;
    row.append(end);
    box.append(el("span", { class: "tl-cpu", text: `cpu${t.cpu} ${t.type}` }), row);
  }
  box.append(el("div", { class: "tl-axis" }, el("span", { text: "0" }), el("span", { text: fmtSec(scale) })));
  return box;
}

function lineChart(series, opts) {
  const W = 720, H = 250, ml = 34, mr = 150, mt = 22, mb = 26;
  const xs = series.flatMap((s) => s.points.map((p) => p[0])), ys = series.flatMap((s) => s.points.map((p) => p[1]));
  const xmax = Math.max(...xs), ymax = Math.ceil(Math.max(...ys) / 10) * 10;
  const X = (x) => ml + ((W - ml - mr) * x) / xmax, Y = (y) => mt + (H - mt - mb) * (1 - y / ymax);
  const NS = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
  svg.setAttribute("role", "img");
  svg.setAttribute("aria-label", opts.label);
  const add = (tag2, a, text) => { const n = document.createElementNS(NS, tag2); for (const [k, v] of Object.entries(a)) n.setAttribute(k, v); if (text !== undefined) n.textContent = text; svg.append(n); return n; };
  for (let y = 0; y <= ymax; y += 10) {
    add("line", { x1: ml, x2: W - mr, y1: Y(y), y2: Y(y), class: "grid" });
    add("text", { x: ml - 6, y: Y(y) + 4, "text-anchor": "end" }, y);
  }
  for (let x = 0; x <= xmax; x += 20) add("text", { x: X(x), y: H - 8, "text-anchor": "middle" }, `${x} s`);
  add("line", { x1: ml, x2: W - mr, y1: Y(0), y2: Y(0), class: "axis" });
  for (const s of series) {
    add("polyline", { points: s.points.map((p) => `${X(p[0])},${Y(p[1])}`).join(" "), fill: "none", stroke: s.colour, "stroke-width": 2,
      "stroke-linejoin": "round" });
    const last = s.points[s.points.length - 1];
    add("text", { x: X(last[0]) + 8, y: Y(last[1]) + 4, class: "lbl" }, s.label);
  }
  add("text", { x: ml - 6, y: 10, "text-anchor": "end", class: "lbl" }, opts.yLabel);
  return el("div", { class: "chart" }, svg);
}

// ------------------------------------------------------------------ Inside SIMD replay (real H values, tiled order)
function simdOrder(rows, cols, block) {
  const order = [];
  for (let j0 = 0; j0 < cols; j0 += block)
    for (let i = 0; i < rows; i++)
      for (let j = j0; j < Math.min(cols, j0 + block); j++) order.push([i, j]);
  return order;
}

function drawLanes(canvas, d, step, onlyLane) {
  const single = onlyLane !== undefined;
  const rows = d.window.rows, cols = d.window.cols, cs = single ? 8 : 4, gap = 16, labelH = single ? 0 : 16;
  const lanes = onlyLane === undefined ? [...Array(16).keys()] : [onlyLane];
  const per = onlyLane === undefined ? 4 : 1;
  const w = cols * cs, h = rows * cs;
  const W = per * w + (per - 1) * gap, H = Math.ceil(lanes.length / per) * (h + labelH + gap) - gap;
  const dpr = window.devicePixelRatio || 1;
  if (canvas.width !== W * dpr) { canvas.width = W * dpr; canvas.height = H * dpr; canvas.style.aspectRatio = `${W} / ${H}`; }
  const g = canvas.getContext("2d");
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, W, H);
  const max = Math.max(1, ...d.H.flat(2));
  const empty = css("--surface-2"), ink = css("--ink-2"), accent = css("--accent");
  const order = L.replay.order;
  const done = new Uint8Array(rows * cols);
  // scalar fills one cell per step; SIMD fills the same cell of all 16 lanes per step
  for (let k = 0; k < Math.min(step, order.length); k++) done[order[k][0] * cols + order[k][1]] = 1;
  lanes.forEach((lane, n) => {
    const x0 = (n % per) * (w + gap), y0 = Math.floor(n / per) * (h + labelH + gap);
    const seqInfo = d.sequences[lane];
    if (!single) {
      g.fillStyle = lane === d.target_lane ? css("--ink") : ink;
      g.font = `${lane === d.target_lane ? "600 " : ""}11px system-ui, sans-serif`;
      g.fillText(`lane ${lane}  ${seqInfo ? seqInfo.accession : "empty"}${lane === d.target_lane ? "  (query itself)" : ""}`, x0, y0 + 11);
    }
    for (let i = 0; i < rows; i++)
      for (let j = 0; j < cols; j++) {
        const v = d.H[i][j][lane];
        const pad = !seqInfo || d.window.col0 + j >= seqInfo.length;
        if (!done[i * cols + j] || pad) { g.fillStyle = pad ? css("--line") : empty; g.fillRect(x0 + j * cs, y0 + labelH + i * cs, cs - 0.5, cs - 0.5); continue; }
        g.globalAlpha = 0.1 + 0.9 * (v / max);
        g.fillStyle = accent;
        g.fillRect(x0 + j * cs, y0 + labelH + i * cs, cs - 0.5, cs - 0.5);
        g.globalAlpha = 1;
      }
  });
}

async function loadSimd(acc) {
  if (!L.simd[acc]) L.simd[acc] = await api(`/api/lab/simd?accession=${encodeURIComponent(acc)}`);
  return L.simd[acc];
}

function simdReplay(d) {
  const order = simdOrder(d.window.rows, d.window.cols, d.colblock);
  if (!L.replay || L.replay.data !== d) { stopReplay(); L.replay = { data: d, order, step: order.length, timer: null }; }
  const big = el("canvas", { role: "img", "aria-label": `Score cells of 16 proteins in batch ${d.batch}, filled in the kernel's order` });
  const counter = el("dd", { class: "num" });
  const render = () => {
    drawLanes(big, d, L.replay.step);
    const small = document.getElementById("scalar-canvas");
    if (small) drawLanes(small, d, L.replay.step, d.target_lane);
    counter.textContent = `${fmtInt(Math.min(L.replay.step, order.length))} of ${fmtInt(order.length)}`;
  };
  L.replay.render = render;
  const play = (rate) => {
    stopReplay();
    if (L.replay.step >= order.length) L.replay.step = 0;
    L.replay.timer = setInterval(() => {
      L.replay.step = Math.min(order.length, L.replay.step + rate);
      render();
      if (L.replay.step >= order.length) stopReplay();
    }, 40);
  };
  const lengths = d.sequences.filter(Boolean).map((x) => x.length);
  const box = el("div", {},
    el("h3", { text: "Inside the vector: one real batch of 16 proteins" }),
    el("p", { text: `The engine's own trace of ${d.database} batch ${fmtInt(d.batch)}: ${fmtInt(d.window.rows)} query rows × ${fmtInt(d.window.cols)} columns of the score table for each of the 16 proteins, `
      + `filled in the order the tiled kernel computes them (blocks of ${d.colblock} columns). Every step updates the same cell in all 16 lanes at once.` }),
    el("div", { class: "replay-ctl" },
      el("button", { type: "button", class: "ghost", text: "Play", onclick: () => play(2) }),
      el("button", { type: "button", class: "ghost", text: "Play fast", onclick: () => play(12) }),
      el("button", { type: "button", class: "ghost", text: "Pause", onclick: stopReplay }),
      el("button", { type: "button", class: "ghost", text: "Show all", onclick: () => { stopReplay(); L.replay.step = order.length; render(); } })),
    el("div", { class: "replay" }, big,
      el("div", { class: "replay-side" },
        el("dl", {},
          el("dt", { text: "Steps shown" }), counter,
          el("dt", { text: "Protein lengths" }), el("dd", { text: lengths.every((x) => x === lengths[0]) ? `all ${lengths[0]}` : `${Math.min(...lengths)}–${Math.max(...lengths)}` }),
          el("dt", { text: "Lane scores" }), el("dd", { text: `${fmtInt(d.sequences[d.target_lane].score)} (query lane); others ${fmtInt(Math.min(...d.sequences.filter(Boolean).map((x) => x.score)))}–${fmtInt(Math.max(...d.sequences.filter((x, i) => x && i !== d.target_lane).map((x) => x.score)))}` })),
        el("div", { class: "ramp" }), el("div", { class: "ramp-l" }, el("span", { text: "score 0" }), el("span", { text: `${fmtInt(Math.max(...d.H.flat(2)))}` })),
        el("p", { class: "hint", text: "The database is sorted by length, so the 16 proteins in a batch are nearly equal in length and little vector work is wasted on padding." }),
        el("div", { class: "scalar-lane" }, el("h4", { text: "The same steps, scalar" }),
          el("canvas", { id: "scalar-canvas", role: "img", "aria-label": "The query's own lane, filled one cell per step" }),
          el("p", { class: "hint", text: "Scalar code fills one cell per step, so after the same number of steps only one protein's table has advanced." })))));
  setTimeout(render, 0);
  return box;
}

function stopReplay() { if (L.replay && L.replay.timer) { clearInterval(L.replay.timer); L.replay.timer = null; } }

// ------------------------------------------------------------------ the five pairs
function renderPair() {
  const id = L.pair, r = L.results || {}, race = L.race, sch = L.schedules;
  const pair = PAIRS.find((p) => p.id === id);
  for (const b of $("pair-index").querySelectorAll("button")) b.setAttribute("aria-selected", String(b.dataset.pair === id));
  const runBtn = $("pair-run");
  runBtn.hidden = !pair.run;
  runBtn.disabled = !!L.task;
  runBtn.textContent = L.task ? "Running…" : (pair.run === "race" && race) || (pair.run === "schedules" && sch) ? "Run again" : "Run live";
  const A = $("side-a"), B = $("side-b"), C = $("pair-change"), X = $("pair-extra"), P = $("pair-prov");
  C.className = "change";
  X.replaceChildren();
  const step = (k) => (race && race.results[k]) || null;
  const raceProv = () => race
    ? [tag("live"), `${exampleName(race.example)} (${fmtInt(race.query.length)} residues) against ${race.db.name.replace("ecoli_x20", "E. coli × 20")} `
       + `(${fmtBig(race.db.residues)} residues; a copied benchmark database, not for biology), one run each, ${race.when}${race.on_ac === false ? ", on battery" : ""}. `]
    : [tag("live"), "Press Run live to time both sides on this laptop now. "];
  const e1 = r.e1, e1row = (k) => e1 && e1.rows[k];

  if (id === "simd") {
    $("pair-title").textContent = "Scalar vs SIMD, both on one core";
    $("pair-sub").textContent = "The only change: instead of scoring one database protein at a time, the vector unit scores 16 proteins in lock-step.";
    const a = step(0), b = step(1);
    const storedSimd = e1 ? e1row(0).median_s / e1row(1).median_s : null;
    const liveSimd = a && b ? speedup(a.timing.seconds, b.timing.seconds) : null;
    A.replaceChildren(...side("Scalar, 1 thread", a ? fmtSec(a.timing.seconds) : null, "", "One score cell per step: one protein after another.", [],
      ...laneStrip(1, 0, "1 of 16 lanes busy: one protein at a time")));
    B.replaceChildren(...side("SIMD, 1 thread", b ? [fmtSec(b.timing.seconds), liveSimd ? `${liveSimd.toFixed(1)}× faster` : ""] : null, "gain",
      "Sixteen proteins of equal length share one 256-bit vector register (16 × 16-bit lanes).",
      [storedSimd ? storedLine(`${storedSimd.toFixed(1)}× (M6 E1, Swiss-Prot, sustained, ${e1row(0).rounds} rounds)`) : null, whyLine(liveSimd, storedSimd, race)],
      ...laneStrip(16, 1, "16 of 16 lanes busy: all advance together")));
    C.innerHTML = `${ARROW}<span>16 lanes</span>`;
    const exAcc = $("lab-example").value;
    X.append(el("p", { class: "hint", text: "Loading the engine's trace of one batch…" }));
    loadSimd(exAcc).then((d) => { if (L.pair === "simd") X.replaceChildren(simdReplay(d)); })
      .catch((err) => { if (L.pair === "simd") X.replaceChildren(el("p", { text: `The batch trace could not be loaded: ${err.message}` })); });
    P.replaceChildren(...raceProv(), tag("stored"),
      e1 ? `M6 E1 (${dateOf(e1.sysinfo)}, ${e1.workload}, ${e1row(0).rounds} rounds): scalar ${e1row(0).gcups.toFixed(2)} GCUPS, SIMD ${e1row(1).gcups.toFixed(2)} GCUPS, ${(e1row(0).median_s / e1row(1).median_s).toFixed(1)}× faster. ` : "",
      tag("illus", "replay"), "Real cell values in the kernel's order, slowed down for viewing; the measured gain is about 13×, not 16×, because building the score profile and moving data also cost time.");
  } else if (id === "threads") {
    $("pair-title").textContent = "One thread vs one thread per physical core";
    $("pair-sub").textContent = "The only change: the same SIMD kernel runs on all 10 physical cores, each thread taking the next batch of 16 proteins when it is free.";
    const a = step(1), b = step(2);
    const storedThr = e1 ? e1row(1).median_s / e1row(2).median_s : null;
    const liveThr = a && b ? speedup(a.timing.seconds, b.timing.seconds) : null;
    A.replaceChildren(...side("SIMD, 1 thread", a ? fmtSec(a.timing.seconds) : null, "", "One P-core does all the work.", [], miniProgress(1), coreTiles(a ? a.thread_stats : [{ cpu: 0, items: 1 }])));
    B.replaceChildren(...side("SIMD, 10 threads", b ? [fmtSec(b.timing.seconds), liveThr ? `${liveThr.toFixed(1)}× faster` : ""] : null, "gain",
      "2 P-cores and 8 E-cores; tiles show how many batches each thread took.",
      [storedThr ? storedLine(`${storedThr.toFixed(2)}× (M6 E1, Swiss-Prot, sustained, ${e1row(1).rounds} rounds)`) : null, whyLine(liveThr, storedThr, race)],
      miniProgress(2), coreTiles(b ? b.thread_stats : null)));
    C.innerHTML = `${ARROW}<span>10 cores</span>`;
    if (r.e2) {
      X.append(el("h3", { text: "Which cores? Stored measurement" }),
        el("p", { text: `M6 E2: ${r.e2.workload}. 10 cores give far less than 10×: E-cores are slower than P-cores, and the laptop's 15 W power limit lowers every core's clock when all are busy.` }),
        hbars(r.e2.rows, "gcups", (x) => `${x.gcups.toFixed(1)} GCUPS`, (x) => x.cores === "physical"));
    }
    P.replaceChildren(...raceProv(), race ? "Same live race as the Scalar vs SIMD pair. " : "", tag("stored"),
      e1 ? `M6 E1 (${dateOf(e1.sysinfo)}): SIMD 1 thread ${e1row(1).gcups.toFixed(2)} GCUPS → 10 threads ${e1row(2).gcups.toFixed(2)} GCUPS, ${(e1row(1).median_s / e1row(2).median_s).toFixed(2)}× (sustained). ` : "",
      r.e2 ? `M6 E2 (${dateOf(r.e2.sysinfo)}, ${r.e2.rows[0].rounds} rounds). ` : "");
  } else if (id === "schedule") {
    $("pair-title").textContent = "Static vs dynamic scheduling";
    $("pair-sub").textContent = "The only change: how batches are handed to threads. Static gives each thread an equal share up front; dynamic hands out the next batch to whichever thread is free.";
    const a = sch && sch.results[0], b = sch && sch.results[1];
    const scale = a && b ? Math.max(a.timing.seconds, b.timing.seconds) : 1;
    const e3s = r.e3 && r.e3.rows.find((x) => x.policy === "static"), e3d = r.e3 && r.e3.rows.find((x) => x.policy === "dynamic,1");
    A.replaceChildren(...side("Static: equal shares up front", a ? fmtSec(a.timing.seconds) : null, "", "Fast P-cores finish their share early and sit idle while E-cores catch up.", [],
      miniProgress(0), a ? timeline(a, scale) : el("span")));
    B.replaceChildren(...side("Dynamic: one batch at a time", b ? [fmtSec(b.timing.seconds), a ? `${Math.round(100 * (1 - b.timing.seconds / a.timing.seconds))}% less time` : ""] : null, "gain",
      "Whichever thread is free takes the next batch, so every core stays busy and all finish together.",
      [e3s && e3d ? storedLine(`${Math.round(100 * (1 - e3d.median_s / e3s.median_s))}% less time than static (M6 E3, Swiss-Prot, ${e3d.rounds} rounds)`) : null],
      miniProgress(1), b ? timeline(b, scale) : el("span")));
    C.innerHTML = `${ARROW}<span>schedule</span>`;
    if (r.e3) X.append(el("h3", { text: "All seven policies, stored" }),
      el("p", { text: `M6 E3: ${r.e3.workload}. Extra time against the best policy (median of ${r.e3.rows[0].rounds} rounds); differences within a few percent are ties.` }),
      hbars(r.e3.rows.map((x) => ({ ...x, label: POLICY[x.policy] || x.policy })), "median_s", (x) => `+${(100 * x.regret).toFixed(1)}%`, (x) => x.policy === "dynamic,1" || x.policy === "static"));
    P.replaceChildren(...(sch ? [tag("live"), `${exampleName(sch.example)} against E. coli × 20, one traced run each, ${sch.when}${sch.on_ac === false ? ", on battery" : ""}; bars are the time each thread spent computing batches. `]
      : [tag("live"), "Press Run live for two traced runs. "]),
      ...(r.e3 ? [tag("stored"), `M6 E3 (${dateOf(r.e3.sysinfo)}).`] : []));
  } else if (id === "power") {
    const e4 = r.e4;
    const phys = e4 && e4.series.find((s) => s.cores === "physical");
    const med = (pts) => { const v = pts.map((p) => p[1]).sort((x, y) => x - y); return v[Math.floor(v.length / 2)]; };
    const early = phys ? med(phys.points.filter((p) => p[0] < 5)) : null, late = phys ? med(phys.points.filter((p) => p[0] >= 40)) : null;
    const one = e4 && e4.series.find((s) => s.cores === "1");
    const oneEarly = one ? med(one.points.filter((p) => p[0] < 5)) : null, oneLate = one ? med(one.points.filter((p) => p[0] >= 40)) : null;
    $("pair-title").textContent = "Burst vs sustained: why not 12× faster";
    $("pair-sub").textContent = "Nothing in the code changes here. The laptop may draw more power for a short burst; after that a long-term limit lowers the clock of every busy core.";
    const pw = S.system.info.power || {};
    const lim = pw["intel-rapl-mmio:0_long_term_uw"], win = pw["intel-rapl-mmio:0_window_us"];
    A.replaceChildren(...side("The first seconds", early ? `${early.toFixed(1)} GCUPS` : "—", "", "From idle, all 10 cores run near full clock.", [],
      el("p", { class: "lanes-cap", text: "Median of the searches in the first 5 s" })));
    B.replaceChildren(...side("Sustained", late ? [`${late.toFixed(1)} GCUPS`, `−${Math.round(100 * (1 - late / early))}%`] : "—", "loss",
      lim ? `A ${(lim / 1e6).toFixed(0)} W long-term limit (read from this laptop now${win ? `, averaged over ${(win / 1e6).toFixed(0)} s` : ""}) takes over.` : "The long-term power limit takes over.", [],
      el("p", { class: "lanes-cap", text: "Median of the searches after 40 s" })));
    C.className = "change neutral";
    C.innerHTML = `${ARROW}<span>${lim ? `${(lim / 1e6).toFixed(0)} W limit` : "power limit"}</span>`;
    if (e4) {
      X.append(el("h3", { text: "Speed of each repeated search, from idle" }),
        el("p", { text: `M6 E4: ${e4.workload}. With 10 cores busy the speed falls about 17 s after the start`
          + (oneEarly ? `; one thread falls much less (${oneEarly.toFixed(1)} to ${oneLate.toFixed(1)} GCUPS, −${Math.round(100 * (1 - oneLate / oneEarly))}%), because a single core stays closer to the power budget.` : ".") }),
        lineChart(e4.series.filter((s) => s.cores !== "all").map((s) => ({ label: s.label, points: s.points, colour: s.cores === "physical" ? css("--accent") : css("--ink-2") })),
          { label: "GCUPS of each repeated search over time: one per physical core drops after about 17 seconds", yLabel: "GCUPS" }));
      if (r.e8b && r.e2) {
        const rows = r.e8b.rows.map((b) => { const s = r.e2.rows.find((x) => x.cores === b.cores); return { label: b.label, burst: b.gcups, sustained: s ? s.gcups : null }; });
        const t = el("table", { class: "data-table" }, el("thead", {}, el("tr", {}, el("th", { text: "Thread set" }), el("th", { text: "Burst (E8b)" }), el("th", { text: "Sustained (E2)" }))),
          el("tbody", {}, rows.map((x) => el("tr", {}, el("td", { text: x.label }), el("td", { text: `${x.burst.toFixed(1)}` }), el("td", { text: x.sustained ? x.sustained.toFixed(1) : "—" })))));
        X.append(el("h3", { text: "The same thread sets, burst and sustained (GCUPS)" }), el("p", { text: "The ranking stays the same; the gaps shrink once the power limit applies. E8b used one query of length 400, E2 three queries, so compare rows rather than columns exactly." }), t);
      }
    }
    P.replaceChildren(tag("stored"), e4 ? `M6 E4 (${dateOf(e4.sysinfo)}). ` : "", r.e8b ? `E8b (${dateOf(r.e8b.sysinfo)}). ` : "",
      tag("live"), "The power limit above is read from this laptop's powercap interface.");
  } else if (id === "parasail") {
    const e6 = r.e6;
    $("pair-title").textContent = "Parasail vs HySW, one core each";
    $("pair-sub").textContent = "Same machine, same core, same scores. Parasail is a respected library written with hand-tuned vector intrinsics; HySW's kernel is plain C that the compiler vectorises.";
    A.replaceChildren(...side("Parasail 1.3.4, 1 thread", e6 ? fmtSec(e6.parasail_total_s) : "—", "", "Striped SIMD kernel with a query profile, called once per protein from Python.", []));
    B.replaceChildren(...side("HySW, 1 thread", e6 ? [fmtSec(e6.hysw_total_s), `${(e6.parasail_total_s / e6.hysw_total_s).toFixed(2)}×`] : "—", "gain",
      "Batches of 16 length-sorted proteins, one per lane.", []));
    C.className = "change neutral";
    C.innerHTML = `${ARROW}<span>same scores</span>`;
    const ok = (txt) => el("li", {}, (() => { const s = document.createElementNS("http://www.w3.org/2000/svg", "svg"); s.setAttribute("viewBox", "0 0 16 16"); s.innerHTML = '<path d="M3 8.5 L6.5 12 L13 4.5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>'; return s; })(), el("span", { text: txt }));
    X.append(el("div", { class: "two-col" },
      el("div", {}, el("h3", { text: "Correctness, checked against Parasail" }),
        el("ul", { class: "checks" }, (r.verification || []).map((v) => ok(`${fmtInt(v.identical)} of ${fmtInt(v.pairs)} scores identical: ${v.database.includes("sprot") ? "all of Swiss-Prot, 2 queries" : "E. coli proteome, 8 queries"} (${v.date.slice(0, 10)})`))),
        el("p", { class: "hint", text: "Every score of every database protein compared, not a sample. Run again with tests/run_tests.sh." })),
      e6 ? el("div", {}, el("h3", { text: "Time per query length (seconds)" }),
        el("table", { class: "data-table" }, el("thead", {}, el("tr", {}, el("th", { text: "Query length" }), el("th", { text: "Parasail" }), el("th", { text: "HySW" }), el("th", { text: "Ratio" }))),
          el("tbody", {}, e6.per_length.map((x) => el("tr", {}, el("td", { text: x.length }), el("td", { text: x.parasail_s.toFixed(2) }), el("td", { text: x.hysw_s.toFixed(2) }), el("td", { text: `${(x.parasail_s / x.hysw_s).toFixed(2)}×` }))))),
        el("p", { class: "hint", text: e6.note })) : el("div")));
    P.replaceChildren(tag("stored"), e6 ? `M6 E6 (${dateOf(e6.sysinfo)}, ${e6.workload}). Parasail's time includes Python call overhead, so this is a yardstick, not a race the libraries were built for.` : "");
  }
  if (L.pair === "simd" && L.replay && L.replay.render) setTimeout(L.replay.render, 0);
}

// ------------------------------------------------------------------ live runs
async function runPair() {
  const pair = PAIRS.find((p) => p.id === L.pair);
  if (!pair.run || L.task) return;
  const example = $("lab-example").value;
  $("pair-status").textContent = "";
  let data;
  try {
    data = await api(`/api/lab/${pair.run}`, { example });
  } catch (err) { $("pair-status").textContent = err.message; return; }
  L.task = data.task;
  L.progress = {};
  renderPair();
  $("pair-status").textContent = "Running on this laptop…";
  const es = new EventSource(`/api/jobs/${data.task.id}/events`);
  es.onmessage = (m) => {
    const ev = JSON.parse(m.data);
    if (ev.type === "progress") {
      L.progress[ev.step] = ev;
      const bar = document.querySelector(`#mp-${ev.step} i`);
      if (bar) bar.style.width = `${(100 * ev.done) / ev.total}%`;
      for (const f of document.querySelectorAll(`.lanes[data-step="${ev.step}"] i:not(.idle) b`)) f.style.height = `${(100 * ev.done) / ev.total}%`;
      $("pair-status").textContent = `${data.task.steps[ev.step]}: ${Math.round((100 * ev.done) / ev.total)}%`;
    }
  };
  const finish = async () => {
    es.close();
    const t = await api(`/api/lab/tasks/${data.task.id}`).catch(() => null);
    L.task = null;
    if (t && t.task.state === "done") {
      const rec = { ...t.task, db: t.database, example, when: new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }) };
      if (pair.run === "race") L.race = rec; else L.schedules = rec;
      $("pair-status").textContent = "";
    } else {
      $("pair-status").textContent = t ? (t.task.state === "cancelled" ? "Cancelled." : `Failed: ${t.task.error}`) : "Lost contact with the server.";
    }
    renderPair();
  };
  es.addEventListener("end", finish);
  es.onerror = () => { es.close(); setTimeout(finish, 300); };
}

// ------------------------------------------------------------------ start
function initLab() {
  const idx = $("pair-index");
  for (const p of PAIRS) {
    idx.append(el("button", { type: "button", role: "tab", "data-pair": p.id, "aria-selected": "false", "aria-controls": "pair-panel", text: p.label,
      onclick: () => { stopReplay(); L.pair = p.id; renderPair(); } }));
  }
  idx.addEventListener("keydown", (e) => {
    if (e.key !== "ArrowRight" && e.key !== "ArrowLeft") return;
    const i = PAIRS.findIndex((p) => p.id === L.pair), n = PAIRS[(i + (e.key === "ArrowRight" ? 1 : PAIRS.length - 1)) % PAIRS.length];
    stopReplay(); L.pair = n.id; renderPair();
    idx.querySelector(`[data-pair="${n.id}"]`).focus();
  });
  for (const ex of S.examples) $("lab-example").append(el("option", { value: ex.accession, text: `${exampleName(ex.accession)} (${ex.length} residues)` }));
  $("lab-example").addEventListener("change", () => { stopReplay(); L.replay = null; renderPair(); });
  $("pair-run").addEventListener("click", runPair);
  api("/api/lab/results").then((r) => { L.results = r; renderPair(); }).catch(() => renderPair());
}
