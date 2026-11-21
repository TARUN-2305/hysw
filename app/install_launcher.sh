#!/usr/bin/env bash
# Adds a "HySW" entry to the desktop's application menu for this user (~/.local/share/applications).
# Remove it again with:  rm ~/.local/share/applications/hysw.desktop
set -eu
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
DEST="${XDG_DATA_HOME:-$HOME/.local/share}/applications/hysw.desktop"
mkdir -p "$(dirname "$DEST")"
sed -e "s|@ROOT@|$ROOT|g" "$ROOT/app/hysw.desktop.in" > "$DEST"
chmod +x "$DEST" "$ROOT/app/launch.sh"
echo "installed $DEST"
