#!/usr/bin/env bash
set -euo pipefail

# install.sh — Symlink cc into your PATH
#
# Usage: ./install.sh              # install to ~/.local/bin/cc
#        ./install.sh /usr/local/bin/cc  # install to custom location

DEST="${1:-$HOME/.local/bin/cc}"
SRC="$(cd "$(dirname "$0")" && pwd)/cc"

if [[ ! -f "$SRC" ]]; then
  echo "Error: Cannot find cc script at $SRC" >&2
  exit 1
fi

# Ensure destination directory exists
mkdir -p "$(dirname "$DEST")"

# Remove old symlink if it exists
[[ -L "$DEST" ]] && rm "$DEST"

# Create symlink
ln -s "$SRC" "$DEST"
chmod +x "$SRC"

echo "Installed cc → $DEST"
echo ""
echo "Next steps:"
echo "  1. cp providers.env.example ~/.config/cc/providers.env"
echo "  2. Edit ~/.config/cc/providers.env with your API keys"
echo "  3. Run: cc"
