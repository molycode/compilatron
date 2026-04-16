#!/bin/bash
# Compilatron Distribution Package Creator

set -e

PACKAGE_NAME="compilatron"
ARCHIVE_NAME="${PACKAGE_NAME}.tar.gz"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

log_info "Creating distribution package..."

TEMP_DIR=$(mktemp -d)
PACKAGE_DIR="$TEMP_DIR/$PACKAGE_NAME"

log_info "Preparing files in: $PACKAGE_DIR"

mkdir -p "$PACKAGE_DIR"

# Core source and build files
cp CMakeLists.txt "$PACKAGE_DIR/"
cp CMakePresets.json "$PACKAGE_DIR/"
cp Makefile "$PACKAGE_DIR/"
cp setup.sh "$PACKAGE_DIR/"
cp README.md "$PACKAGE_DIR/"
cp LICENSE "$PACKAGE_DIR/"
cp -r cmake "$PACKAGE_DIR/"
cp -r code "$PACKAGE_DIR/"
cp -r resources "$PACKAGE_DIR/"

mkdir -p "$PACKAGE_DIR/external"

# ImGui — copy the full directory (CMakeLists.txt and all sources)
cp -r external/imgui "$PACKAGE_DIR/external/imgui"

# GLFW — built from source via tge_add_external()
cp -r external/glfw "$PACKAGE_DIR/external/glfw"

# Fonts — required at build time by code/CMakeLists.txt
cp -r external/fonts "$PACKAGE_DIR/external/fonts"

# tge-core — source snapshot (no .git, no external/glm which is 25 MB and only
# needed by TgeMath; keep external/rpmalloc which rpmalloc.cmake includes unconditionally)
log_info "Bundling tge-core snapshot (without .git and without glm)..."
cp -r external/tge-core "$PACKAGE_DIR/external/tge-core"
rm -rf "$PACKAGE_DIR/external/tge-core/.git"
rm -rf "$PACKAGE_DIR/external/tge-core/external/glm"

chmod +x "$PACKAGE_DIR/setup.sh"

# Remove stale build artifacts (top-level build/ only — code/build/ is source, not artifacts)
rm -rf "$PACKAGE_DIR/build"
find "$PACKAGE_DIR" -name "*.o" -delete 2>/dev/null || true
find "$PACKAGE_DIR" -name "Compilatron" -not -path "*/code/*" -delete 2>/dev/null || true

cd "$TEMP_DIR"
log_info "Creating archive: $ARCHIVE_NAME"
tar -czf "$ARCHIVE_NAME" "$PACKAGE_NAME"

mv "$ARCHIVE_NAME" "$SCRIPT_DIR/"

ARCHIVE_SIZE=$(du -h "$SCRIPT_DIR/$ARCHIVE_NAME" | cut -f1)
ARCHIVE_SHA256=$(sha256sum "$SCRIPT_DIR/$ARCHIVE_NAME" | cut -d' ' -f1)

rm -rf "$TEMP_DIR"

log_success "Distribution package created successfully!"
echo ""
echo "Package: $ARCHIVE_NAME ($ARCHIVE_SIZE)"
echo "SHA256: $ARCHIVE_SHA256"
echo ""
echo "To test: tar -xzf $ARCHIVE_NAME && cd $PACKAGE_NAME && ./setup.sh"
