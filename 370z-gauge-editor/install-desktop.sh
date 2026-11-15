#!/bin/sh
# Adds "370Z Layout Editor" to the Ubuntu app menu (no sudo needed).
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/370z-layout-editor.desktop" <<EOT
[Desktop Entry]
Type=Application
Name=370Z Layout Editor
Comment=Design the gauge screens' custom views
Exec=python3 "$HERE/editor.py"
Icon=$HERE/web/icon.svg
Terminal=false
Categories=Development;
EOT
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
echo "Added. Find '370Z Layout Editor' in the app menu (you can pin it to the dock)."
