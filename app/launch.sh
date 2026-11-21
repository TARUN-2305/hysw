#!/usr/bin/env bash
# HySW launcher: builds the engine if it is missing, starts the local server (127.0.0.1 only) unless it is
# already running, and opens the app in the default browser. Used by the desktop entry (app/install_launcher.sh).
#   app/launch.sh          start (or reopen) the app
#   app/launch.sh --stop   stop the server started by this script
set -u
cd "$(dirname "$(readlink -f "$0")")/.."
PORT=${HYSW_PORT:-8372}
URL="http://127.0.0.1:$PORT/"
RUN="${XDG_RUNTIME_DIR:-/tmp}/hysw"
mkdir -p "$RUN"
PIDFILE="$RUN/server.pid" LOG="$RUN/server.log"

say() {                                   # message to the terminal and, without one, to the desktop
    echo "HySW: $*"
    if [ ! -t 1 ] && command -v notify-send >/dev/null; then notify-send "HySW" "$*"; fi
}
running() { curl -s -m 2 -o /dev/null -w '%{http_code}' "$URL/api/system" 2>/dev/null | grep -q 200; }

if [ "${1:-}" = "--stop" ]; then
    if [ -f "$PIDFILE" ] && kill "$(cat "$PIDFILE")" 2>/dev/null; then rm -f "$PIDFILE"; say "stopped"; else say "not running"; fi
    exit 0
fi
if ! running; then
    if [ ! -x bin/hysw_engine ]; then
        say "building the engine (first start)…"
        ./build.sh >> "$LOG" 2>&1 || { say "the engine did not build; see $LOG"; exit 1; }
    fi
    if [ ! -x .venv/bin/python ]; then
        say "Python environment missing: run  python3 -m venv .venv && .venv/bin/pip install -r requirements.txt"; exit 1
    fi
    nohup .venv/bin/python -I app/server.py --port "$PORT" >> "$LOG" 2>&1 &
    echo $! > "$PIDFILE"
    for _ in $(seq 1 50); do running && break; sleep 0.2; done
    running || { say "the server did not start; see $LOG"; exit 1; }
fi
xdg-open "$URL" >/dev/null 2>&1 || say "open $URL in your browser"
