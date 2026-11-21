# Case study: from minutes to seconds, and why twelve threads are not twelve times faster on a laptop

## The situation
A student has a protein sequence and a laptop with an Intel i3-1220P (2 P-cores with hyper-threading, 8 E-cores, 12 logical CPUs). Searching all 576,073 Swiss-Prot proteins exactly with plain C on one core takes about 2 minutes for a 400-residue query (measured: 117.9 s). How fast can it go, and what limits it?

## Step 1: data-level parallelism (Unit III: SIMD, vector extensions, loop-level parallelism)
Sort the database by length and score 16 proteins of nearly equal length at once, one per 16-bit lane of an AVX2 register. The loop over lanes has no dependencies, so the compiler vectorises it from `#pragma omp simd`. Result: 117.9 s → 9.3 s, **12.7×** on one core (M6 E1). Discussion: why not 16×? Profile building and data movement do not shrink with the lane count (Amdahl's law in miniature).

## Step 2: thread-level parallelism (Unit II: shared-memory multiprocessors, synchronization)
Run batches on all 10 physical cores; every thread writes only its own score slots, so no locks are needed in the hot loop. Result: 9.3 s → 3.7 s, **2.52×** more under sustained load, 31.9× in total. Discussion: why so little from 10 cores? See steps 3 and 4.

## Step 3: mapping and load balancing (Unit IV: decomposition, mapping, load balancing)
With `static` scheduling every thread gets an equal share, but a P-core is roughly twice as fast as an E-core, so P-cores idle at the end: **+31%** time (M6 E3). `dynamic,1` (each free thread takes the next batch) balances automatically; six smarter variants tie with it. Lesson: on asymmetric cores, dynamic assignment of small tasks beats a static decomposition, and cleverness beyond that bought nothing here.

## Step 4: the power wall (Unit I: performance measurement and dependability)
From idle, 10 cores run at about 32 GCUPS; about 17 s later the 15 W long-term power limit forces lower clocks and speed settles at 19 GCUPS (**−41%**, M6 E4). Hyperthreads add nothing (23.3 vs 23.7 GCUPS) because the two threads of a P-core share one vector unit. Lesson: on a laptop, measured speedup depends on the power regime, so benchmarks must control it (randomised order, conditioning load).

## Step 5: the memory hierarchy (Unit I/II: caches)
Long queries (990 residues) were 2× slower per cell on 10 threads. The cause: each thread's working arrays left L1 and four E-cores sharing one 2 MB L2 saturated it. Blocking the loop over 16 columns kept the data in L1: **1.54×** faster for long queries (M6 E7), at a cost of up to 27% for short queries on one thread.

## Outcome
About 43 s → about 1.2 s for a haemoglobin query against Swiss-Prot in the app (single run; a labelled estimate from the measured scalar rate), with every score identical to the Parasail library (1,152,146/1,152,146). Not covered: GPU, MPI and OpenACC (no discrete GPU; single machine).

## Questions for discussion
1. Predict the speedup on a CPU with AVX-512 (32 lanes of int16). What would stop it reaching 2×?
2. Why does `guided` scheduling perform worst here, even though it is designed for imbalance?
3. Would a desktop CPU without a 15 W limit show the same 2.5× thread speedup? What would you measure?
