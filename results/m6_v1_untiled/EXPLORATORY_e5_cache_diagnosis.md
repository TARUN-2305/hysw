# Exploratory: why do long queries slow down on 10 threads? (E5 follow-up)

**Status: exploratory.** Short, unrandomised spot checks, run on 2026-11-21 after M6 finished. They are not part of the
randomised M6 protocol. Use them for the explanation only, not as headline numbers.

## Observation (from E5)
With one thread per physical core, the median GCUPS on E. coli ×20 falls from 28.2 at L=400 to 14.0 at L=990 and
14.2 at L=2339. A single thread, on a P-core or an E-core, does **not** fall: it rises slightly with query length.

## Hypothesis 1: power limit, because the long queries run last and longest — rejected
E5 runs its queries shortest first. The check ran the six sweep queries in forward and in reversed order, 3 repeats
each (`--cores physical --condition 2`, E. coli ×20).

| order | L10 | L50 | L150 | L400 | L990 | L2339 |
|---|---|---|---|---|---|---|
| forward, rep 0–2 | 3.1 / 5.4 / 4.6 | 20.7 / 15.8 / 17.2 | 31.3 / 26.3 / 24.8 | 26.6 / 27.2 / 22.3 | 15.6 / 14.7 / 17.0 | 14.2 / 15.1 / 15.9 |
| reversed, rep 0–2 | 5.4 / 5.1 / 5.4 | 17.1 / 15.4 / 17.4 | 26.3 / 24.0 / 26.1 | 20.8 / 28.8 / 25.3 | 16.9 / 14.6 / 16.9 | 14.9 / 14.9 / 15.9 |

L2339 stays near 15 GCUPS even when it runs first, and L400 stays at 21–29 even after the long queries. The slowdown
follows query length, not position or elapsed time.

## Hypothesis 2: the H/E arrays overflow L1 and saturate the L2 shared by an E-core cluster — supported
Each thread keeps H and E arrays of 16 lanes × 2 B × L, i.e. **64 B × L** (25.6 KB at L=400, 63 KB at L=990, 150 KB at
L=2339). L1D is 48 KB on a P-core and 32 KB on an E-core (sysfs). The E-cores cpu4–7 share one 2 MB L2, and so do
cpu8–11; each P-core has a private L2 (sysfs `cache/index2/shared_cpu_list`).

Per-thread GCUPS for the queries L=400/990/2339 on E. coli ×20, with `--condition 2`, 2 repeats:

| cores | threads | L400 | L990 | L2339 |
|---|---|---|---|---|
| 4 (one E-core) | 1 | 4.78 / 4.37 | 5.18 / 4.86 | 5.37 / 5.06 |
| 4,5,6,7 (one E-cluster, shared L2) | 4 | 4.42 / 3.16 | **1.76 / 1.82** | **1.80 / 1.82** |
| 4,8 (two E-cores, different clusters) | 2 | 3.71 / 3.90 | 4.19 / 4.11 | 4.35 / 4.34 |
| 0 (one P-core) | 1 | 7.65 / 7.84 | 8.45 / 8.75 | 9.26 / 9.01 |
| 0,2 (two P-cores, private L2) | 2 | 6.69 / 6.69 | 6.76 / 6.95 | 7.05 / 6.78 |
| 0,1 (P-core + its hyperthread) | 2 | 3.92 / 3.89 | 4.55 / 4.45 | 4.75 / 4.74 |

Only the four-E-cores-on-one-L2 case collapses, by about 60% per thread, once the arrays exceed L1. When the same
number of E-cores is spread across clusters, or on P-cores, nothing collapses. So when every E-core of a cluster spills
H/E into the shared L2 at the same time, they exceed its bandwidth.

## What would fix it (not implemented)
Strip-mine the query: process rows in strips of about 128–256, so the H/E rows of a strip stay in L1 (8–16 KB). The
F/H values at each strip's bottom boundary are kept per database column (64 B × batch length), so the L2 traffic falls
by roughly the strip height. This is an engine change. E1–E5 would need re-running afterwards.

## Commands
Forward vs reversed order: `bin/hysw_engine search --db data/ecoli_x20.bin --query <sweep6 | reversed sweep6> --cores physical --top-k 1 --align-k 0 --condition 2`.
Core sets: the same command with `--query` = the L400/L990/L2339 records of `bench/queries_sweep6.fasta` and `--cores 4 | 4,5,6,7 | 4,8 | 0 | 0,2 | 0,1`.
