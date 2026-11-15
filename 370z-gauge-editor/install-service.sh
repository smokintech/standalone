#!/bin/sh
# Run the layout editor all the time on this machine (the build server), for
# opening from the Surface's browser.
#
#   sudo ./install-service.sh          system service, runs as YOU (not root) - like gauge-publish.service
#   ./install-service.sh               user service, no sudo (needs a working "systemctl --user")
#   sudo ./install-service.sh remove   (or without sudo) stop and remove
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-8370}"
NAME=370z-layout-editor

if [ "$(id -u)" = 0 ]; then
  RUNAS="${SUDO_USER:-}"
  if [ -z "$RUNAS" ] || [ "$RUNAS" = root ]; then
    echo "Run this with sudo from your normal login (e.g. jester), not as root itself:"
    echo "  sudo ./install-service.sh"
    exit 1
  fi
  MODE=system
  UNIT="/etc/systemd/system/$NAME.service"
  RHOME="$(getent passwd "$RUNAS" | cut -d: -f6)"
  SC="systemctl"
else
  MODE=user
  RUNAS="$(id -un)"; RHOME="$HOME"
  UNIT="$HOME/.config/systemd/user/$NAME.service"
  SC="systemctl --user"
fi

if [ "$1" = "remove" ]; then
  $SC disable --now $NAME 2>/dev/null || true
  rm -f "$UNIT"; $SC daemon-reload
  echo "Removed ($MODE service)."; exit 0
fi

mkdir -p "$(dirname "$UNIT")"
if [ $MODE = system ]; then
  cat > "$UNIT" <<EOT
[Unit]
Description=370Z gauge layout editor
After=network-online.target

[Service]
Type=simple
User=$RUNAS
Group=$(id -gn "$RUNAS")
Environment=HOME=$RHOME
WorkingDirectory=$HERE
ExecStart=/usr/bin/python3 -u "$HERE/editor.py" --lan --no-browser --port $PORT
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOT
else
  cat > "$UNIT" <<EOT
[Unit]
Description=370Z gauge layout editor
After=network-online.target

[Service]
ExecStart=/usr/bin/python3 -u "$HERE/editor.py" --lan --no-browser --port $PORT
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOT
fi

$SC daemon-reload
$SC enable $NAME >/dev/null
$SC restart $NAME
sleep 1
$SC --no-pager --lines=5 status $NAME || true
IP=$(hostname -I 2>/dev/null | awk '{print $1}')
echo
echo "Installed as a $MODE service, running as $RUNAS."
echo "Open  http://${IP:-this-server}:$PORT/  on the Surface."
echo "Logs:  ${SC} status $NAME   /   journalctl ${MODE:+$( [ $MODE = user ] && echo --user )} -u $NAME"
if [ $MODE = user ] && ! loginctl show-user "$RUNAS" 2>/dev/null | grep -q "Linger=yes"; then
  echo "To keep it running when you're not logged in, run once:  sudo loginctl enable-linger $RUNAS"
fi
