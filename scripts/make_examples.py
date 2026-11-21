"""Extract the app's example query proteins from the local UniProtKB/Swiss-Prot FASTA (no typing of sequences).

    .venv/bin/python -I scripts/make_examples.py [data/swissprot_2026_04/uniprot_sprot.fasta]

Writes app/examples.json. Source: UniProtKB/Swiss-Prot release in data/swissprot_2026_04/reldate.txt,
licence CC BY 4.0 (data/swissprot_2026_04/LICENSE).
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FASTA = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "data", "swissprot_2026_04", "uniprot_sprot.fasta")
WANT = {  # accession -> why it is a useful example
    "P68871": "Human haemoglobin beta: a classic, well-studied short protein",
    "P01308": "Human insulin precursor: short, famous",
    "P42212": "Green fluorescent protein (jellyfish): distinctive barrel structure",
    "P00582": "E. coli DNA polymerase I: a long protein that is also in the bundled E. coli database",
}
found, cur, seq = {}, None, []


def flush():
    if cur:
        found[cur[0]] = {"accession": cur[0], "header": cur[1], "sequence": "".join(seq)}


for line in open(FASTA):
    if line.startswith(">"):
        flush()
        acc = line[1:].split("|")[1] if line.count("|") >= 2 else None
        cur = (acc, line[1:].strip()) if acc in WANT else None
        seq = []
    elif cur:
        seq.append(line.strip())
flush()
missing = set(WANT) - set(found)
if missing:
    raise SystemExit(f"not found in {FASTA}: {sorted(missing)}")
release = open(os.path.join(os.path.dirname(FASTA), "reldate.txt")).read().split("\n")[1].strip()
out = {"source": f"{release}; CC BY 4.0", "examples": [dict(found[a], note=WANT[a], length=len(found[a]["sequence"])) for a in WANT]}
with open(os.path.join(ROOT, "app", "examples.json"), "w") as f:
    json.dump(out, f, indent=1)
for e in out["examples"]:
    print(e["accession"], e["length"], e["header"][:70])
print("source:", out["source"])
