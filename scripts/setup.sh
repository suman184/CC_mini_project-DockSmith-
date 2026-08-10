#!/usr/bin/env bash
#
# One-time setup: create the docksmith store under ~/.docksmith and import the
# base image. Everything docksmith does afterwards is fully offline.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STORE="$HOME/.docksmith"

echo "docksmith setup"
echo

# --- prerequisites -----------------------------------------------------------
missing=0
for tool in gcc tar sha256sum rsync; do
    if command -v "$tool" >/dev/null 2>&1; then
        echo "  found  $tool"
    else
        echo "  MISSING $tool"
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    echo
    echo "Install the missing tools first:"
    echo "  sudo apt-get install build-essential rsync coreutils tar"
    exit 1
fi

if [ "$(uname -s)" != "Linux" ]; then
    echo
    echo "warning: docksmith needs Linux — chroot(2) is the isolation primitive."
    echo "         Run this inside a Linux VM."
fi

# --- store layout ------------------------------------------------------------
echo
echo "Creating $STORE"
mkdir -p "$STORE/images" "$STORE/layers" "$STORE/cache"
echo "  images/  image manifests"
echo "  layers/  content-addressed layer tars"
echo "  cache/   build cache index"

# --- base image --------------------------------------------------------------
echo
echo "Importing base image"
cp "$REPO_ROOT/scripts/base-image.json" "$STORE/images/base.json"
echo "  base:latest imported"

# --- build -------------------------------------------------------------------
echo
echo "Building docksmith"
make -C "$REPO_ROOT" >/dev/null
echo "  binary ready: $REPO_ROOT/docksmith"

echo
echo "Setup complete. Next:"
echo "  cd examples/demo-app && sudo ../../docksmith build -t myapp:latest ."
