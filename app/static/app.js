// HySW front end: plain JavaScript, no framework. Every number shown comes from the engine's output or
// from stored benchmark results; the one estimate (scalar time) is labelled as such.
"use strict";

const $ = (id) => document.getElementById(id);
const S = { system: null, dbs: [], examples: [], job: null, result: null, selected: null, es: null, threadCpus: [], maxItems: 1 };
const SET_LABEL = { physical: "One per physical core", all: "All logical CPUs", e: "E-cores only", p: "P-cores only", "1": "One thread" };

function el(tag, attrs = {}, ...kids) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (k === "class") n.className = v;
    else if (k === "text") n.textContent = v;
    else if (k.startsWith("on")) n.addEventListener(k.slice(2), v);
    else n.setAttribute(k, v === true ? "" : v);
  }
  for (const c of kids.flat()) if (c !== null && c !== undefined) n.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return n;
}

const fmtInt = (n) => Math.round(n).toLocaleString("en-US");
function fmtBig(n) {
  if (n >= 1e12) return (n / 1e12).toFixed(2) + " trillion";
  if (n >= 1e9) return (n / 1e9).toFixed(2) + " billion";
  if (n >= 1e6) return (n / 1e6).toFixed(1) + " million";
  return fmtInt(n);
}
function fmtSec(s) {
  if (s < 0.001) return (s * 1e6).toFixed(0) + " µs";
  if (s < 1) return (s * 1000).toFixed(s < 0.1 ? 1 : 0) + " ms";
  if (s < 90) return s.toFixed(s < 10 ? 2 : 1) + " s";
  if (s < 5400) return (s / 60).toFixed(1) + " min";
  return (s / 3600).toFixed(1) + " h";
}
const pct = (x) => (100 * x).toFixed(1) + "%";

async function api(path, body) {
  const opt = body === undefined ? {} : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) };
  const r = await fetch(path, opt);
  let data = null;
  try { data = await r.json(); } catch { /* not JSON */ }
  if (!r.ok) throw new Error((data && data.error) || `Request failed (${r.status})`);
  return data;
}

function showMsg(node, text, kind = "") {
  node.hidden = !text;
  node.className = "msg" + (kind ? " " + kind : "");
  node.textContent = text || "";
}

// ------------------------------------------------------------------ views
function setView(name) {
  for (const b of document.querySelectorAll(".tab")) {
    if (b.dataset.view === name) b.setAttribute("aria-current", "page"); else b.removeAttribute("aria-current");
  }
  for (const v of ["search", "lab", "setup"]) $(`view-${v}`).hidden = name !== v;
  if (name === "lab" && typeof renderPair === "function" && L.results) renderPair();
  if (name !== "lab" && typeof stopReplay === "function") stopReplay();
}

// ------------------------------------------------------------------ query input
function countLetters(text) {
  let t = text.trim();
  if (t.startsWith(">")) {
    const lines = t.split(/\r?\n/);
    if (lines.slice(1).some((l) => l.startsWith(">"))) return { n: 0, note: "One sequence at a time" };
    t = lines.slice(1).join("");
  }
  const letters = t.replace(/[^A-Za-z]/g, "").toUpperCase();
  const n = letters.length;
  if (!n) return { n, note: "Paste letters or one FASTA record" };
  const nuc = [...letters].filter((c) => "ACGTUN".includes(c)).length;
  if (n >= 20 && nuc >= 0.9 * n) return { n, note: `${fmtInt(n)} letters: looks like DNA, not protein` };
  if (n < S.limits.min_length) return { n, note: `${n} letters: at least ${S.limits.min_length} needed` };
  if (n > S.limits.max_length) return { n, note: `${fmtInt(n)} letters: limit is ${fmtInt(S.limits.max_length)}` };
  return { n, note: `${fmtInt(n)} letters` };
}
function updateQinfo() { $("qinfo").textContent = countLetters($("query").value).note; }

function wrap60(seq) { return seq.match(/.{1,60}/g).join("\n"); }

function setupExamples() {
  const box = $("examples");
  for (const ex of S.examples) {
    const short = ex.header.split(" OS=")[0].replace(/^sp\|[^|]+\|\S+\s*/, "");
    box.append(el("button", {
      type: "button", class: "chip", title: `${ex.note} (${ex.length} residues, ${ex.accession})`, text: short,
      onclick: () => { $("query").value = `>${ex.header}\n${wrap60(ex.sequence)}`; updateQinfo(); showMsg($("form-msg"), ""); },
    }));
  }
}

$("file").addEventListener("change", (e) => {
  const f = e.target.files[0];
  e.target.value = "";
  if (!f) return;
  if (f.size > 512 * 1024) { showMsg($("form-msg"), "That file is larger than 512 KB; HySW takes one protein of at most 5,000 letters."); return; }
  const rd = new FileReader();
  rd.onload = () => { $("query").value = rd.result; updateQinfo(); showMsg($("form-msg"), ""); };
  rd.readAsText(f);
});
$("query").addEventListener("input", updateQinfo);

function syncModeCores() {
  const threaded = $("mode").value === "auto";
  $("cores").disabled = !threaded;
  $("adv-note").textContent = threaded
    ? ($("cores").value === "auto" ? "Auto uses one thread per physical core, the fastest set in the benchmarks (M6 E2, E8)."
                                   : `Using: ${SET_LABEL[$("cores").value]}.`)
    : "One-thread modes ignore the core setting. Scalar is about 13× slower than SIMD on one thread (M6 E1).";
}
$("mode").addEventListener("change", syncModeCores);
$("cores").addEventListener("change", syncModeCores);

// ------------------------------------------------------------------ core strip
function threadSetName() {
  if ($("mode").value !== "auto") return "1";
  const c = $("cores").value;
  return c === "auto" ? "physical" : c;
}

function buildCores(activeSet) {
  const info = S.system.info;
  const used = info.thread_sets[activeSet] || [];
  S.threadCpus = used;
  const strip = $("cores-strip");
  strip.replaceChildren();
  for (const c of info.cpus) {
    const t = used.indexOf(c.cpu);
    const type = (c.type || "?").toLowerCase();
    const tile = el("div", { class: `core ${type}${t < 0 ? " off" : ""}`, id: `core-${c.cpu}`,
      title: `cpu${c.cpu}: ${c.type === "P" ? "P-core" : c.type === "E" ? "E-core" : "core"} ${c.core}${t < 0 ? " (not used)" : ""}` },
      el("div", { class: "fill" }),
      el("span", { class: "lbl" }, el("b", { text: `cpu${c.cpu}` }), " ", c.type),
      el("span", { class: "cnt", text: t < 0 ? "—" : "0" }));
    strip.append(tile);
  }
}

function updateCores(items, unit, final) {
  const prev = S.prevItems || [];
  const max = Math.max(1, ...items);
  items.forEach((n, t) => {
    const tile = $(`core-${S.threadCpus[t]}`);
    if (!tile) return;
    tile.querySelector(".fill").style.height = `${(100 * n) / max}%`;
    tile.querySelector(".cnt").textContent = `${fmtInt(n)} ${unit}`;
    tile.classList.toggle("busy", !final && n > (prev[t] || 0));
  });
  S.prevItems = items.slice();
}

function setProgress(done, total, elapsed) {
  const p = total ? (100 * done) / total : 0;
  $("bar").style.width = `${p}%`;
  document.querySelector(".progress").setAttribute("aria-valuenow", Math.round(p));
  $("pct").textContent = total ? `${p.toFixed(0)}% of the database` : "Idle";
  $("elapsed").textContent = elapsed != null ? fmtSec(elapsed) : "";
}

// ------------------------------------------------------------------ search
$("search-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  if (S.job) return;
  showMsg($("form-msg"), "");
  const body = { query: $("query").value, database: $("database").value, top_k: Number($("topk").value),
                 mode: $("mode").value, cores: $("cores").value };
  const setName = threadSetName();
  let data;
  try {
    data = await api("/api/search", body);
  } catch (err) {
    showMsg($("form-msg"), err.message);
    return;
  }
  S.job = data.job;
  S.prevItems = [];
  S.unit = body.mode === "scalar" ? "seqs" : "batches";
  buildCores(setName);
  setProgress(0, 1, 0);
  $("pct").textContent = "Starting…";
  $("go").disabled = true;
  $("go").textContent = "Searching…";
  $("cancel").hidden = false;
  $("cancel").disabled = false;
  if (S.job.warnings && S.job.warnings.length) showMsg($("form-msg"), S.job.warnings.join(" "), "warn");
  follow(S.job.id);
});

function follow(id) {
  const es = new EventSource(`/api/jobs/${id}/events`);
  S.es = es;
  es.onmessage = (m) => {
    const ev = JSON.parse(m.data);
    setProgress(ev.done, ev.total, ev.elapsed);
    updateCores(ev.thread_items, S.unit, false);
  };
  es.addEventListener("end", () => { es.close(); finish(id); });
  es.onerror = () => { es.close(); poll(id); };
}

async function poll(id) {
  for (;;) {
    const j = await api(`/api/jobs/${id}`).catch(() => null);
    if (j && j.state !== "running") return finish(id, j);
    await new Promise((r) => setTimeout(r, 400));
  }
}

async function finish(id, j) {
  if (!j) j = await api(`/api/jobs/${id}`).catch((err) => ({ state: "error", error: err.message }));
  S.job = null;
  $("go").disabled = false;
  $("go").textContent = "Search";
  $("cancel").hidden = true;
  if (j.progress) updateCores(j.progress.thread_items, S.unit, true);
  else for (const t of document.querySelectorAll(".core.busy")) t.classList.remove("busy");
  if (j.state === "done") {
    const tm = j.result.queries[0].timing;
    setProgress(1, 1, tm.seconds);
    $("pct").textContent = "Done";
    renderResults(j);
  } else if (j.state === "cancelled") {
    $("pct").textContent = "Cancelled";
    showMsg($("form-msg"), "Search cancelled.", "info");
  } else {
    $("pct").textContent = "Failed";
    showMsg($("form-msg"), `The search failed: ${j.error || "unknown error"}`);
  }
}

$("cancel").addEventListener("click", async () => {
  if (!S.job) return;
  $("cancel").disabled = true;
  $("pct").textContent = "Cancelling…";
  await api(`/api/jobs/${S.job.id}/cancel`, {}).catch(() => {});
});

// ------------------------------------------------------------------ results
function stat(k, v, s) {
  return el("div", { class: "stat" }, el("div", { class: "k", text: k }), el("div", { class: "v", text: v }), el("div", { class: "s", text: s }));
}

function renderResults(j) {
  const r = j.result, q = r.queries[0], tm = q.timing, st = r.settings;
  S.result = j;
  $("results").hidden = false;
  const engineName = st.kernel === "scalar" ? "Scalar" : `SIMD × ${st.lanes}`;
  const coresSpec = st.cores_spec === "auto" ? "physical (auto)" : st.cores_spec;
  $("summary").replaceChildren(
    stat("Search time", fmtSec(tm.seconds), `scoring all ${fmtInt(r.database.sequences)} proteins; alignments +${fmtSec(tm.align_seconds)}`),
    stat("Cells computed", fmtBig(tm.cells), `${fmtInt(q.query.length)} query × ${fmtBig(r.database.residues)} database residues`),
    stat("Speed", `${tm.gcups.toFixed(tm.gcups < 10 ? 2 : 1)} GCUPS`, "billion score-matrix cells per second"),
    stat("Engine", `${engineName}, ${st.threads} thread${st.threads > 1 ? "s" : ""}`,
         `cores: ${st.threads > 1 ? coresSpec : "one P-core"} · schedule ${st.policy}`),
  );
  const est = j.scalar_estimate;
  $("estimate").textContent = est && st.kernel !== "scalar"
    ? `Estimate: a scalar, one-thread search of the same ${fmtBig(tm.cells)} cells would take about ${fmtSec(est.seconds)} `
      + `(cells ÷ ${est.gcups.toFixed(2)} GCUPS, the ${est.source}). This search was ${(est.seconds / tm.seconds).toFixed(0)}× faster than that estimate.`
    : "";
  const warn = [...(j.warnings || [])];
  if (q.query.unknown_letters) warn.push(`${q.query.unknown_letters} letter(s) scored as unknown (X).`);
  showMsg($("warnings"), warn.join(" "), "warn");
  $("exp-csv").href = `/api/jobs/${j.id}/hits.csv`;
  $("exp-json").href = `/api/jobs/${j.id}/report.json`;

  const tbody = $("hits").tBodies[0];
  tbody.replaceChildren();
  const top = Math.max(1, ...q.hits.map((h) => h.score));
  for (const h of q.hits) {
    const tr = el("tr", { tabindex: "0", "aria-selected": "false", "data-rank": h.rank },
      el("td", { class: "num", text: h.rank }),
      el("td", { class: "acc" }, el("a", { href: `https://www.uniprot.org/uniprotkb/${encodeURIComponent(h.accession)}/entry`,
        target: "_blank", rel: "noopener noreferrer", title: "Open in UniProt (internet)", text: h.accession })),
      el("td", { text: h.name || h.header }),
      el("td", { class: "org", text: h.organism }),
      el("td", { class: "num", text: fmtInt(h.length) }),
      el("td", {}, el("div", { class: "scorecell" }, el("div", { class: "track" }, (() => {
        const b = el("div", { class: "sbar" }); b.style.width = `${(100 * h.score) / top}%`; return b; })()),
        el("span", { text: fmtInt(h.score) }))),
      el("td", { class: "num", text: h.identity != null ? pct(h.identity) : "—" }),
      el("td", { class: "num", text: h.coverage != null ? pct(h.coverage) : "—" }));
    tr.addEventListener("click", (e) => { if (e.target.tagName !== "A") selectHit(h.rank); });
    tr.addEventListener("keydown", (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); selectHit(h.rank); } });
    tbody.append(tr);
  }
  if (q.hits.length) selectHit(q.hits[0].rank, false);
  else $("detail").hidden = true;
}

function selectHit(rank, scroll = true) {
  const j = S.result, q = j.result.queries[0];
  const h = q.hits.find((x) => x.rank === rank);
  S.selected = h;
  for (const tr of $("hits").tBodies[0].rows) tr.setAttribute("aria-selected", String(Number(tr.dataset.rank) === rank));
  $("exp-aln").href = `/api/jobs/${j.id}/alignment/${rank}.txt`;
  $("detail").hidden = false;
  $("detail-title").textContent = `Hit ${rank}: ${h.name || h.accession}${h.organism ? ` (${h.organism})` : ""}`;
  const a = h.alignment;
  if (!a) {
    $("detail-stats").replaceChildren();
    $("aln").replaceChildren(el("p", { class: "hint", text: "No alignment for this hit." }));
  } else {
    $("detail-stats").replaceChildren(
      el("span", {}, "Score ", el("b", { text: fmtInt(h.score) })),
      el("span", {}, "Identities ", el("b", { text: `${a.identities}/${a.length}` }), ` (${pct(h.identity)})`),
      el("span", {}, "Positives ", el("b", { text: `${a.positives}/${a.length}` }), ` (${pct(h.positives)})`),
      el("span", {}, "Gaps ", el("b", { text: fmtInt(a.gaps) })),
      el("span", {}, `Query ${a.q_start}–${a.q_end} of ${q.query.length}`),
      el("span", {}, `Hit ${a.t_start}–${a.t_end} of ${fmtInt(h.length)}`));
    $("aln").replaceChildren(alignmentBlocks(a));
  }
  S.map = { len: h.length, classes: alignmentClasses(h), plddt: null };
  drawMap();
  history.replaceState(null, "", `#job=${j.id}&hit=${rank}`);
  loadStructure(h, false);
  if (scroll) $("detail").scrollIntoView({ behavior: "smooth", block: "nearest" });
}

// Each residue span carries its hit position (data-t) so the alignment and the 3D model can point at each other.
function alignmentBlocks(a, width = 60) {
  const box = el("div", { class: "aln", role: "img", "aria-label": "Aligned sequences: query above, hit below" });
  let qi = a.q_start, ti = a.t_start;
  for (let i = 0; i < a.length; i += width) {
    const qs = a.q.slice(i, i + width), ms = a.mid.slice(i, i + width), ts = a.t.slice(i, i + width);
    const qn = [...qs].filter((c) => c !== "-").length, tn = [...ts].filter((c) => c !== "-").length;
    let tpos = ti;
    const hitPos = [...ts].map((c) => (c === "-" ? null : tpos++));
    const line = (who, start, seq, end, kind) => {
      const row = el("div", {}, el("span", { class: "who", text: who }), el("span", { class: "pos", text: start }), "  ");
      [...seq].forEach((c, k) => {
        const m = ms[k];
        let cls = "r";
        if (kind !== "mid") cls += c === "-" ? " gp" : m === "+" ? " ps" : m !== " " && m !== undefined ? " id" : "";
        const span = el("span", { class: cls, text: c });
        if (hitPos[k] !== null) span.dataset.t = hitPos[k];
        row.append(span);
      });
      if (end !== null) row.append("  ", el("span", { class: "pos", text: end }));
      return row;
    };
    box.append(el("div", { class: "blk" },
      line("Query", qi, qs, qi + qn - 1, "seq"),
      line("", "", ms, null, "mid"),
      line("Hit", ti, ts, ti + tn - 1, "seq")));
    qi += qn; ti += tn;
  }
  box.addEventListener("pointerover", (e) => { const t = e.target.dataset && e.target.dataset.t; markResidue(t ? Number(t) : null, "aln"); });
  box.addEventListener("pointerleave", () => markResidue(null, "aln"));
  return box;
}

// ------------------------------------------------------------------ protein map (2D, works without WebGL)
// Alignment class of every hit residue, computed from the alignment strings alone (no structure needed).
function alignmentClasses(h) {
  const a = h.alignment, cls = new Map();
  if (!a) return cls;
  let pos = a.t_start;
  for (let i = 0; i < a.length; i++) {
    if (a.t[i] === "-") continue;
    const m = a.mid[i];
    cls.set(pos, a.q[i] === "-" ? "mm" : m === "+" ? "ps" : m !== " " ? "id" : "mm");
    pos++;
  }
  return cls;
}

function runs(len, f) {                   // run-length encode residues 1..len by a key function
  const out = [];
  let start = 1, key = f(1);
  for (let r = 2; r <= len + 1; r++) {
    const k = r <= len ? f(r) : Symbol("end");
    if (k !== key) { out.push([start, r - 1, key]); start = r; key = k; }
  }
  return out;
}

function segments(len, list, colourOf) {
  return list.map(([r0, r1, k]) => {
    const d = el("div", { class: "pm-seg" });
    d.style.left = `${(100 * (r0 - 1)) / len}%`;
    d.style.width = `${(100 * (r1 - r0 + 1)) / len}%`;
    d.style.background = colourOf(k);
    return d;
  });
}

// Whole hit protein as two tracks (alignment class, AlphaFold confidence); positions in % of its length.
function drawMap() {
  const m = S.map, a = S.selected.alignment, len = m.len;
  const colour = { id: css("--st-id"), ps: css("--st-ps"), mm: css("--st-mm"), out: css("--st-rest") };
  const alnTrack = el("div", { class: "pm-trk" }, segments(len, runs(len, (r) => m.classes.get(r) || "out"), (k) => colour[k]));
  let confTrack;
  if (m.plddt) {
    const v = m.plddt.values, first = m.plddt.first;
    const key = (r) => { const b = v[r - first]; return b == null ? "none" : b >= 90 ? "--af-vh" : b >= 70 ? "--af-c" : b >= 50 ? "--af-l" : "--af-vl"; };
    confTrack = el("div", { class: "pm-trk" }, segments(len, runs(len, key).filter((x) => x[2] !== "none"), (k) => css(k)));
  } else {
    confTrack = el("div", { class: "pm-trk pm-empty", text: "loads with the structure" });
  }
  const axis = el("div", { class: "pm-axis" }, el("span", { text: "1" }));
  if (a) {
    const mark = (r, t) => { const s = el("span", { class: "pm-tick", text: t }); s.style.left = `${(100 * (r - 0.5)) / len}%`; return s; };
    if (a.t_start > len * 0.08) axis.append(mark(a.t_start, a.t_start));
    if (a.t_end < len * 0.92) axis.append(mark(a.t_end, a.t_end));
  }
  axis.append(el("span", { class: "pm-end", text: fmtInt(len) }));
  const cur = el("div", { class: "pm-cur", id: "pmap-cursor", hidden: true });
  const body = el("div", { class: "pm-body" }, alnTrack, confTrack, cur, axis);
  const toResi = (e) => {
    const p = alnTrack.getBoundingClientRect();
    const r = Math.floor(((e.clientX - p.left) / p.width) * len) + 1;
    return r >= 1 && r <= len ? r : null;
  };
  body.addEventListener("pointermove", (e) => markResidue(toResi(e), "map"));
  body.addEventListener("pointerleave", () => markResidue(null, "map"));
  const key = m.plddt ? el("div", { class: "pm-key" }, "Confidence (AlphaFold pLDDT):",
    ...[["--af-vh", "very high"], ["--af-c", "confident"], ["--af-l", "low"], ["--af-vl", "very low"]]
      .map(([c, t]) => { const s = el("span", { text: t }); s.style.setProperty("--c", css(c)); return s; })) : null;
  $("pmap").replaceChildren(el("div", { class: "pm-labels" }, el("span", { text: "Alignment" }), el("span", { text: "Confidence" })), body, key);
  $("pmap").setAttribute("aria-label", `Map of the hit protein, ${fmtInt(len)} residues; aligned region ${a ? `${a.t_start} to ${a.t_end}` : "none"}.`);
}

// ------------------------------------------------------------------ 3D structure (AlphaFold DB model, 3Dmol.js)
const V = { lib: null, viewer: null, model: null, mode: "alignment", data: null, marker: null, req: 0, allowFetch: false };

function loadLib() {
  if (!V.lib) {
    V.lib = new Promise((resolve, reject) => {
      const s = el("script", { src: "/static/vendor/3Dmol-min.js" });
      s.onload = () => resolve(window.$3Dmol);
      s.onerror = () => reject(new Error("The 3D viewer library could not be loaded."));
      document.head.append(s);
    });
  }
  return V.lib;
}

function viewerState(...nodes) { $("viewer-state").replaceChildren(...nodes); }

async function loadStructure(h, fetchAllowed) {
  const req = ++V.req;
  V.data = null;
  clearViewer();
  $("struct-legend").replaceChildren();
  $("struct-note").textContent = "";
  setModeButtons(false);
  viewerState(el("div", { class: "pulse" }), el("p", { text: fetchAllowed ? "Downloading the predicted structure from AlphaFold DB…" : "Looking for the predicted structure…" }));
  const url = `/api/jobs/${S.result.id}/structure/${h.rank}${fetchAllowed || V.allowFetch ? "" : "?cached_only=1"}`;
  let data;
  try {
    const r = await fetch(url);
    data = await r.json();
    if (req !== V.req) return;
    if (!r.ok) throw Object.assign(new Error(data.error || `Request failed (${r.status})`), { kind: data.kind });
  } catch (err) {
    if (req !== V.req) return;
    if (err.kind === "not_cached") {
      viewerState(el("p", { text: `The predicted structure of ${h.accession} is not on this computer yet.` }),
        el("button", { type: "button", class: "ghost", text: "Download from AlphaFold DB",
          onclick: () => { V.allowFetch = true; loadStructure(h, true); } }),
        el("p", { class: "hint", text: "Sends only the public accession; usually 0.1–1 MB, then cached. Other hits will then download when selected." }));
    } else {
      viewerState(el("p", { text: err.message }),
        err.kind === "offline" || err.kind === "refused" || err.kind === "failed"
          ? el("button", { type: "button", class: "ghost", text: "Try again", onclick: () => loadStructure(h, true) }) : null);
    }
    return;
  }
  try {
    const [$3D, pdb] = await Promise.all([loadLib(), fetch(data.model_url).then((r) => { if (!r.ok) throw new Error("model file missing"); return r.text(); })]);
    if (req !== V.req) return;
    V.data = data;
    S.map.plddt = data.plddt;
    drawMap();
    if (!hasWebGL()) {
      viewerState(el("p", { text: "This browser has 3D graphics (WebGL) turned off, so the model cannot be drawn here. "
        + "The map below shows the same alignment and confidence along the protein." }));
      applyColours();
      structureNote();
      return;
    }
    drawStructure($3D, pdb);
  } catch (err) {
    if (req === V.req) viewerState(el("p", { text: `The structure could not be shown: ${(err && err.message) || err}` }));
  }
}

function css(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }

function clearViewer() {
  if (V.viewer) { V.viewer.clear(); V.viewer.render(); }
  V.model = null; V.marker = null;
  $("viewer-hover").hidden = true;
}

function drawStructure($3D, pdb) {
  if (!V.viewer) {
    V.viewer = $3D.createViewer($("viewer"), { backgroundColor: css("--surface-2"), antialias: true });
    window.addEventListener("resize", () => { if (V.viewer) { V.viewer.resize(); V.viewer.render(); } });
  }
  V.viewer.clear();
  V.model = V.viewer.addModel(pdb, "pdb");
  viewerState();
  applyColours();
  V.viewer.setHoverable({ atom: "CA" }, true,
    (atom) => { markResidue(atom.resi, "3d"); },
    () => { markResidue(null, "3d"); });
  V.viewer.setHoverDuration(60);
  V.viewer.zoomTo();
  V.viewer.render();
  setModeButtons(true);
  structureNote();
}

function hasWebGL() {
  try { const c = document.createElement("canvas"); return !!(c.getContext("webgl2") || c.getContext("webgl")); }
  catch { return false; }
}

function structureNote() {
  const m = V.data.meta, map = V.data.mapping;
  const conf = m.globalMetricValue;
  const band = conf >= 90 ? "very high" : conf >= 70 ? "confident" : conf >= 50 ? "low" : "very low";
  $("struct-note").replaceChildren(
    `${m.entryId}, model v${m.latestVersion}; mean confidence (pLDDT) ${conf.toFixed(1)}, ${band}. `,
    map.ok ? "" : `${map.reason} `,
    el("a", { href: m.source, target: "_blank", rel: "noopener noreferrer", text: "AlphaFold DB entry" }),
    `. ${m.attribution}`);
}

function bandColour(b) { return b >= 90 ? css("--af-vh") : b >= 70 ? css("--af-c") : b >= 50 ? css("--af-l") : css("--af-vl"); }

function applyColours() {
  if (!V.data) return;
  const map = V.data.mapping;
  const v = V.model ? V.viewer : { setStyle() {}, render() {} };       // legend only when there is no 3D model
  if (V.mode === "plddt") {
    v.setStyle({}, { cartoon: { colorfunc: (atom) => bandColour(atom.b) } });
    $("struct-legend").replaceChildren(
      ...[["--af-vh", "very high (>90)"], ["--af-c", "confident (70–90)"], ["--af-l", "low (50–70)"], ["--af-vl", "very low (<50)"]]
        .map(([c, t]) => { const s = el("span", { text: t }); s.style.setProperty("--c", css(c)); return s; }));
  } else {
    v.setStyle({}, { cartoon: { color: css("--st-rest") } });
    const items = [];
    if (map.ok) {
      if (map.different.length) v.setStyle({ resi: map.different }, { cartoon: { color: css("--st-mm") } });
      if (map.similar.length) v.setStyle({ resi: map.similar }, { cartoon: { color: css("--st-ps") } });
      if (map.identical.length) v.setStyle({ resi: map.identical }, { cartoon: { color: css("--st-id") } });
      items.push(["--st-id", "identical"], ["--st-ps", "similar"], ["--st-mm", "different"]);
    }
    items.push(["--st-rest", map.ok ? "outside the alignment" : "whole protein (not highlighted)"]);
    $("struct-legend").replaceChildren(...items.map(([c, t]) => { const s = el("span", { text: t }); s.style.setProperty("--c", css(c)); return s; }));
  }
  v.render();
}

function setModeButtons(enabled) {
  for (const b of $("struct-mode").querySelectorAll("button")) {
    b.disabled = !enabled;
    b.setAttribute("aria-pressed", String(b.dataset.mode === V.mode));
  }
}
$("struct-mode").addEventListener("click", (e) => {
  const b = e.target.closest("button");
  if (!b || b.disabled) return;
  V.mode = b.dataset.mode;
  setModeButtons(true);
  applyColours();
});

// One residue marked in both views: a sphere on the model's C-alpha atom and an outline in the alignment.
function markResidue(resi, from) {
  for (const s of document.querySelectorAll(".aln .hot")) s.classList.remove("hot");
  const cur = $("pmap-cursor");
  if (cur) {
    const on = resi !== null && resi !== undefined && S.map;
    cur.hidden = !on;
    if (on) cur.style.left = `${(100 * (resi - 0.5)) / S.map.len}%`;
  }
  const tip = $("viewer-hover");
  if (V.marker && V.viewer) { V.viewer.removeShape(V.marker); V.marker = null; }
  if (resi === null || resi === undefined || !V.model) {
    tip.hidden = true;
    if (V.viewer) V.viewer.render();
    if (resi === null || resi === undefined) return;
  }
  for (const s of document.querySelectorAll(`.aln [data-t="${resi}"]`)) s.classList.add("hot");
  if (!V.model) return;
  const atom = V.model.selectedAtoms({ resi, atom: "CA" })[0];
  if (!atom) { tip.hidden = true; V.viewer.render(); return; }
  V.marker = V.viewer.addSphere({ center: { x: atom.x, y: atom.y, z: atom.z }, radius: 1.9, color: css("--st-mark"), opacity: 0.95 });
  V.viewer.render();
  tip.hidden = false;
  tip.textContent = `${atom.resn} ${resi}, confidence ${atom.b.toFixed(0)}`;
  if (from === "3d") {
    const target = document.querySelector(`.aln [data-t="${resi}"]`);
    if (target) target.scrollIntoView({ block: "nearest", inline: "nearest" });
  }
}

// ------------------------------------------------------------------ setup
function renderSetup() {
  const list = $("db-list");
  list.replaceChildren();
  for (const d of S.dbs) {
    list.append(el("div", { class: "db" },
      el("div", { class: "t" }, d.label, el("span", { class: `badge ${d.ready ? "ok" : "no"}`, text: d.ready ? "ready" : "not prepared" })),
      el("div", { class: "d", text: d.about }),
      d.ready ? el("div", { class: "d", text: `${fmtInt(d.sequences)} proteins, ${fmtBig(d.residues)} residues, longest ${fmtInt(d.longest)}` })
              : el("div", { class: "d", text: d.missing }),
      el("div", { class: "d", text: `Source: ${d.source}` })));
  }
  const i = S.system.info;
  const nP = i.cpus.filter((c) => c.type === "P").length, nE = i.cpus.filter((c) => c.type === "E").length;
  const pw = i.power || {};
  const lim = pw["intel-rapl-mmio:0_long_term_uw"] ?? pw["intel-rapl:0_long_term_uw"];
  const win = pw["intel-rapl-mmio:0_window_us"] ?? pw["intel-rapl:0_window_us"];
  const rows = [
    ["Processor", i.cpu_model],
    ["Logical CPUs", `${i.logical_cpus} (${nP} on P-cores, ${nE} on E-cores)`],
    ["Physical cores", `${i.thread_sets.physical.length}${i.hybrid ? " (hybrid: performance + efficiency cores)" : ""}`],
    ["SIMD", `${i.simd.avx2 ? "AVX2" : "no AVX2"}${i.simd.avx512bw ? ", AVX-512BW" : ""}; engine uses ${i.simd.lanes} lanes × ${i.simd.lane_bits}-bit`],
    ["Power profile", `${pw.platform_profile || "unknown"}, governor ${pw.governor || "unknown"}`],
    ["Long-term power limit", lim != null ? `${(lim / 1e6).toFixed(0)} W${win != null ? `, averaged over ${(win / 1e6).toFixed(0)} s` : ""}` : "not readable"],
    ["Compiler", i.compiler],
  ];
  $("hw").replaceChildren(...rows.flatMap(([k, v]) => [el("dt", { text: k }), el("dd", { text: v })]));
  if (S.system.calibration) renderCalibration(S.system.calibration, false);
}

function renderCalibration(c, apply) {
  const rows = c.sets.map((s) => el("tr", { class: s.name === c.chosen ? "chosen" : "" },
    el("td", { text: SET_LABEL[s.name] || s.name }), el("td", { class: "num", text: s.threads }),
    el("td", { class: "num", text: s.median_gcups.toFixed(1) }),
    el("td", { text: s.name === c.chosen ? "chosen" : "" })));
  $("calib").replaceChildren(el("table", { class: "calib" },
    el("thead", {}, el("tr", {}, el("th", { text: "Thread set" }), el("th", { class: "num", text: "Threads" }),
      el("th", { class: "num", text: "Median GCUPS" }), el("th", { text: "" }))),
    el("tbody", {}, rows)));
  const kept = c.chosen === c.default;
  let text = `Calibrated ${c.when.replace("T", " ").slice(0, 16)} on ${c.database} in ${c.seconds.toFixed(2)} s `
    + `(${c.rounds} rounds, query length ${c.query_length}). `;
  text += kept ? "The default (one thread per physical core) is kept." : `Chosen: ${SET_LABEL[c.chosen]}, more than ${Math.round(100 * c.margin)}% faster than the default.`;
  text += " Calibration trials are short, so their speeds run higher than a full search's (E8).";
  if (apply && !kept && $("cores").querySelector(`option[value="${c.chosen}"]`)) {
    $("cores").value = c.chosen;
    syncModeCores();
    text += " The Cores setting on the Search screen now uses it.";
  }
  showMsg($("calib-msg"), text, "info");
}

$("calibrate").addEventListener("click", async () => {
  const b = $("calibrate");
  b.disabled = true;
  b.textContent = "Calibrating…";
  showMsg($("calib-msg"), "");
  try {
    const out = await api("/api/calibrate", { database: $("database").value });
    renderCalibration(out.calibration, true);
  } catch (err) {
    showMsg($("calib-msg"), err.message);
  } finally {
    b.disabled = false;
    b.textContent = "Calibrate (about 1 second)";
  }
});

async function restoreFromHash() {
  const p = new URLSearchParams(location.hash.slice(1));
  const id = p.get("job");
  if (!id || !/^[0-9a-f]{12}$/.test(id)) return;
  const j = await api(`/api/jobs/${id}`).catch(() => null);
  if (!j || j.state !== "done") { history.replaceState(null, "", location.pathname); return; }
  S.unit = j.result.settings.kernel === "scalar" ? "seqs" : "batches";
  const setName = j.result.settings.threads === 1 ? "1" : (j.params.cores === "auto" ? "physical" : j.params.cores);
  buildCores(setName);
  updateCores(j.progress ? j.progress.thread_items : [], S.unit, true);
  setProgress(1, 1, j.result.queries[0].timing.seconds);
  $("pct").textContent = "Done";
  $("query").value = `>${j.query.header}\n${wrap60(j.query.sequence)}`;
  updateQinfo();
  renderResults(j);
  const rank = Number(p.get("hit"));
  if (rank && j.result.queries[0].hits.some((h) => h.rank === rank)) selectHit(rank, false);
}

// ------------------------------------------------------------------ start
for (const b of document.querySelectorAll(".tab")) b.addEventListener("click", () => setView(b.dataset.view));

(async function init() {
  try {
    const [sys, dbs, ex] = await Promise.all([api("/api/system"), api("/api/databases"), api("/api/examples")]);
    S.system = sys; S.limits = sys.limits; S.dbs = dbs.databases; S.examples = ex.examples;
    for (const d of S.dbs) {
      $("database").append(el("option", { value: d.id, disabled: !d.ready,
        text: d.ready ? `${d.label} (${fmtInt(d.sequences)} proteins)` : `${d.label} (not prepared)` }));
    }
    if (dbs.default) $("database").value = dbs.default;
    setupExamples();
    syncModeCores();
    buildCores(threadSetName());
    renderSetup();
    updateQinfo();
    if (typeof initLab === "function") initLab();
    await restoreFromHash();
  } catch (err) {
    showMsg($("form-msg"), `Could not reach the HySW server: ${err.message}`);
  }
})();
