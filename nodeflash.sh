#!/bin/sh
# nodeflash launcher for Linux, macOS and Git Bash: creates a private Python environment on first use.
HERE="$(cd "$(dirname "$0")" && pwd)"
PY="$HERE/.venv/bin/python"
[ -x "$HERE/.venv/Scripts/python.exe" ] && PY="$HERE/.venv/Scripts/python.exe"   # Windows venv
if [ ! -x "$PY" ]; then
  echo "First run: setting up nodeflash in $HERE/.venv ..."
  python3 -m venv "$HERE/.venv" 2>/dev/null || python -m venv "$HERE/.venv" || exit 1
  PY="$HERE/.venv/bin/python"
  [ -x "$HERE/.venv/Scripts/python.exe" ] && PY="$HERE/.venv/Scripts/python.exe"
  "$PY" -m pip install --quiet --upgrade pip
  "$PY" -m pip install --quiet -e "$HERE" || exit 1
fi
exec "$PY" -m nodeflash "$@"
