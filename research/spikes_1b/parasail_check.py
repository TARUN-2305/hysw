# EXPLORATORY (Phase 1B, disposable): cross-check the spike's Smith-Waterman scores against an
# independent, established library (Parasail), and time Parasail's hand-vectorised kernel on one thread.
# Run: <venv>/bin/python -I parasail_check.py <db.fasta> <scores.bin from SW_DUMP>
import sys, time, array
import parasail
fa, dump = sys.argv[1], sys.argv[2]
ALPHA = set("ARNDCQEGHILKMFPSTWYVBZX*")
seqs, cur = [], []
for line in open(fa):
    if line.startswith(">"):
        if cur: seqs.append("".join(cur))
        cur = []
    else:
        cur.append("".join(c if c in ALPHA else "X" for c in line.strip().upper() if c.isalpha()))
if cur: seqs.append("".join(cur))
lens = [len(s) for s in seqs]
queries = []
for want in (150, 400, 1000):                      # same selection rule as the C spike
    best = min(range(len(seqs)), key=lambda k: (abs(lens[k] - want), k))
    queries.append(seqs[best])
mine = array.array("i"); mine.frombytes(open(dump, "rb").read())
n = len(seqs); assert len(mine) == 3 * n, (len(mine), n)
M = parasail.blosum62
# which gap convention matches? (the C spike charges 11 + k for a gap of k residues)
for (go, ge) in ((11, 1), (12, 1)):
    prof = parasail.profile_create_16(queries[0], M)
    ok = sum(parasail.sw_striped_profile_16(prof, seqs[k], go, ge).score == mine[k] for k in range(400))
    print(f"gap open={go} extend={ge}: {ok}/400 scores equal on the first 400 sequences")
    if ok == 400: conv = (go, ge)
go, ge = conv
total_cells, t_all, mism = 0, 0.0, 0
for qi, q in enumerate(queries):
    prof = parasail.profile_create_16(q, M)
    f = parasail.sw_striped_profile_16
    t0 = time.perf_counter()
    sc = [f(prof, s, go, ge).score for s in seqs]
    t_all += time.perf_counter() - t0
    total_cells += len(q) * sum(lens)
    mism += sum(1 for k in range(n) if sc[k] != mine[qi * n + k])
print(f"all 3 queries x {n} sequences: {3*n - mism}/{3*n} scores identical to the C spike (mismatches: {mism})")
print(f"parasail striped 16-bit (best instruction set auto-selected; AVX2 available), one thread (includes Python call overhead): {t_all:.3f} s = {total_cells/t_all/1e9:.2f} GCUPS")
