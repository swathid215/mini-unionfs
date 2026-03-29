#!/bin/bash

echo "🚀 Advanced UnionFS Whiteout Demo"
echo "================================="

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_ROOT" || exit 1

LOWER="$PROJECT_ROOT/test_lower"
UPPER="$PROJECT_ROOT/test_upper"
MNT="$PROJECT_ROOT/mnt"

echo "🧹 Cleaning old setup..."
sudo fusermount3 -u "$MNT" 2>/dev/null || fusermount -u "$MNT" 2>/dev/null || umount "$MNT" 2>/dev/null || true
rm -rf "$LOWER" "$UPPER" "$MNT"
mkdir -p "$LOWER/src" "$LOWER/docs" "$LOWER/config" "$UPPER" "$MNT"

echo "📁 Creating LOWER layer files..."

cat > "$LOWER/README.md" << 'EOF'
# UnionFS Lower Layer
This is the immutable lower layer.
EOF

cat > "$LOWER/config/app.json" << 'EOF'
{
  "name": "MiniUnionFS",
  "version": "1.0",
  "feature": "whiteout"
}
EOF

cat > "$LOWER/src/main.c" << 'EOF'
#include <stdio.h>
int main() { printf("demo\n"); return 0; }
EOF

cat > "$LOWER/docs/guide.txt" << 'EOF'
Whiteout guide:
rm file -> creates .wh.file
EOF

cat > "$LOWER/data.csv" << 'EOF'
name,layer,status
file1,lower,active
file2,lower,active
EOF

echo
echo "✅ LOWER contents:"
find "$LOWER" -type f | sort

echo
echo "🔨 Compiling..."
gcc -Wall -g -o unionfs main.c `pkg-config --cflags --libs fuse3`
if [ $? -ne 0 ]; then
    echo "❌ Compilation failed"
    exit 1
fi

echo
echo "🗻 Mounting..."
sudo ./unionfs "$LOWER" "$UPPER" "$MNT" -o allow_other &
PID=$!
sleep 3

if ! mount | grep -q "$MNT"; then
    echo "❌ Mount failed"
    exit 1
fi

echo
echo "📋 1. MERGED VIEW:"
find "$MNT" -maxdepth 3 -type f | sort

echo
echo "🗑️ 2. Deleting files from merged view..."
cd "$MNT" || exit 1
sudo rm -f README.md
sudo rm -f config/app.json
sudo rm -f src/main.c

echo
echo "👻 3. AFTER DELETE (merged view):"
find "$MNT" -maxdepth 3 -type f | sort

echo
echo "🛡️ 4. LOWER layer untouched:"
find "$LOWER" -maxdepth 3 -type f | sort

echo
echo "🎩 5. WHITEOUT files in UPPER:"
ls -la "$UPPER"
echo
find "$UPPER" -name ".wh.*" | sort

echo
echo "✅ 6. Nested whiteout test..."
sudo rm -f "$MNT/docs/guide.txt"

echo
echo "📂 UPPER after nested delete:"
find "$UPPER" -maxdepth 3 | sort
echo
echo "Hidden files explicitly:"
ls -la "$UPPER/docs"

echo
echo "🧹 Cleanup:"
cd "$PROJECT_ROOT" || exit 1
sudo fusermount3 -u "$MNT" 2>/dev/null || fusermount -u "$MNT" 2>/dev/null || umount "$MNT" 2>/dev/null || true

echo
echo "🎉 TEST COMPLETE"
echo "Expected whiteouts:"
echo "  $UPPER/.wh.README.md"
echo "  $UPPER/config/.wh.app.json"
echo "  $UPPER/src/.wh.main.c"
echo "  $UPPER/docs/.wh.guide.txt"