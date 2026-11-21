"""Download and cache the AlphaFold DB models of the app's example proteins (app/examples.json), so the
3D view works offline in the demo. Uses the app's own fetch code. Data: AlphaFold DB, CC BY 4.0.

    .venv/bin/python -I scripts/cache_example_structures.py
"""
import json
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "app"))
import structures  # noqa: E402

for ex in json.load(open(os.path.join(ROOT, "app", "examples.json")))["examples"]:
    acc = ex["accession"]
    try:
        meta, cached = structures.fetch(acc)
    except structures.StructureError as e:
        print(f"{acc}: FAILED ({e.kind}) {e}")
        continue
    size = os.path.getsize(structures.model_path(acc))
    same = meta["sequence"] == ex["sequence"]
    print(f"{acc}: {'already cached' if cached else 'downloaded'} {meta['entryId']} v{meta['latestVersion']}, "
          f"{size / 1024:.0f} KB, mean pLDDT {meta['globalMetricValue']}, sequence identical to Swiss-Prot 2026_04: {same}")
    if not cached:
        time.sleep(2)          # be gentle with the public server
