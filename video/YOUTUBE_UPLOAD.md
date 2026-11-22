# YouTube upload kit

**File:** `video/renders/HySW_explainer.mp4` · **Visibility:** Unlisted · **Category:** Education · **Made for kids:** No

**Title:** Exact protein search on a laptop: SIMD + threads from first principles (HySW)

**Description:**
```
How fast can an ordinary laptop search every known protein exactly? This video builds the answer from first principles: what a protein is, how the Smith–Waterman algorithm scores similarity, why exact search costs 30.8 billion grid cells per query, and how SIMD (16 proteins per instruction) and threads on a hybrid Intel i3-1220P bring a search of all 576,073 Swiss-Prot proteins down to about one second. Every number is measured on the laptop; it ends with a real recording of the HySW app.

Chapters
0:00 Why find a protein's relatives
0:35 What is a protein
0:59 Measuring similarity
1:20 The Smith–Waterman grid
1:57 The cost: 30.8 billion cells
2:24 The key insight: independence
2:44 Go wider: SIMD (12.7×)
3:38 Go more: threads and scheduling
4:15 The limits: hyperthreads and the 15 W wall
4:49 A cache surprise
5:09 All together, and checked
5:31 Live demo of the real app
6:16 Three lessons

Code, tests and raw measurements: https://github.com/TARUN-2305/hysw

Credits: narration by the Kokoro TTS model (local); predicted structures from AlphaFold DB (CC BY 4.0); sequences from UniProtKB/Swiss-Prot (CC BY 4.0). Animation made in code with the free Nafay 3D skills (nafay3d.com). CS372IA Parallel Architecture project.

#parallelcomputing #SIMD #bioinformatics
```

**Tags:** parallel computing, SIMD, OpenMP, Smith-Waterman, bioinformatics, protein search, AVX2, hybrid CPU, performance engineering

**Thumbnail:** `video/renders/thumbnail.jpg`
