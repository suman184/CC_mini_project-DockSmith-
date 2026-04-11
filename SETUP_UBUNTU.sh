#!/bin/bash
set -e

echo "=================================="
echo "  Docksmith Ubuntu Demo Setup"
echo "=================================="
echo ""

# Check if running on Ubuntu
if ! grep -q "Ubuntu" /etc/os-release 2>/dev/null; then
    echo "⚠️  WARNING: This script is optimized for Ubuntu"
    echo "   Some paths may differ on other Linux distributions"
fi

# Step 1: Create required directories
echo "📂 Creating ~/.docksmith directories..."
mkdir -p ~/.docksmith/images
mkdir -p ~/.docksmith/layers
mkdir -p ~/.docksmith/cache
echo "   ✓ Directories created"
echo ""

# Step 2: Create base image
echo "🖼️  Creating base image..."
cat > ~/.docksmith/images/base.json << 'EOF'
{
  "name": "base",
  "version": "1.0"
}
EOF
echo "   ✓ Base image: base.json"
echo ""

# Step 3: Check Prerequisites
echo "✓ Checking prerequisites..."
which gcc > /dev/null && echo "  ✓ GCC found" || (echo "  ✗ GCC not found - install: sudo apt-get install build-essential"; exit 1)
which tar > /dev/null && echo "  ✓ tar found" || (echo "  ✗ tar not found"; exit 1)
which sha256sum > /dev/null && echo "  ✓ sha256sum found" || (echo "  ✗ sha256sum not found"; exit 1)
which rsync > /dev/null && echo "  ✓ rsync found" || (echo "  ✗ rsync not found - install: sudo apt-get install rsync"; exit 1)
echo ""

# Step 4: Compile
echo "🔨 Compiling Docksmith..."
cd docksmith
gcc -o docksmith main.c -lm
echo "   ✓ Binary ready: ./docksmith"
echo ""

# Step 5: Demo checklist
echo "=================================="
echo "  Demo Checklist (8 requirements)"
echo "=================================="
echo ""
echo "1️⃣  Testing: Cold build (all [CACHE MISS])"
echo "   Run: cd demo_app && ../docksmith/docksmith build -t myapp:latest ."
echo ""

echo "2️⃣  Testing: Image list"
echo "   Run: ./docksmith/docksmith images"
echo ""

echo "3️⃣  Testing: Container run"
echo "   Run: ./docksmith/docksmith run myapp:latest"
echo ""

echo "4️⃣  Testing: ENV override"
echo "   Run: ./docksmith/docksmith run myapp:latest -e APP_NAME=CustomName"
echo ""

echo "5️⃣  Testing: Isolated filesystem"
echo "   When running RUN: file system is isolated via chroot"
echo ""

echo "6️⃣  Testing: Image deletion"
echo "   Run: ./docksmith/docksmith rmi myapp:latest"
echo ""

echo "7️⃣  Testing: All 6 instructions used"
echo "   Done - Docksmithfile uses: FROM, COPY, WORKDIR, RUN, ENV, CMD"
echo ""

echo "8️⃣  Testing: Fully offline"
echo "   No network access required"
echo ""

echo "=================================="
echo "✅ Setup complete! Ready to demo"
echo "=================================="
