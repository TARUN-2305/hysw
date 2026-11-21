# Application Design — local web app for exact protein database search

Decided 2026-11-20 with the student (see `DECISIONS.md`, D1). Companion to `FINAL_PROJECT_PROPOSAL.md` and `PHASE_2_BLUEPRINT_V2.md`.

Working name: **HySW** (rename freely).

## 1. Who uses it and why

| User | What they want | What they use |
|---|---|---|
| **Researcher** — a biology student or researcher with a protein sequence (e.g. from a lab experiment or a newly sequenced gene) | "Which known proteins does this resemble, how closely, and where?" — the usual first step towards guessing what a protein does | **Search** screen |
| **Learner / examiner** — the course examiner, classmates, the student presenting | "Why is this fast? What do SIMD and threads each contribute? Why aren't 12 cores 12× faster?" | **Performance Lab** screen |

Both use the same engine and the same real data. The Lab is not a separate toy.

## 2. Screens

### 2.1 Home / Setup (first launch only)
- Shows which protein databases are ready.
  - Bundled: *E. coli* K-12 reviewed proteome (4,403 proteins), always available.
  - Optional: **UniProtKB/Swiss-Prot** (~570k curated proteins, ~94 MB download). A button downloads and prepares it, with a progress bar and the licence shown first.
- Shows a hardware card: CPU name, P-cores/E-cores/threads, SIMD support, power profile, power limit. Read from the system, not typed in.

### 2.2 Search (the application)
**Inputs**
| Input | Form | Default |
|---|---|---|
| Query protein | Paste raw letters or FASTA, upload a `.fasta` file, or pick from examples (e.g. human haemoglobin β, insulin, a long protein) | — |
| Database | Drop-down of prepared databases | Swiss-Prot if prepared, else *E. coli* |
| Number of hits | 10 / 25 / 50 | 25 |
| Engine settings (collapsed "Advanced" panel) | Mode: Auto / Scalar / SIMD / SIMD + threads. Cores: Auto / P-cores only / E-cores only / One per physical core / All | Auto / Auto |

Input checks: only amino-acid letters (others flagged, unknowns become X); length 10–5,000; clear message for empty, wrong format, or DNA pasted by mistake.

**What happens when the user clicks Search**
1. Progress bar driven by the engine's real progress (batches finished / total), with elapsed time.
2. Live core strip: 12 tiles (P0–P3 as the two P-cores' hyperthreads, E0–E7), lit from the engine's real per-thread activity.
3. Cancel button.

**Outputs**
| Output | Content |
|---|---|
| Hit table | Rank, accession (link to UniProt), protein name, organism, length, alignment score, % identity and % coverage of the aligned region |
| Alignment view | For the selected hit: query and hit sequences aligned, identical / similar / gap positions coloured, start and end positions |
| **3D structure** | Predicted structure of the selected hit (AlphaFold DB), rotatable; **the aligned region highlighted** on the model, the rest greyed. Cached locally after first view; works offline for cached and demo proteins; a clear message if offline and not cached |
| Run summary | Time taken; cells computed; speed (billion cells per second); engine mode and cores used; "a scalar single-core search would take about X" (labelled as estimated from the measured scalar rate) |
| Export | Hits as CSV; alignment as text; a run report (JSON) with all settings and measurements |

### 2.3 Performance Lab (the explanation)
| Panel | What it shows | Data source |
|---|---|---|
| **The race** | Same query, same database: Scalar vs SIMD (1 core) vs SIMD + threads, three bars filling live, final times and speedups | Live run on the small *E. coli* database (scalar on Swiss-Prot would take minutes) |
| **Inside SIMD** | A small score table animating 16 database proteins filled in lock-step, with real cell values replayed from an actual search | Engine debug trace of one batch |
| **Cores at work** | Per-core timeline for two runs side by side: default `static` schedule (idle P-cores visible) vs the chosen schedule | Live run with tracing |
| **Which cores?** | Bar chart: P-only, E-only, one-per-physical-core, all — burst and sustained | Results of the rigorous benchmark (§5), with date and settings shown |
| **Why not 12× faster?** | Power limit read from the system; burst vs sustained speed; short plain-language explanation | Benchmark results + live system reading |
| **Correctness** | "All N scores identical to Parasail" badge with the date of the last verification | Test-suite output |

## 3. End-to-end flow

```
User double-clicks the HySW desktop icon
  └─ launcher: builds the C engine if missing (gcc), starts the local server, opens the browser at http://127.0.0.1:<port>

Browser (HTML + CSS + JavaScript, no build step)
  │  POST /api/search {query, database, top_k, mode, cores}
  ▼
Local Python server (Flask), localhost only
  │  validates input, creates a job, launches the engine as a separate process
  ▼
C engine  (hysw_engine — never seen by the user)
  │  writes progress and per-thread activity as JSON lines while running
  │  writes the final result (hits, scores, timings, alignments for top hits) as JSON
  ▼
Server relays progress to the browser (Server-Sent Events) and stores the result
  ▼
Browser renders hit table, alignment, run summary
  └─ on selecting a hit: server fetches the AlphaFold structure by accession (once, then cached) → 3D viewer highlights the aligned region
```

Design rules:
- The engine is a separate program with a small, documented JSON interface. It is built and tested on its own (unit tests, Parasail oracle, benchmarks), so a UI problem can never corrupt a measurement.
- The server binds to `127.0.0.1` only. No accounts. The query sequence never leaves the machine; the only outbound request is an AlphaFold structure lookup by public accession number, and only when the user opens the 3D view.
- **Every animation is driven by real engine output.** Nothing is simulated for show. Where a number is an estimate, the screen says so.
- Rigorous benchmarks run from the benchmark harness, not from the UI; the Lab displays their stored results with their settings.

## 4. Engine interface (internal)

```
hysw_engine prep   --fasta <db.fasta> --out <db.bin>
hysw_engine search --db <db.bin> --query <q.fasta> --top-k 25
                   --mode auto|scalar|simd|simd_threads --cores auto|p|e|physical|all|<list>
                   [--trace] [--progress]
hysw_engine info   (hardware, SIMD, power profile and limits as JSON)
```

Result JSON (sketch):
```json
{
  "query": {"name": "...", "length": 147},
  "database": {"name": "swissprot", "sequences": 571000, "residues": 206000000},
  "settings": {"mode": "simd_threads", "cores": [0,2,4,5,6,7,8,9,10,11], "schedule": "dynamic"},
  "timing": {"seconds": 5.4, "gcups": 5.6, "cells": 3.0e10},
  "hits": [{"rank": 1, "accession": "P68871", "name": "Hemoglobin subunit beta", "organism": "Homo sapiens",
            "length": 147, "score": 768, "identity": 1.0, "coverage": 1.0,
            "alignment": {"q_start": 1, "q_end": 147, "t_start": 1, "t_end": 147, "q": "...", "t": "...", "mid": "..."}}],
  "trace": {"threads": [{"cpu": 0, "type": "P", "busy": [[0.00, 0.41], ...]}]}
}
```
(Numbers above are placeholders for format only, not measurements.)

## 5. Technology choices

| Part | Choice | Why |
|---|---|---|
| Engine | C11 + OpenMP (GCC 15) | Already prototyped and verified |
| Server | Python 3.14 + Flask in the project venv | Small, widely known, easy to explain; the venv is needed anyway for Parasail and plots |
| Front end | Plain HTML/CSS/JavaScript, no framework or build step | Nothing to compile or break; easy to read in a viva |
| Charts | Chart.js, bundled locally | Mature, offline |
| 3D viewer | 3Dmol.js, bundled locally | Lightweight, displays AlphaFold models, supports colouring residue ranges |
| Launcher | Linux `.desktop` file + start script | Double-click start; no terminal shown |

Licences of the bundled libraries and of AlphaFold DB data to be checked when they are added (believed permissive / CC-BY; not yet confirmed).

## 6. Out of scope for the app
User accounts, remote hosting, nucleotide (DNA) search, E-values/statistics, multiple-sequence alignment, mobile layout beyond basic responsiveness, running rigorous benchmarks from the browser.
