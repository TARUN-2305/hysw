// Builds HySW_first_principles.pptx and preview.html (same layout spec) for visual QA.
const fs = require("fs"), path = require("path"), sharp = require("sharp"), pptxgen = require("pptxgenjs");
const { art } = require("./art.js");
const OUT = process.argv[2] || "HySW_first_principles.pptx";
const SHOT = path.resolve("img/shot.png");
const C = { ink: "1D2433", muted: "5B6475", soft: "EEF2F7", coral: "E4572E", teal: "17A398", blue: "3B6FD8",
  orange: "F28C28", yellow: "FFC857", lav: "8E7CC3", red: "D64545", white: "FFFFFF", night: "2B3550", grey: "C9D1DC" };
fs.mkdirSync("img", { recursive: true });

// ---- slide spec helpers --------------------------------------------------------------
const S = []; let cur;
const slide = (dark, notes) => { cur = { dark, notes, els: [] }; S.push(cur); return cur; };
const T = (text, o) => cur.els.push({ k: "t", text, ...o });
const IMG = (name, svgStr, x, y, w, h) => cur.els.push({ k: "i", name, svgStr, x, y, w, h });
const PIC = (file, x, y, w, h) => cur.els.push({ k: "p", file, x, y, w, h });
const BOX = (x, y, w, h, fill, o = {}) => cur.els.push({ k: "b", x, y, w, h, fill, ...o });
const BAR = (labels, vals, x, y, w, h, colors, unit) => cur.els.push({ k: "c", labels, vals, x, y, w, h, colors, unit });
const title = (s, dark) => T(s, { x: .6, y: .4, w: 8.8, h: .7, size: 30, bold: true, color: dark ? C.white : C.ink });
const body = (s, x, y, w, h, o = {}) => T(s, { x, y, w, h, size: 16, color: C.muted, ...o });
const cap = (s, x, y, w, h, o = {}) => T(s, { x, y, w, h, size: 12, color: C.muted, ...o });
const stat = (s, x, y, w, color, size = 54) => T(s, { x, y, w, h: size / 60, size, bold: true, color });
const kicker = (s, x, y, w, color = C.coral) => T(s, { x, y, w, h: .3, size: 13, bold: true, color, upper: true });

// ---- the story ---------------------------------------------------------------------------
// 1 Title
slide(true, "HySW asks one question from first principles: how fast can an ordinary laptop do an exact protein search, and what limits it? Every number in this deck is measured on this laptop unless it says estimate.");
kicker("CS372IA · Parallel Architecture", .6, .75, 5, C.yellow);
T("Finding a protein's relatives, exactly, on a laptop", { x: .6, y: 1.2, w: 5.0, h: 2.0, size: 36, bold: true, color: C.white });
body("HySW: Smith–Waterman search with SIMD and threads, measured", .6, 3.35, 4.6, .7, { color: C.grey });
IMG("title", art.title(), 5.5, 1.0, 4.0, 3.45);
cap("Swiss-Prot · 576,073 proteins · Intel i3-1220P", .6, 4.85, 5, .3, { color: C.grey });

// 2 Protein = string
slide(false, "Start from the object. A protein is a chain of amino acids; there are 20 kinds, so a protein can be written as a string over a 20-letter alphabet. Haemoglobin beta begins M V H L T P E E K and is 147 letters long.");
title("A protein is a string of 20 letters");
IMG("protein", art.protein(), .6, 1.55, 8.8, 2.0);
body("Each bead is one amino acid. There are 20 kinds, so a protein can be written as text.", .6, 3.85, 5.6, .7, { color: C.ink });
cap("Haemoglobin β begins M V H L T P E E K … and is 147 letters long", .6, 4.65, 6, .3);

// 3 The question
slide(false, "The user has one new protein and wants to know which known proteins resemble it, how closely, and where. The reference library is UniProtKB/Swiss-Prot: 576,073 reviewed proteins, 209 million letters.");
title("Which known proteins look like mine?");
IMG("query", art.query(), .5, 1.35, 5.0, 3.05);
stat("576,073", 5.9, 1.75, 3.6, C.ink, 48);
body("known proteins in Swiss-Prot, 209 million letters in total", 5.9, 2.65, 3.4, .8);
body("Similar proteins often share a job, so finding relatives tells you what yours might do.", 5.9, 3.55, 3.5, 1.0, { color: C.ink, size: 14 });

// 4 Alignment
slide(false, "Similarity is measured by aligning: slide two strings against each other, allowing gaps. Identical letters score high, chemically similar letters score a little, unlike letters score negative, and a gap costs a penalty. The best total is the similarity score.");
title("Similar means: line them up and score it");
IMG("align", art.align(), .7, 1.4, 8.6, 2.3);
const leg = [[C.teal, "Same letter: reward"], [C.yellow, "Similar letter: small reward"], [C.red, "Gap: penalty"]];
leg.forEach(([c, s], i) => { BOX(.75 + i * 3.0, 4.12, .26, .26, c, { round: true }); T(s, { x: 1.12 + i * 3.0, y: 4.1, w: 2.6, h: .3, size: 14, color: C.ink }); });
cap("Rewards come from the BLOSUM62 table; opening a gap costs more than making it longer.", .75, 4.75, 8.5, .3);

// 5 Grid
slide(false, "Smith and Waterman (1981) showed the best local alignment can be found exactly with a table. Put one protein along the top and the other down the side. Each cell takes the best of three ways to arrive, or zero, which means start a fresh alignment here. The highest cell anywhere is the score. This is the simplified picture; the real kernel keeps two extra tables for gaps (Gotoh 1982).");
title("The best score lives in a grid");
IMG("grid", art.grid(), .6, 1.3, 3.6, 3.6);
const ways = [[C.teal, "From the diagonal", "add the score for these two letters"], [C.coral, "From above", "open or extend a gap"], [C.lav, "From the left", "open or extend a gap"], [C.grey, "Or zero", "start a fresh alignment here"]];
ways.forEach(([c, h, d], i) => { const y = 1.45 + i * .8; BOX(4.75, y + .05, .32, .32, c, { round: true });
  T(h, { x: 5.25, y, w: 4.2, h: .32, size: 16, bold: true, color: C.ink }); T(d, { x: 5.25, y: y + .33, w: 4.2, h: .3, size: 14, color: C.muted }); });
cap("The highest cell anywhere in the grid is the similarity score.", 4.75, 4.75, 4.6, .3);

// 6 Cost
slide(false, "Exact means no shortcuts: every cell of every grid. 147 query letters times 209.2 million database letters is 30.8 billion cells, each a handful of additions and comparisons. Heuristic tools like BLAST skip most cells and can miss distant relatives.");
title("Exact means every cell, every protein");
IMG("cells", art.cells(), .6, 1.3, 8.8, 3.1);
BOX(2.3, 2.05, 5.4, 1.6, C.white, { line: C.ink, round: true, shadow: true });
stat("30.8 billion", 2.3, 2.15, 5.4, C.ink, 48); cur.els.at(-1).align = "center";
T("grid cells for one 147-letter search", { x: 2.3, y: 3.05, w: 5.4, h: .4, size: 16, color: C.muted, align: "center" });
cap("Heuristic tools such as BLAST skip cells to go faster, and can miss distant relatives.", .6, 4.75, 8.8, .3);

// 7 One core
slide(false, "Measured: plain C on one core fills 0.71 billion cells per second, so one search takes about 43 seconds. The 43 seconds is calculated from that measured rate, not timed directly for this query.");
title("One plain core: 43 seconds per search");
IMG("snail", art.snail(), .7, 1.3, 3.3, 3.5);
stat("0.71", 4.7, 1.55, 4, C.coral, 60);
body("billion cells per second (GCUPS), measured on one core in plain C", 4.7, 2.6, 4.4, .7);
stat("≈ 43 s", 4.7, 3.35, 4, C.ink, 40);
cap("for one search: 30.8 billion ÷ 0.71 billion per second (estimate from the measured rate)", 4.7, 4.1, 4.5, .5);

// 8 Independence
slide(false, "This is the key observation. Scoring my protein against protein number 1 does not need anything from protein number 2. 576,073 independent jobs. Independence is what parallel hardware needs. There are two ways to use it on a CPU: do several jobs in one instruction, which is SIMD, and run several instruction streams at once, which is threads.");
title("Every comparison is independent");
IMG("many", art.many(), .6, 1.3, 8.6, 3.2);
body("Scoring against protein 1 needs nothing from protein 2. That independence is the parallelism.", .6, 4.6, 8.8, .6, { color: C.ink });

// 9 Divider: wider
slide(true, "Level one: make each instruction do more work.");
stat("1", .6, 1.0, 1.5, C.yellow, 96);
T("Go wider: one instruction, many proteins", { x: 2.0, y: 1.25, w: 7.4, h: 1.2, size: 32, bold: true, color: C.white });
body("SIMD: Single Instruction, Multiple Data", 2.0, 2.6, 7, .4, { color: C.grey });
IMG("reg", art.reg(true), .6, 3.1, 8.8, 2.0);

// 10 Lanes
slide(false, "The CPU has 256-bit vector registers (AVX2). A score fits in 16 bits, so one register holds 16 scores. HySW gives each of the 16 slots, called lanes, a different database protein. One add instruction now advances 16 grids at once. Scores that would overflow 16 bits are detected and re-scored exactly in 32 bits.");
title("One instruction, sixteen proteins");
IMG("lanes", art.lanes(), .6, 1.25, 8.8, 3.25);
body("A 256-bit register holds 16 scores of 16 bits. Each lane works on a different protein, so one add advances 16 grids.", .6, 4.6, 8.8, .6, { color: C.ink, size: 15 });

// 11 Sort
slide(false, "Sixteen lanes move in lock-step, so a batch takes as long as its longest protein. Unsorted, short proteins finish early and their lanes sit idle, the hatched area. Sorting the database by length puts proteins of nearly equal length together: wasted lanes drop to 0.19 percent of Swiss-Prot.");
title("Sort by length so lanes finish together");
IMG("sort", art.sort(), .6, 1.55, 8.8, 3.25);
kicker("Unsorted batch", .75, 1.2, 4, C.red); kicker("Sorted by length", 5.25, 1.2, 4, C.teal);
cap("Hatched = lanes waiting for the longest protein in the batch.  Sorted: only 0.19% wasted.", .6, 4.9, 8.8, .3);

// 12 Result SIMD
slide(false, "Measured over 5 rounds with a 400-letter query: scalar 0.71, SIMD 9.00 billion cells per second, 12.7 times. Not 16 times, because some work does not get wider: building the score profile, loading data, checking for overflow. That is Amdahl's law in miniature.");
title("Wider: 12.7× faster on one core");
BAR(["Plain C", "SIMD, 16 lanes"], [0.71, 9.00], .6, 1.3, 5.6, 2.6, [C.grey, C.teal], "GCUPS");
stat("12.7×", 6.6, 1.45, 3, C.teal, 60);
body("Why not 16×? Setup and data loading don't get wider (Amdahl's law).", 6.6, 2.55, 2.9, 1.3, { color: C.ink, size: 15 });
cap("Billion cells per second, one core, 400-letter query, median of 5 rounds", .6, 4.6, 8.8, .3);

// 13 Divider: more
slide(true, "Level two: run many instruction streams at once.");
stat("2", .6, 1.0, 1.5, C.yellow, 96);
T("Go more: every core takes batches", { x: 2.0, y: 1.25, w: 5.0, h: 1.2, size: 32, bold: true, color: C.white });
body("Threads with OpenMP, one per core", 2.0, 2.55, 4.6, .4, { color: C.grey });
IMG("chipd", art.chip(true), 6.6, 1.0, 2.9, 2.25);
body("Each thread grabs a batch of 16 proteins, scores it, and writes only its own results, so no locks are needed.", 2.0, 3.55, 7.3, .9, { color: C.white, size: 16 });

// 14 Chip
slide(false, "The Intel i3-1220P is a hybrid chip: 2 performance cores, each with hyper-threading so it looks like 2 CPUs, plus 8 efficiency cores in two groups of four that share an L2 cache. A P-core is roughly twice as fast as an E-core here. The whole chip has a 15 watt long-term power limit.");
title("This laptop has two kinds of core");
IMG("chip", art.chip(false), .6, 1.25, 4.4, 3.42);
const cores = [[C.blue, "2 P-cores", "fast; each runs 2 hyperthreads"], [C.orange, "8 E-cores", "about half the speed; groups of 4 share an L2 cache"], [C.coral, "15 W", "long-term power limit for the whole chip"]];
cores.forEach(([c, h, d], i) => { const y = 1.45 + i * 1.05; BOX(5.5, y + .05, .32, .32, c, { round: true });
  T(h, { x: 6.0, y, w: 3.5, h: .35, size: 18, bold: true, color: C.ink }); T(d, { x: 6.0, y: y + .38, w: 3.5, h: .55, size: 14, color: C.muted }); });

// 15 Scheduling
slide(false, "With static scheduling every thread gets an equal share of batches up front. The P-cores finish their share in about half the time and then wait, while the E-cores are still working: 31 percent more time in total. Dynamic scheduling hands the next batch to whichever thread is free, so fast cores simply do more batches and everyone finishes together. Six smarter policies tied with plain dynamic.");
title("Equal shares make fast cores wait");
IMG("sched", art.sched(), .6, 1.25, 6.2, 2.88);
kicker("Equal shares (static)", .85, 1.36, 5, C.red); kicker("Next batch to whoever is free (dynamic)", .85, 2.81, 5.6, C.teal);
stat("+31%", 7.1, 1.4, 2.4, C.red, 48);
body("time with equal shares", 7.1, 2.25, 2.4, .4, { size: 14 });
stat("tie", 7.1, 2.85, 2.4, C.teal, 48);
body("dynamic and six smarter policies all tie", 7.1, 3.7, 2.4, .7, { size: 14 });
cap("Blue = P-core, orange = E-core, hatched = idle. Measured over 9 rounds.", .6, 4.6, 8.8, .3);

// 16 Hyperthreads
slide(false, "A P-core can run two threads, hyper-threading, but both threads share the same vector unit. HySW keeps that unit busy with one thread already, so the second thread has nothing to add: one thread per physical core gave 23.7 billion cells per second, all 12 logical CPUs gave 23.3, a tie.");
title("Hyperthreads add nothing here");
IMG("ht", art.ht(), .7, 1.3, 3.9, 3.15);
stat("23.7", 5.3, 1.45, 2, C.ink, 48); body("10 threads, one per core", 5.3, 2.3, 4, .4, { size: 14 });
stat("23.3", 5.3, 2.85, 2, C.ink, 48); body("12 threads, using hyperthreads", 5.3, 3.7, 4, .4, { size: 14 });
cap("GCUPS, sustained load, 9 rounds. Two threads share one vector unit, which is already busy.", .6, 4.75, 8.8, .3);

// 17 Power wall
slide(false, "Starting from idle, 10 cores run at 32 billion cells per second. After about 17 seconds the 15 watt long-term limit kicks in and lowers every core's clock; speed settles at 19. That is a 41 percent drop. So benchmarks must say which regime they measure; this project runs a 2 second warm-up and randomises run order.");
title("A 15-watt limit cuts speed by 41%");
IMG("power", art.power(), .6, 1.3, 2.6, 3.3);
BAR(["First seconds (burst)", "After ~17 s (sustained)"], [32.0, 19.0], 3.5, 1.3, 6.0, 2.6, [C.orange, C.coral], "GCUPS");
cap("10 cores, starting from idle, GCUPS. Benchmarks here use warm-up and random order so regimes don't mix.", 3.5, 4.3, 6, .6);

// 18 Why 2.5x
slide(false, "Putting it together: ten cores could ideally give ten times. Measured under sustained load: 2.52 times. Three brakes: E-cores are slower than P-cores, hyperthreads share vector units, and the power limit lowers every clock when all cores are busy. In the short burst regime the same change gives about 4 times.");
title("So 10 cores give 2.5×, not 10×");
IMG("brakes", art.brakes(), .6, 1.3, 8.8, 2.45);
T("Ideal: 10×", { x: 1.0, y: 1.9, w: 3, h: .4, size: 16, bold: true, color: C.muted });
T("Measured: 2.52×", { x: 3.05, y: 3.0, w: 3, h: .4, size: 18, bold: true, color: C.teal });
const br = [[C.orange, "E-cores are slower"], [C.blue, "Hyperthreads share one vector unit"], [C.coral, "15 W limit lowers every clock"]];
br.forEach(([c, s], i) => { BOX(.75 + i * 3.0, 4.07, .26, .26, c, { round: true }); T(s, { x: 1.12 + i * 3.0, y: 4.05, w: 2.6, h: .55, size: 14, color: C.ink }); });
cap("Sustained load. In the short burst regime the same change gives about 4×.", .75, 4.85, 8.5, .3);

// 19 Cache
slide(false, "A surprise found by measuring: long queries, 990 letters, were twice as slow per cell on 10 threads. Each thread's working arrays no longer fit in its small, fast L1 cache, and four E-cores sharing one L2 saturated it. The fix: process 16 columns of the grid at a time, so the working set stays in L1. Long queries became 1.54 times faster; short ones on one thread pay up to 27 percent.");
title("Long queries spilled out of L1 cache");
IMG("cache", art.cache(), .6, 1.25, 8.8, 3.38);
stat("1.54×", .6, 4.55, 2, C.teal, 36);
body("faster on 990-letter queries, 10 threads. Cost: up to 27% on short queries, 1 thread.", 2.6, 4.62, 6.8, .6, { size: 14, color: C.ink });

// 20 Together
slide(false, "Both levels multiply: 12.7 times from SIMD, times 2.52 from threads, is 31.9 times over plain C on one core, under sustained load with a 400-letter query.");
title("Together: 31.9× faster");
BAR(["Plain C, 1 core", "SIMD, 1 core", "SIMD + 10 cores"], [0.71, 9.00, 22.6], .6, 1.3, 6.0, 3.0, [C.grey, C.teal, C.blue], "GCUPS");
T([{ text: "12.7×", options: { color: C.teal } }, { text: "  ×  ", options: { color: C.muted } }, { text: "2.52×", options: { color: C.blue } }], { x: 6.8, y: 1.6, w: 2.9, h: .6, size: 26, bold: true });
stat("= 31.9×", 6.9, 2.35, 2.7, C.ink, 44);
cap("GCUPS, 400-letter query, sustained load, median of 5 rounds", .6, 4.6, 8.8, .3);

// 21 Correctness
slide(false, "Speed means nothing if the answer is wrong. Every one of 1,152,146 scores over all of Swiss-Prot was compared with Parasail, an independent published library: all identical. Plus 2,351 kernel checks under AddressSanitizer and UBSan, and the 16-bit overflow path verified on scores above 32,767. The scores in the picture are illustrative.");
title("Fast is useless unless it's right");
IMG("check", art.check(), .6, 1.4, 5.6, 2.63);
stat("1,152,146", 6.5, 1.5, 3.2, C.teal, 36);
body("of 1,152,146 scores identical to Parasail, an independent library", 6.5, 2.2, 3.0, .9, { size: 14 });
stat("2,351", 6.5, 3.15, 3, C.ink, 36);
body("kernel checks, clean under AddressSanitizer and UBSan", 6.5, 3.85, 3.0, .7, { size: 14 });
cap("Scores in the drawing are illustrative.", .6, 4.75, 5, .3);

// 22 App
slide(false, "The engine is wrapped in a local web app. A haemoglobin search over Swiss-Prot took about 1.2 seconds in a single run; the tiles light up from the engine's real per-core progress. The 3D model comes from AlphaFold DB.");
title("You can watch it work");
PIC(SHOT, .6, 1.25, 5.9, 3.69);
stat("≈ 1.2 s", 6.9, 1.4, 2.7, C.teal, 40);
body("haemoglobin search over all of Swiss-Prot (single run)", 6.9, 2.15, 2.6, .8, { size: 14 });
body("Live per-core progress, the alignment, and the 3D structure from AlphaFold DB", 6.9, 3.1, 2.6, 1.2, { size: 14, color: C.ink });

// 23 Lessons
slide(true, "Three lessons, each backed by a measurement in this project.");
title("Three lessons", true);
const les = [["a", "Find the independence first", "576,073 independent comparisons made both SIMD and threads possible."],
  ["b", "On mixed cores, hand out work", "Dynamic scheduling beat equal shares by 31% and tied with six smarter policies."],
  ["c", "On a laptop, power is a resource", "The 15 W limit cut speed 41%. Measure in a controlled power state."]];
les.forEach(([k, h, d], i) => { const x = .6 + i * 3.0;
  BOX(x, 1.4, 2.75, 3.4, C.night, { round: true }); IMG("icon" + k, art.icon(k), x + .3, 1.65, .9, .9);
  T(h, { x: x + .3, y: 2.75, w: 2.25, h: .8, size: 18, bold: true, color: C.white });
  T(d, { x: x + .3, y: 3.6, w: 2.25, h: 1.1, size: 13, color: C.grey }); });

// 24 Limits
slide(false, "Being clear about the limits: one machine, so no claim about other CPUs. No GPU or MPI, because the laptop has no discrete GPU. The Parasail speed comparison goes through Python, so it is a yardstick. Live app numbers are single runs and vary with power state; they are labelled on screen.");
title("What this does not show");
const lim = [["One laptop", "Results come from one i3-1220P; other CPUs may differ."], ["No GPU or MPI", "There is no discrete GPU, and everything ran on one machine."],
  ["Parasail via Python", "The speed comparison includes Python overhead: a yardstick, not a race."], ["Live numbers vary", "App runs are single runs and depend on power state; they are labelled."]];
lim.forEach(([h, d], i) => { const x = .6 + (i % 2) * 4.5, y = 1.35 + Math.floor(i / 2) * 1.75;
  BOX(x, y, 4.2, 1.5, C.soft, { round: true }); IMG("lim" + i, art.icon("d").replace(/#E4572E|#F28C28|#17A398/, "#5B6475"), x + .25, y + .3, .6, .6);
  T(h, { x: x + 1.05, y: y + .22, w: 2.95, h: .4, size: 17, bold: true, color: C.ink }); T(d, { x: x + 1.05, y: y + .65, w: 2.95, h: .75, size: 13, color: C.muted }); });

// 25 Close
slide(true, "Exact search in about a second on a laptop, and a measured answer to why twelve threads are not twelve times faster. Code, tests and raw results are in the repository.");
T("Exact search in about a second, and a measured answer to why 12 threads aren't 12× faster", { x: .6, y: 1.1, w: 5.2, h: 2.3, size: 30, bold: true, color: C.white });
IMG("title2", art.title(), 6.0, 1.0, 3.5, 3.0);
body("Code, tests and raw results: github.com/TARUN-2305/hysw", .6, 3.9, 5.2, .6, { color: C.grey, size: 14 });
T("Thank you", { x: .6, y: 4.6, w: 4, h: .5, size: 20, bold: true, color: C.yellow });

// ---- render images -------------------------------------------------------------------------
(async () => {
  for (const s of S) for (const e of s.els) if (e.k === "i") {
    e.file = path.resolve("img", e.name + ".png");
    await sharp(Buffer.from(e.svgStr), { density: 300 }).resize(Math.round(e.w * 300)).png().toFile(e.file);
  }
  // ---- PPTX ----
  const pres = new pptxgen(); pres.layout = "LAYOUT_16x9"; pres.title = "HySW from first principles";
  pres.theme = { headFontFace: "Arial", bodyFontFace: "Arial" };
  S.forEach((s, n) => {
    const sl = pres.addSlide(); sl.background = { color: s.dark ? C.ink : C.white };
    for (const e of s.els) {
      if (e.k === "t") sl.addText(Array.isArray(e.text) ? e.text : (e.upper ? e.text.toUpperCase() : e.text),
        { x: e.x, y: e.y, w: e.w, h: e.h, fontSize: e.size, bold: !!e.bold, color: e.color, fontFace: "Arial", align: e.align || "left",
          valign: "top", margin: 0, isTextBox: true, charSpacing: e.upper ? 1.5 : 0, objectName: `s${n + 1}-text` });
      if (e.k === "i" || e.k === "p") sl.addImage({ path: e.file, x: e.x, y: e.y, w: e.w, h: e.h, altText: e.name || "HySW app screenshot", objectName: `s${n + 1}-${e.name || "shot"}` });
      if (e.k === "b") sl.addShape(e.round ? pres.shapes.ROUNDED_RECTANGLE : pres.shapes.RECTANGLE, { x: e.x, y: e.y, w: e.w, h: e.h,
        fill: { color: e.fill }, line: e.line ? { color: e.line, width: 2 } : { type: "none" }, rectRadius: e.round ? Math.min(.15, e.h / 2) : undefined,
        shadow: e.shadow ? { type: "outer", color: C.ink, opacity: .18, blur: 0, offset: 4, angle: 45 } : undefined });
      if (e.k === "c") sl.addChart(pres.charts.BAR, [{ name: e.unit, labels: e.labels, values: e.vals }], { x: e.x, y: e.y, w: e.w, h: e.h,
        barDir: "bar", catAxisOrientation: "maxMin", chartColors: e.colors, barGapWidthPct: 45, showValue: true, dataLabelPosition: "outEnd",
        dataLabelFormatCode: "0.00", dataLabelFontSize: 16, dataLabelFontBold: true, dataLabelColor: C.ink, dataLabelFontFace: "+mn-lt",
        catAxisLabelColor: C.ink, catAxisLabelFontSize: 14, catAxisLabelFontFace: "+mn-lt", catAxisLineShow: false, valAxisHidden: true,
        valGridLine: { style: "none" }, catGridLine: { style: "none" }, valAxisMinVal: 0, valAxisMaxVal: Math.max(...e.vals) * 1.22, showLegend: false, objectName: `s${n + 1}-chart` });
    }
    sl.addNotes(s.notes);
  });
  await pres.writeFile({ fileName: OUT });
  // ---- HTML preview (96 px per inch, pt sizes as in PowerPoint) ----
  const px = v => (v * 96).toFixed(1) + "px";
  let h = `<!doctype html><meta charset=utf-8><style>body{margin:0;background:#888}.s{position:relative;width:960px;height:540px;margin:0 0 12px;overflow:hidden;font-family:'Liberation Sans',Arial}.s>*{position:absolute;margin:0;box-sizing:border-box}.t{line-height:1.17}.n{position:absolute;right:6px;bottom:4px;font:11px monospace;color:#f0f}</style>`;
  S.forEach((s, n) => { h += `<div class=s style="background:#${s.dark ? C.ink : C.white}">`;
    for (const e of s.els) { const pos = `left:${px(e.x)};top:${px(e.y)};width:${px(e.w)};height:${px(e.h)}`;
      if (e.k === "t") { const inner = Array.isArray(e.text) ? e.text.map(r => `<span style="color:#${r.options.color}">${r.text.replace(/ /g, "&nbsp;")}</span>`).join("") : (e.upper ? e.text.toUpperCase() : e.text);
        h += `<div class=t style="${pos};font-size:${e.size}pt;font-weight:${e.bold ? 700 : 400};color:#${e.color};text-align:${e.align || "left"};letter-spacing:${e.upper ? "1.5pt" : 0};outline:1px dotted rgba(255,0,255,.25)">${inner}</div>`; }
      if (e.k === "i" || e.k === "p") h += `<img src="${path.relative(".", e.file)}" style="${pos}">`;
      if (e.k === "b") h += `<div style="${pos};background:#${e.fill};border-radius:${e.round ? Math.min(.15, e.h / 2) * 96 : 0}px;${e.line ? `border:2.6px solid #${e.line};` : ""}${e.shadow ? "box-shadow:4px 4px 0 rgba(29,36,51,.18)" : ""}"></div>`;
      if (e.k === "c") { const mx = Math.max(...e.vals) * 1.22, bh = e.h / e.vals.length;
        h += `<div style="${pos}">` + e.vals.map((v, i) => `<div style="position:absolute;left:0;top:${px(i * bh + bh * .18)};width:${px(e.w * .34)};font-size:14pt;color:#${C.ink};text-align:right">${e.labels[i]}</div><div style="position:absolute;left:${px(e.w * .36)};top:${px(i * bh + bh * .16)};height:${px(bh * .68)};width:${px(e.w * .62 * v / mx)};background:#${e.colors[i]}"></div><div style="position:absolute;left:${px(e.w * .37 + e.w * .62 * v / mx)};top:${px(i * bh + bh * .3)};font-size:16pt;font-weight:700;color:#${C.ink}">${v.toFixed(2)}</div>`).join("") + `</div>`; }
    }
    h += `<div class=n>${n + 1}</div></div>`; });
  fs.writeFileSync("preview.html", h);
  console.log("slides:", S.length, "->", OUT);
})();
