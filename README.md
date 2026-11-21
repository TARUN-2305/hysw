# HySW — exact protein database search on a hybrid laptop CPU

Exact Smith–Waterman local alignment of a protein query against every sequence of a protein database,
using two levels of parallelism: SIMD inside one core (16 database sequences per vector, plain C with
`#pragma omp simd`) and OpenMP threads across the P- and E-cores. CS372IA Experiential Learning project.

Status: Phase 2 — engine milestones M1–M5 built and verified, M6 benchmark study done and re-run with the tiled kernel (`results/m6/SUMMARY.md`; first run in `results/m6_v1_untiled/`; progress in `PHASE_2_PROGRESS.md`), thread-set
calibration measured (`results/calibration/`), web app parts 1 and 2 (M6a: server and Search screen; M6b: 3D
structure view, Performance Lab, launcher) built and tested. M7 documents in `docs/` (report, case study, slides, demo script). Design records: `research/` (Phase 1), `DECISIONS.md` (changes during Phase 2).

## Prerequisites
- Linux x86-64 with AVX2, GCC with OpenMP (tested: GCC 15.2, Ubuntu 26.04)
- Python 3 with `venv` (tested: 3.14) — only for the Parasail oracle test, plots and the app server

## Setup
```bash
./build.sh                                   # builds bin/hysw_engine and bin/test_kernels
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
```
`build.sh` fails if GCC stops vectorising the SIMD kernel loop (report in `build/vec_report.txt`).

## Test
```bash
tests/run_tests.sh                           # all correctness tests (kernels, Parasail oracle, engine, web app)
```

## Run
```bash
bin/hysw_engine prep --fasta data/ecoli_k12.fasta --out data/ecoli_k12.bin
bin/hysw_engine search --db data/ecoli_k12.bin --query tests/queries/q_medium.fasta --top-k 10
bin/hysw_engine info                         # CPU topology, thread sets, SIMD, power limits (JSON)
scripts/sysinfo.sh                           # machine state recorded with benchmark results
```

## Web app
```bash
app/launch.sh                                # build if needed, start the server, open http://127.0.0.1:8372/
app/launch.sh --stop                         # stop it
app/install_launcher.sh                      # optional: add "HySW" to the desktop's application menu
.venv/bin/python -I app/server.py --open     # or run the server in this terminal (Ctrl+C stops it)
```
Search screen: paste a protein or pick an example, choose the database, watch the engine's live progress per
CPU, then read the hit table, alignments and run summary; export hits (CSV), an alignment (text) or the full
run report (JSON). Setup screen: databases, hardware, optional thread-set calibration. The server runs the
engine as a separate process for every search (one at a time) and binds to 127.0.0.1 only.

## Benchmark
```bash
.venv/bin/python -I bench/run_bench.py --db data/X.bin --queries bench/queries_bench3.fasta \
    --set policies --rounds 9 --regime sustained --out results/raw/NAME.csv
.venv/bin/python -I scripts/analyse.py results/raw/NAME.csv --baseline "simd:physical:dynamic,1"
.venv/bin/python -I scripts/plot_timeline.py fig.png run_static.json run_dynamic.json   # runs made with --trace
```
Every round runs the configurations in a fresh random order; in the sustained regime the engine runs
the same all-core conditioning load immediately before every measured pass (burst regime: idle
cool-down, no conditioning). `sysinfo.sh` output and the random orders are saved next to the CSV.

## Data
- `data/BLOSUM62` — NCBI BLOSUM62 matrix.
- `data/ecoli_k12.fasta` — UniProtKB reviewed *E. coli* K-12 proteome (4,403 proteins), small test database.
- UniProtKB/Swiss-Prot (main benchmark and demo database) is downloaded separately:
  `scripts/get_swissprot.sh` → `data/swissprot_<release>/swissprot.bin`. Used here: release 2026_04
  (04-Nov-2026), 576,073 sequences, 209.2 M residues; SHA-256 of the archive in `SHA256SUMS` next to it.
  UniProt data is licensed CC BY 4.0 (The UniProt Consortium); the UniProt `LICENSE` file is saved with
  the data and must accompany any copy. The E. coli proteome above is UniProt data under the same licence.
