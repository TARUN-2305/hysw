"""Write a FASTA file that repeats every record K times (pilot databases larger than the E. coli
proteome while keeping its real length distribution). Headers get a copy suffix.
Usage: python3 -I scripts/replicate_fasta.py <in.fasta> <K> <out.fasta>"""
import sys

src, k, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
recs, hdr, seq = [], None, []
for line in open(src):
    if line.startswith(">"):
        if hdr is not None:
            recs.append((hdr, "".join(seq)))
        hdr, seq = line[1:].rstrip("\n"), []
    else:
        seq.append(line.strip())
if hdr is not None:
    recs.append((hdr, "".join(seq)))
with open(out, "w") as f:
    for c in range(k):
        for h, s in recs:
            f.write(f">{h} copy={c}\n{s}\n")
print(f"{len(recs)} records x {k} = {len(recs) * k} records -> {out}")
