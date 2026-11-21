"""Synthetic database whose best scores pass the 16-bit limit (32,767), for the overflow oracle test.
W/W scores 11 in BLOSUM62, so a run of n W's against a W-run query scores 11 x n.
Usage: python3 -I tests/make_overflow_fasta.py <out_db.fasta> <out_query.fasta>"""
import random
import sys

rng = random.Random(7)
AA = "ACDEFGHIKLMNPQRSTVWY"
db, qf = sys.argv[1], sys.argv[2]
with open(db, "w") as f:
    for k, n in enumerate((2975, 2977, 2978, 2979, 3000, 3200, 3600)):
        f.write(f">sp|W{k:05d}|WRUN_{n} W run of {n}\n" + "W" * n + "\n")
    for k in range(40):
        s = "".join(rng.choice(AA) for _ in range(rng.randint(50, 3000)))
        f.write(f">sp|R{k:05d}|RAND_{k} random\n{s}\n")
    mixed = "".join("C" if j % 13 == 0 else "W" for j in range(3500))
    f.write(f">sp|M00001|MIXED mixed W/C run\n{mixed}\n")
with open(qf, "w") as f:
    f.write(">wq W run query of 3400\n" + "W" * 3400 + "\n")
    f.write(">mq mixed query\n" + "".join("C" if j % 13 == 0 else "W" for j in range(3300)) + "\n")
