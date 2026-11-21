#!/usr/bin/env bash
# Download UniProtKB/Swiss-Prot (FASTA, ~90 MB compressed) and prepare it for HySW.
# Licence: CC BY 4.0 (UniProt LICENSE file, saved next to the data; keep it with any copy).
# Usage: scripts/get_swissprot.sh            -> data/swissprot_<release>/swissprot.bin
set -euo pipefail
cd "$(dirname "$0")/.."
U=https://ftp.uniprot.org/pub/databases/uniprot/current_release/knowledgebase/complete
rel=$(curl -fsS $U/reldate.txt | sed -n 's/.*Swiss-Prot Release \([0-9_]*\).*/\1/p')
D=data/swissprot_${rel:?could not read release}
mkdir -p "$D"
curl -fsS -o "$D/LICENSE" $U/LICENSE
curl -fsS -o "$D/reldate.txt" $U/reldate.txt
[[ -f "$D/uniprot_sprot.fasta.gz" ]] || curl -fS -o "$D/uniprot_sprot.fasta.gz" $U/uniprot_sprot.fasta.gz
gzip -t "$D/uniprot_sprot.fasta.gz"
sha256sum "$D/uniprot_sprot.fasta.gz" > "$D/SHA256SUMS"
gunzip -c "$D/uniprot_sprot.fasta.gz" > "$D/uniprot_sprot.fasta"
[[ -x bin/hysw_engine ]] || ./build.sh
bin/hysw_engine prep --fasta "$D/uniprot_sprot.fasta" --out "$D/swissprot.bin"
echo "ready: $D/swissprot.bin (release $rel)"
