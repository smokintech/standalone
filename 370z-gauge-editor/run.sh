#!/bin/sh
# Start the 370Z layout editor (opens in your browser).
cd "$(dirname "$0")" && exec python3 editor.py "$@"
