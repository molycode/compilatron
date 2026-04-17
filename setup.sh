#!/bin/bash

# Compilatron Cross-Distribution Setup Script
# Automatically detects Linux distribution and installs dependencies

set -e  # Exit on any error

# Always run from the directory containing this script
SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Pinned versions for auto-download (update these on new releases)
CMAKE_DOWNLOAD_VERSION="4.3.1"
NINJA_DOWNLOAD_VERSION="1.12.1"

# Download a URL to a destination file using curl or wget.
download_file() {
    local url="$1"
    local dest="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --progress-bar "$url" -o "$dest"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --show-progress "$url" -O "$dest"
    else
        log_error "Neither curl nor wget is available — cannot download"
        return 1
    fi
}

# Extract a zip file to a destination directory using unzip or python3.
extract_zip() {
    local zip="$1"
    local dest="$2"
    if command -v unzip >/dev/null 2>&1; then
        unzip -q "$zip" -d "$dest"
    elif command -v python3 >/dev/null 2>&1; then
        python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$zip" "$dest"
    else
        log_error "Need unzip or python3 to extract $(basename "$zip")"
        return 1
    fi
}

# Download cmake CMAKE_DOWNLOAD_VERSION to tools/cmake/ and set CMAKE_EXEC.
download_cmake() {
    local arch
    arch=$(uname -m)
    local arch_tag=""

    case "$arch" in
        x86_64)  arch_tag="x86_64" ;;
        aarch64) arch_tag="aarch64" ;;
        *)
            log_error "Automatic cmake download is not supported on $arch."
            log_info "Download cmake manually from https://cmake.org/download/ then run:"
            echo "    ./setup.sh --cmake /path/to/cmake"
            exit 1
            ;;
    esac

    local archive="cmake-${CMAKE_DOWNLOAD_VERSION}-linux-${arch_tag}.tar.gz"
    local url="https://github.com/Kitware/CMake/releases/download/v${CMAKE_DOWNLOAD_VERSION}/${archive}"
    local tools_dir="$SCRIPT_DIR/tools"

    log_info "Downloading cmake ${CMAKE_DOWNLOAD_VERSION} (~62 MiB)..."
    mkdir -p "$tools_dir"

    if download_file "$url" "$tools_dir/$archive"; then
        log_info "Extracting cmake..."
        tar xf "$tools_dir/$archive" -C "$tools_dir"
        rm -f "$tools_dir/$archive"
        local cmake_bin
        cmake_bin=$(find "$tools_dir" -maxdepth 3 -name cmake -path "*/bin/cmake" 2>/dev/null | head -1)
        if [ -n "$cmake_bin" ] && cmake_version_ok "$cmake_bin"; then
            local version
            version=$("$cmake_bin" --version 2>/dev/null | head -1 | grep -oP '\d+\.\d+\.\d+' | head -1)
            log_success "cmake ${version} downloaded to tools/"
            CMAKE_EXEC="$cmake_bin"
        else
            log_error "Downloaded cmake not found or version check failed"
            exit 1
        fi
    else
        log_error "cmake download failed"
        exit 1
    fi
}

# Download ninja NINJA_DOWNLOAD_VERSION to tools/ninja/ and add it to PATH.
download_ninja() {
    if [ "$(uname -m)" != "x86_64" ]; then
        log_error "Automatic ninja download is only available on x86_64."
        log_info "Install ninja manually, then re-run setup.sh"
        exit 1
    fi

    local tools_ninja="$SCRIPT_DIR/tools/ninja"
    local url="https://github.com/ninja-build/ninja/releases/download/v${NINJA_DOWNLOAD_VERSION}/ninja-linux.zip"

    log_info "Downloading ninja ${NINJA_DOWNLOAD_VERSION} (~131 KiB)..."
    mkdir -p "$tools_ninja"

    if download_file "$url" "$tools_ninja/ninja-linux.zip"; then
        extract_zip "$tools_ninja/ninja-linux.zip" "$tools_ninja"
        rm -f "$tools_ninja/ninja-linux.zip"
        chmod +x "$tools_ninja/ninja"
        export PATH="$tools_ninja:$PATH"
        log_success "ninja downloaded to tools/ninja/"
        log_info "To use outside of setup: export PATH=\"$tools_ninja:\$PATH\""
    else
        log_error "ninja download failed"
        exit 1
    fi
}

# Detect distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO="$ID"
        DISTRO_VERSION="$VERSION_ID"
        DISTRO_NAME="$PRETTY_NAME"
    else
        log_error "Cannot detect Linux distribution"
        exit 1
    fi
}

# Check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Returns the installed version of a package, or empty string if not installed
get_package_version() {
    local pkg="$1"
    case "$DISTRO" in
        ubuntu|debian)
            dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null | grep -v '^$' || true
            ;;
        fedora|centos|rhel|rocky|almalinux)
            rpm -q --queryformat '%{VERSION}-%{RELEASE}' "$pkg" 2>/dev/null \
                | grep -v 'not installed' || true
            ;;
        opensuse*|suse)
            rpm -q --queryformat '%{VERSION}-%{RELEASE}' "$pkg" 2>/dev/null \
                | grep -v 'not installed' || true
            ;;
        arch|manjaro)
            pacman -Q "$pkg" 2>/dev/null | awk '{print $2}' || true
            ;;
    esac
}

# Install a specific list of packages using the distro's package manager
install_packages() {
    case "$DISTRO" in
        ubuntu|debian)
            sudo apt update
            sudo apt install -y "$@"
            ;;
        fedora|centos|rhel|rocky|almalinux)
            local mgr="dnf"; command_exists dnf || mgr="yum"
            sudo $mgr install -y "$@"
            ;;
        opensuse*|suse)
            sudo zypper install -y "$@"
            ;;
        arch|manjaro)
            sudo pacman -S --needed "$@"
            ;;
        *)
            log_error "Unsupported distribution: $DISTRO — cannot install packages"
            exit 1
            ;;
    esac
}

# Returns the distro-specific package name for ninja.
ninja_pkg_name() {
    case "$DISTRO" in
        ubuntu|debian|fedora|centos|rhel|rocky|almalinux) echo "ninja-build" ;;
        *) echo "ninja" ;;
    esac
}

# Ensure ninja is available: check PATH, tools/, package manager, then offer download.
ensure_ninja() {
    echo ""
    echo "NINJA:"
    echo ""

    # Already in PATH
    if command_exists ninja; then
        printf "  ${GREEN}✓${NC}  ninja  %s\n" "$(command -v ninja)"
        return
    fi

    # Previously downloaded to tools/
    if [ -x "$SCRIPT_DIR/tools/ninja/ninja" ]; then
        export PATH="$SCRIPT_DIR/tools/ninja:$PATH"
        printf "  ${GREEN}✓${NC}  ninja  %s\n" "$SCRIPT_DIR/tools/ninja/ninja"
        return
    fi

    printf "  ${RED}✗${NC}  ninja  not found\n"
    echo ""

    if [ "$YES_MODE" = true ]; then
        # Try package manager first, then auto-download
        log_info "Non-interactive mode: installing ninja via package manager..."
        install_packages "$(ninja_pkg_name)" 2>/dev/null || true
        if command_exists ninja; then
            log_success "ninja installed"
            return
        fi
        log_info "Package manager install failed — downloading ninja ${NINJA_DOWNLOAD_VERSION}..."
        download_ninja
        return
    fi

    echo "  1. Install via package manager (requires sudo)"
    echo "  2. Download ninja ${NINJA_DOWNLOAD_VERSION} binary (~131 KiB) to tools/ninja/"
    echo "  3. Exit and install manually"
    echo ""
    read -p "Choose option (1-3): " -r

    case "$REPLY" in
        1)
            install_packages "$(ninja_pkg_name)"
            if ! command_exists ninja; then
                log_warning "Package manager install failed — trying download..."
                download_ninja
            fi
            ;;
        2) download_ninja ;;
        *)
            log_error "ninja is required to build. Install it and re-run setup.sh."
            exit 1
            ;;
    esac

    if ! command_exists ninja; then
        log_error "ninja is still not available — cannot continue"
        exit 1
    fi
}

# Check required packages, display status table, install any missing ones or exit.
check_and_install_dependencies() {
    local packages=()
    local binaries=()   # binary name for tool packages; empty string for dev libraries
    local pkg_mgr_hint=""

    case "$DISTRO" in
        ubuntu|debian)
            packages=(pkg-config   libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev)
            binaries=(pkg-config   ""              ""         ""            ""              ""             ""       )
            pkg_mgr_hint="sudo apt install"
            ;;
        fedora|centos|rhel|rocky|almalinux)
            packages=(mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel)
            binaries=(""               ""           ""              ""                ""              ""          )
            local mgr="dnf"; command_exists dnf || mgr="yum"
            pkg_mgr_hint="sudo $mgr install"
            ;;
        opensuse*|suse)
            packages=(pkg-config   Mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel)
            binaries=(pkg-config   ""               ""           ""              ""                ""              ""          )
            pkg_mgr_hint="sudo zypper install"
            ;;
        arch|manjaro)
            packages=(mesa libx11 libxrandr libxinerama libxcursor libxi)
            binaries=(""   ""     ""        ""           ""         ""  )
            pkg_mgr_hint="sudo pacman -S"
            ;;
        *)
            log_warning "Unsupported distribution: $DISTRO — skipping dependency check"
            log_info "Ensure these are installed manually: OpenGL dev headers, X11 dev headers"
            return
            ;;
    esac

    echo ""
    echo "SYSTEM DEPENDENCIES:"
    echo ""

    local missing=()
    for i in "${!packages[@]}"; do
        local pkg="${packages[$i]}"
        local bin="${binaries[$i]}"
        local version
        version=$(get_package_version "$pkg")
        if [ -n "$version" ]; then
            local path_info
            if [ -n "$bin" ]; then
                path_info=$(command -v "$bin" 2>/dev/null || echo "$bin")
            else
                path_info="(dev headers)"
            fi
            printf "  ${GREEN}✓${NC}  %-22s %-26s %s\n" "$pkg" "$version" "$path_info"
        else
            printf "  ${RED}✗${NC}  %-22s not installed\n" "$pkg"
            missing+=("$pkg")
        fi
    done

    echo ""

    if [ ${#missing[@]} -eq 0 ]; then
        log_success "All dependencies satisfied"
    else
        log_warning "${#missing[@]} package(s) missing"
        echo ""
        local do_install=true
        if [ "$YES_MODE" = false ]; then
            read -p "Install missing packages now? (Y/n): " -r
            if [[ $REPLY =~ ^[Nn]$ ]]; then
                do_install=false
            fi
        fi
        if [ "$do_install" = true ]; then
            install_packages "${missing[@]}"
            log_success "Dependencies installed"
        else
            echo ""
            log_error "Cannot continue — the following packages are required:"
            for pkg in "${missing[@]}"; do
                echo "    $pkg"
            done
            echo ""
            log_info "Install them manually, then re-run setup.sh:"
            echo "    $pkg_mgr_hint ${missing[*]}"
            echo ""
            exit 1
        fi
    fi
}

# Minimum required cmake version
CMAKE_MIN_MAJOR=4
CMAKE_MIN_MINOR=3
CMAKE_MIN_PATCH=0

# Returns 0 if the given cmake binary meets the minimum version
cmake_version_ok() {
    local cmake_bin="$1"
    local version
    version=$("$cmake_bin" --version 2>/dev/null | head -1 | grep -oP '\d+\.\d+\.\d+' | head -1)
    [ -z "$version" ] && return 1

    local major minor patch
    major=$(echo "$version" | cut -d. -f1)
    minor=$(echo "$version" | cut -d. -f2)
    patch=$(echo "$version" | cut -d. -f3)

    if [ "$major" -gt "$CMAKE_MIN_MAJOR" ]; then return 0; fi
    if [ "$major" -lt "$CMAKE_MIN_MAJOR" ]; then return 1; fi
    if [ "$minor" -gt "$CMAKE_MIN_MINOR" ]; then return 0; fi
    if [ "$minor" -lt "$CMAKE_MIN_MINOR" ]; then return 1; fi
    [ "$patch" -ge "$CMAKE_MIN_PATCH" ]
}

# Find a cmake >= CMAKE_MIN version.
# Sets CMAKE_EXEC on success; exits with error on failure.
# Argument: optional explicit path (binary, bin/ dir, or install root)
find_cmake() {
    local custom_path="$1"
    CMAKE_EXEC=""

    if [ -n "$custom_path" ]; then
        local resolved=""

        if [ -x "$custom_path" ] && [ -f "$custom_path" ]; then
            resolved="$custom_path"
        elif [ -d "$custom_path" ]; then
            if [ -x "$custom_path/cmake" ]; then
                resolved="$custom_path/cmake"
            elif [ -x "$custom_path/bin/cmake" ]; then
                resolved="$custom_path/bin/cmake"
            fi
        fi

        if [ -z "$resolved" ]; then
            log_error "cmake not found at: $custom_path"
            exit 1
        fi

        local version
        version=$("$resolved" --version 2>/dev/null | head -1 | grep -oP '\d+\.\d+\.\d+' | head -1)

        if cmake_version_ok "$resolved"; then
            log_success "CMake $version (meets ${CMAKE_MIN_MAJOR}.${CMAKE_MIN_MINOR}.${CMAKE_MIN_PATCH}+ requirement)"
            CMAKE_EXEC="$resolved"
            return
        else
            log_error "CMake $version at $resolved is too old — need ${CMAKE_MIN_MAJOR}.${CMAKE_MIN_MINOR}.${CMAKE_MIN_PATCH}+"
            exit 1
        fi
    fi

    # Try system cmake first
    if command_exists cmake; then
        local version
        version=$(cmake --version 2>/dev/null | head -1 | grep -oP '\d+\.\d+\.\d+' | head -1)

        if cmake_version_ok "cmake"; then
            log_success "CMake $version (meets ${CMAKE_MIN_MAJOR}.${CMAKE_MIN_MINOR}.${CMAKE_MIN_PATCH}+ requirement)"
            CMAKE_EXEC="cmake"
            return
        else
            log_warning "System cmake $version is too old (need ${CMAKE_MIN_MAJOR}.${CMAKE_MIN_MINOR}.${CMAKE_MIN_PATCH}+)"
        fi
    else
        log_warning "cmake not found in PATH"
    fi

    # Search common non-system locations for a newer cmake
    log_info "Searching for cmake ${CMAKE_MIN_MAJOR}.${CMAKE_MIN_MINOR}.${CMAKE_MIN_PATCH}+ in common locations..."
    local search_patterns=(
        "$SCRIPT_DIR/tools/cmake*/bin/cmake"
        "$HOME/cmake-*/bin/cmake"
        "$HOME/.local/cmake-*/bin/cmake"
        "/opt/cmake*/bin/cmake"
        "/usr/local/cmake*/bin/cmake"
        "/usr/local/bin/cmake"
    )

    for pattern in "${search_patterns[@]}"; do
        for candidate in $pattern; do
            if [ -x "$candidate" ] && cmake_version_ok "$candidate"; then
                local version
                version=$("$candidate" --version 2>/dev/null | head -1 | grep -oP '\d+\.\d+\.\d+' | head -1)
                log_success "Found cmake $version at $candidate"
                CMAKE_EXEC="$candidate"
                return
            fi
        done
    done

    # Nothing usable found — offer to download before exiting
    log_error "No cmake ${CMAKE_MIN_MAJOR}.${CMAKE_MIN_MINOR}.${CMAKE_MIN_PATCH}+ found!"
    echo ""
    echo "Compilatron requires CMake ${CMAKE_MIN_MAJOR}.${CMAKE_MIN_MINOR}.${CMAKE_MIN_PATCH} or newer."
    echo ""

    if [ "$YES_MODE" = true ]; then
        log_info "Non-interactive mode: downloading cmake ${CMAKE_DOWNLOAD_VERSION}..."
        download_cmake
        return
    fi

    echo "  1. Download cmake ${CMAKE_DOWNLOAD_VERSION} automatically to tools/cmake/  (recommended)"
    echo "  2. Point to an existing cmake:  ./setup.sh --cmake /path/to/cmake"
    echo "  3. Download manually from https://cmake.org/download/"
    echo ""
    read -p "Download cmake ${CMAKE_DOWNLOAD_VERSION} now? (Y/n): " -r
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        download_cmake
        return
    fi

    echo ""
    log_info "Re-run with --cmake once you have a suitable installation:"
    echo "    ./setup.sh --cmake /path/to/cmake-${CMAKE_DOWNLOAD_VERSION}-linux-x86_64"
    echo ""
    exit 1
}

# Test if compiler supports C++23
test_cpp23_support() {
    local compiler="$1"
    if [ -z "$compiler" ] || ! command -v "$compiler" >/dev/null 2>&1; then
        return 1
    fi

    # Try to compile a simple C++23 program
    echo 'int main(){ return 0; }' | "$compiler" -std=c++23 -x c++ - -o /dev/null >/dev/null 2>&1
}

# Detect available compilers with C++23 support
detect_compilers() {
    log_info "Detecting C++ compilers with C++23 support..."

    COMPILERS=""
    local cpp23_compilers=""

    # Dynamically find ALL C++ compilers available on the system

    # Search in PATH and common compiler directories
    # Filter out Windows paths in WSL to speed up scanning
    local filtered_path=$(echo "$PATH" | tr ':' '\n' | grep -v '/mnt/c' | tr '\n' ':' | sed 's/:$//')
    local search_paths="$filtered_path:/usr/bin:/usr/local/bin:/opt/*/bin"

    local original_path_count=$(echo "$PATH" | tr ':' '\n' | wc -l)
    local filtered_path_count=$(echo "$filtered_path" | tr ':' '\n' | wc -l)

    if [ "$original_path_count" != "$filtered_path_count" ]; then
        local windows_paths=$((original_path_count - filtered_path_count))
        log_info "Analyzing PATH: $original_path_count directories ($windows_paths Windows paths filtered out)"
    else
        log_info "Analyzing PATH with $original_path_count directories..."
    fi

    # Find all potential C++ compiler executables
    local all_compilers=""

    # Search PATH (with filtered PATH for performance)
    log_info "Scanning command database..."
    start_time=$(date +%s)

    # Temporarily set PATH to filtered version for compgen
    local original_path="$PATH"
    export PATH="$filtered_path"

    total_commands=$(compgen -c 2>/dev/null | wc -l)
    log_info "  Found $total_commands total commands in filtered PATH"

    all_compilers=$(compgen -c 2>/dev/null | grep -E '^(g\+\+|clang\+\+|c\+\+)(-[0-9]|$)' | sort -u)

    # Restore original PATH
    export PATH="$original_path"

    end_time=$(date +%s)
    scan_duration=$((end_time - start_time))
    log_info "  Command database scan completed in ${scan_duration}s"

    # Search common directories for versioned compilers
    log_info "Searching directories for additional compilers..."
    local search_count=0
    for search_dir in $(echo "$search_paths" | tr ':' '\n' | sort -u); do
        if [ -d "$search_dir" ] && [ -r "$search_dir" ]; then
            search_count=$((search_count + 1))
            log_info "  Checking $search_dir..."

            # Find g++, clang++, and versioned variants
            found_compilers=$(find "$search_dir" -maxdepth 1 -name 'g++*' -executable 2>/dev/null)
            found_compilers="$found_compilers $(find "$search_dir" -maxdepth 1 -name 'clang++*' -executable 2>/dev/null)"
            found_compilers="$found_compilers $(find "$search_dir" -maxdepth 1 -name 'c++*' -executable 2>/dev/null)"

            # Extract just the executable names
            local found_count=0
            for compiler_path in $found_compilers; do
                if [ -x "$compiler_path" ]; then
                    compiler_name=$(basename "$compiler_path")
                    all_compilers="$all_compilers $compiler_name"
                    found_count=$((found_count + 1))
                fi
            done

            if [ $found_count -gt 0 ]; then
                log_info "    Found $found_count compilers"
            fi
        fi
    done

    # Deduplicate and sort
    log_info "Processing and deduplicating results..."
    local test_compilers=$(echo "$all_compilers" | tr ' ' '\n' | grep -E '^(g\+\+|clang\+\+|c\+\+)(-[0-9]|$)' | sort -u | tr '\n' ' ')

    log_info "Found $(echo $test_compilers | wc -w) potential C++ compilers: $(echo $test_compilers | tr ' ' ',')"

    for compiler in $test_compilers; do
        if command_exists "$compiler"; then
            log_info "Testing $compiler for C++23 support..."
            if test_cpp23_support "$compiler"; then
                version=$($compiler --version 2>/dev/null | head -1 | cut -d' ' -f1-4)
                log_success "✓ C++23 compatible: $compiler ($version)"
                cpp23_compilers="$cpp23_compilers $compiler"
            else
                version=$($compiler --version 2>/dev/null | head -1 | cut -d' ' -f1-4)
                log_warning "✗ No C++23 support: $compiler ($version)"
            fi
        else
            log_info "Checking $compiler... not found"
        fi
    done

    COMPILERS="$cpp23_compilers"

    if [ -z "$COMPILERS" ]; then
        log_error "No C++23 compatible compilers found!"
        echo ""
        echo "Compilatron requires C++23 support. Please install:"
        echo "  - GCC 13+ (g++-13 or newer)"
        echo "  - Clang 15+ (clang++-15 or newer)"
        echo ""
        echo "On Ubuntu 22.04:"
        echo "  sudo apt install gcc-13 g++-13"
        echo "  sudo apt install clang-15 clang++-15"
    else
        log_success "Found ${COMPILERS// /,} with C++23 support"
    fi
}

# Resolve a path (file, bin/ dir, or install root) to a C++23-capable compiler binary.
# Sets COMPILER_EXEC on success; exits with error on failure.
resolve_compiler_path() {
    local input="$1"

    # Strip quotes
    input=$(echo "$input" | sed "s/^['\"]//;s/['\"]$//")

    if [ -z "$input" ]; then
        log_error "Empty compiler path"
        exit 1
    fi

    local resolved=""

    if [ -x "$input" ] && [ -f "$input" ]; then
        resolved="$input"
    elif [ -d "$input" ]; then
        log_info "Searching for compiler in directory: $input"
        if [ -d "$input/bin" ]; then
            resolved=$(find_compiler_in_dir "$input/bin")
        fi
        if [ -z "$resolved" ]; then
            resolved=$(find_compiler_in_dir "$input")
        fi
    elif [ -f "$input" ]; then
        log_error "File exists but is not executable: $input"
        exit 1
    else
        log_error "Path not found: $input"
        exit 1
    fi

    if [ -z "$resolved" ]; then
        log_error "No C++23 compatible compiler found in: $input"
        if [ -d "$input" ]; then
            log_info "Searched for C++23 compatible: g++, clang++, c++, gcc, clang, cc"
        fi
        exit 1
    fi

    if ! test_cpp23_support "$resolved"; then
        local version
        version=$("$resolved" --version 2>/dev/null | head -1 || echo "Unknown version")
        log_error "Compiler does not support C++23: $resolved ($version)"
        log_error "Compilatron requires C++23. Please use GCC 13+ or Clang 15+"
        exit 1
    fi

    local version
    version=$("$resolved" --version 2>/dev/null | head -1 || echo "Unknown version")
    log_success "Using compiler: $resolved"
    log_info "Version: $version"
    COMPILER_EXEC="$resolved"
}

# Find a C++23 capable compiler.
# Sets COMPILER_EXEC on success; exits with error on failure.
# Argument: optional explicit path (binary, bin/ dir, or install root)
find_compiler() {
    local custom_path="$1"
    COMPILER_EXEC=""

    if [ -n "$custom_path" ]; then
        resolve_compiler_path "$custom_path"
        return
    fi

    # Interactive: detect then ask user to select
    detect_compilers
    select_compiler
}

# Warn if the chosen compiler ships a newer libstdc++ than the system provides.
# Compares the max GLIBCXX version symbol between the compiler's own libstdc++ and
# the system's. If the compiler's is newer, binaries it produces may fail to run.
# No-op when --static is already set.
warn_if_runtime_mismatch() {
    [ "$STATIC_BUILD" = true ] && return

    # Resolve the libstdc++ the compiler ships with
    local compiler_libstdcxx
    compiler_libstdcxx=$(realpath "$("$COMPILER_EXEC" -print-file-name=libstdc++.so.6 2>/dev/null)" 2>/dev/null)
    [ -z "$compiler_libstdcxx" ] || [ ! -f "$compiler_libstdcxx" ] && return

    # Resolve the system libstdc++
    local system_libstdcxx
    system_libstdcxx=$(ldconfig -p 2>/dev/null | grep 'libstdc++\.so\.6 ' | awk '{print $NF}' | head -1)
    [ -z "$system_libstdcxx" ] || [ ! -f "$system_libstdcxx" ] && return

    # Same file — no mismatch possible
    [ "$compiler_libstdcxx" = "$(realpath "$system_libstdcxx" 2>/dev/null)" ] && return

    # Compare max versioned GLIBCXX symbol in each library
    local compiler_max system_max
    compiler_max=$(strings "$compiler_libstdcxx" | grep '^GLIBCXX_[0-9]' | sort -V | tail -1)
    system_max=$(strings "$system_libstdcxx"   | grep '^GLIBCXX_[0-9]' | sort -V | tail -1)
    [ -z "$compiler_max" ] || [ -z "$system_max" ] && return

    # Warn only when the compiler's libstdc++ is strictly newer
    if [ "$(printf '%s\n%s' "$system_max" "$compiler_max" | sort -V | tail -1)" = "$compiler_max" ] &&
       [ "$compiler_max" != "$system_max" ]; then
        echo ""
        log_warning "Compiler libstdc++ ($compiler_max) is newer than the system libstdc++ ($system_max)."
        log_warning "Compilatron will fail to run on this machine without the newer libstdc++ at runtime."
        log_warning "Pass --static to produce a self-contained binary that avoids this."
        echo ""
        if [ "$YES_MODE" = true ]; then
            STATIC_BUILD=true
            log_info "Non-interactive mode: enabling static linking automatically"
        else
            read -p "Enable static linking to produce a self-contained binary? (Y/n): " -r
            if [[ ! $REPLY =~ ^[Nn]$ ]]; then
                STATIC_BUILD=true
                log_success "Static linking enabled"
            else
                log_info "Continuing without static linking"
            fi
        fi
        echo ""
    fi
}

# Find C++23 compatible compiler in directory
find_compiler_in_dir() {
    local dir="$1"
    local found_compiler=""

    # Try different compiler names in order of preference
    for compiler_name in g++ clang++ c++ gcc clang cc; do
        local full_path="$dir/$compiler_name"
        if [ -x "$full_path" ] && test_cpp23_support "$full_path"; then
            found_compiler="$full_path"
            break
        fi
    done

    echo "$found_compiler"
}

# Group compilers by suite (GCC/Clang and version)
group_compiler_suites() {
    # Arrays to store unique suites
    declare -A suite_map
    declare -a suite_order

    for compiler in $COMPILERS; do
        # Get version information
        local version_info=$($compiler --version 2>/dev/null | head -1)

        # Extract compiler type and version
        local compiler_type=""
        local version_number=""

        if echo "$version_info" | grep -qi "clang"; then
            compiler_type="Clang"
            version_number=$(echo "$version_info" | grep -oP 'version \K[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        elif echo "$version_info" | grep -qi "gcc\|g++"; then
            compiler_type="GCC"
            version_number=$(echo "$version_info" | grep -oP '\(.*\) \K[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        elif echo "$version_info" | grep -qi "ubuntu"; then
            # Handle "c++ (Ubuntu ...)" format
            if echo "$version_info" | grep -qi "gcc"; then
                compiler_type="GCC"
            else
                compiler_type="GCC"  # c++ is usually gcc
            fi
            version_number=$(echo "$version_info" | grep -oP '\) \K[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        fi

        # Create suite key
        if [ -n "$compiler_type" ] && [ -n "$version_number" ]; then
            local suite_key="${compiler_type} ${version_number}"

            # Add to map if not already present
            if [ -z "${suite_map[$suite_key]}" ]; then
                suite_map[$suite_key]="$compiler"
                suite_order+=("$suite_key")
            fi
        fi
    done

    # Export results
    COMPILER_SUITES=("${suite_order[@]}")
    declare -gA SUITE_COMPILER_MAP
    for key in "${suite_order[@]}"; do
        SUITE_COMPILER_MAP[$key]="${suite_map[$key]}"
    done
}

# Ask for compiler selection
select_compiler() {
    # Group compilers by suite
    group_compiler_suites

    # Non-interactive: auto-select first detected compiler
    if [ "$YES_MODE" = true ]; then
        if [ ${#COMPILER_SUITES[@]} -gt 0 ]; then
            local selected_suite="${COMPILER_SUITES[0]}"
            COMPILER_EXEC="${SUITE_COMPILER_MAP[$selected_suite]}"
            log_info "Non-interactive mode: selected $selected_suite"
            return
        elif [ -n "$COMPILERS" ]; then
            COMPILER_EXEC=$(echo "$COMPILERS" | awk '{print $1}')
            log_info "Non-interactive mode: selected $COMPILER_EXEC"
            return
        fi
        log_error "No C++23 compiler detected — use --compiler to specify one"
        exit 1
    fi

    echo ""
    echo "COMPILER SELECTION:"
    echo ""
    echo "Available compilers:"

    local option_num=1
    for suite in "${COMPILER_SUITES[@]}"; do
        echo "  $option_num. Use $suite"
        option_num=$((option_num + 1))
    done
    echo "  $option_num. Specify custom compiler path or directory"
    echo ""

    local max_option=$option_num
    read -p "Choose option (1-${max_option}): " -r

    # Check if user chose a compiler suite
    if [[ $REPLY =~ ^[0-9]+$ ]] && [ "$REPLY" -ge 1 ] && [ "$REPLY" -lt "$max_option" ]; then
        local selected_suite="${COMPILER_SUITES[$((REPLY - 1))]}"
        local selected_compiler="${SUITE_COMPILER_MAP[$selected_suite]}"
        local version
        version=$("$selected_compiler" --version 2>/dev/null | head -1 || echo "Unknown version")
        log_success "Using $selected_suite"
        log_info "Compiler: $selected_compiler ($version)"
        COMPILER_EXEC="$selected_compiler"
        return
    fi

    # User chose custom path option
    if [[ $REPLY == "$max_option" ]]; then
        echo ""
        echo "You can specify either:"
        echo "  - Full path to compiler: /path/to/g++"
        echo "  - Directory path: /path/to/compiler/bin (script will find g++/clang++)"
        echo ""
        echo "Note: Paths with spaces can be quoted with single or double quotes"
        echo ""
        read -p "Enter compiler path or directory: " -r CUSTOM_PATH

        if [ -z "$CUSTOM_PATH" ]; then
            log_error "No path provided"
            log_info "Use --compiler to specify a path: ./setup.sh --compiler /path/to/g++"
            exit 1
        fi

        resolve_compiler_path "$CUSTOM_PATH"
        return
    fi

    # Invalid selection — fall back to first detected compiler or exit
    if [ -n "$COMPILERS" ]; then
        local first_compiler
        first_compiler=$(echo "$COMPILERS" | awk '{print $1}')
        log_info "Invalid selection — using first detected compiler: $first_compiler"
        COMPILER_EXEC="$first_compiler"
    else
        log_error "No compiler selected and none detected automatically"
        log_info "Use --compiler to specify a path: ./setup.sh --compiler /path/to/g++"
        exit 1
    fi
}

# Ask what the user wants to do after a successful build: run, install, or both.
# Argument: path to the compiled binary.
post_build_prompt() {
    local bin="$1"

    if [ "$YES_MODE" = true ]; then
        log_success "Binary available at: $bin"
        return
    fi

    echo ""
    echo "WHAT WOULD YOU LIKE TO DO?"
    echo ""
    echo "  1. Run Compilatron now"
    echo "  2. Install then run"
    echo "  3. Install only"
    echo "  4. Nothing (exit)"
    echo ""
    read -p "Choose option (1-4): " -r

    local do_install=false
    local do_run=false

    case "$REPLY" in
        1) do_run=true ;;
        2) do_install=true; do_run=true ;;
        3) do_install=true ;;
        *) log_info "Skipping — binary is at: $bin" ; return ;;
    esac

    if [ "$do_install" = true ]; then
        echo ""
        echo "INSTALL LOCATION:"
        echo ""
        echo "  1. Current user only  (~/.local)  — no sudo required  (recommended)"
        echo "  2. System-wide        (/usr/local) — requires sudo"
        echo "  3. Custom path"
        echo ""
        read -p "Choose option (1-3): " -r

        local prefix=""
        case "$REPLY" in
            1) prefix="$HOME/.local" ;;
            2) prefix="/usr/local" ;;
            3)
                echo ""
                read -p "Enter install prefix: " -r prefix
                prefix=$(echo "$prefix" | sed "s/^['\"]//;s/['\"]$//")
                if [ -z "$prefix" ]; then
                    log_error "Empty path — skipping install"
                    do_install=false
                fi
                ;;
            *)
                log_info "Skipping install"
                do_install=false
                ;;
        esac

        if [ "$do_install" = true ] && [ -n "$prefix" ]; then
            log_info "Installing to $prefix..."
            local install_cmd="make $CMAKE_OVERRIDE install CMAKE_INSTALL_PREFIX=$prefix"
            if [ "$prefix" = "/usr/local" ]; then
                install_cmd="sudo $install_cmd"
            fi
            if $install_cmd; then
                log_success "Installed to $prefix"
                log_info "To uninstall: right-click the app icon, or run compilatron-uninstall"
                if [ "$prefix" = "$HOME/.local" ] && [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
                    echo ""
                    log_info "Note: ~/.local/bin is not in your PATH. Add this to ~/.bashrc or ~/.profile:"
                    echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
                fi
                bin="$prefix/bin/compilatron"
            else
                log_error "Install failed"
                do_run=false
            fi
        fi
    fi

    if [ "$do_run" = true ]; then
        log_info "Launching Compilatron..."
        "$bin" &
    fi
}

# Show usage information
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --compiler PATH    Use specific compiler or directory"
    echo "  --cmake PATH       Use specific cmake binary or install root"
    echo "  --no-deps         Skip dependency installation"
    echo "  --no-build        Skip building step"
    echo "  --static          Link libstdc++ statically (recommended with custom compilers)"
    echo "  -y, --yes         Non-interactive mode: answer yes to all prompts and use defaults"
    echo "  --help            Show this help"
    echo ""
    echo "Compiler PATH can be:"
    echo "  - Full path to executable: /home/user/compilers/gcc_15/bin/g++"
    echo "  - Directory path: /home/user/compilers/gcc_15 (finds g++/clang++ automatically)"
    echo "  - Directory with bin: /home/user/compilers/gcc_15/bin"
    echo ""
    echo "CMake PATH can be:"
    echo "  - Full path to cmake binary: /home/user/cmake-4.3.0/bin/cmake"
    echo "  - Install root or bin/ directory: /home/user/cmake-4.3.0"
    echo ""
    echo "Examples:"
    echo "  $0                                              # Interactive setup"
    echo "  $0 --compiler /usr/bin/clang++-15              # Use specific compiler"
    echo "  $0 --compiler /home/user/compilers/gcc_15      # Use directory (finds g++ automatically)"
    echo "  $0 --cmake ~/cmake-4.3.0-linux-x86_64          # Use custom cmake installation"
    echo "  $0 --no-deps --compiler g++-12                 # Skip deps, use g++-12"
}

# Parse command line arguments
parse_arguments() {
    SKIP_DEPS=false
    SKIP_BUILD=false
    STATIC_BUILD=false
    YES_MODE=false
    CUSTOM_COMPILER=""
    CUSTOM_CMAKE=""

    while [[ $# -gt 0 ]]; do
        case $1 in
            --compiler)
                CUSTOM_COMPILER="$2"
                shift 2
                ;;
            --cmake)
                CUSTOM_CMAKE="$2"
                shift 2
                ;;
            --no-deps)
                SKIP_DEPS=true
                shift
                ;;
            --no-build)
                SKIP_BUILD=true
                shift
                ;;
            --static)
                STATIC_BUILD=true
                shift
                ;;
            -y|--yes)
                YES_MODE=true
                shift
                ;;
            --help)
                show_usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

# Main setup function
main() {
    # Parse command line arguments
    parse_arguments "$@"

    # Auto-detect non-interactive environments (piped stdin, CI, etc.)
    if [ ! -t 0 ] && [ "$YES_MODE" = false ]; then
        YES_MODE=true
    fi

    echo "=========================================="
    echo "    Compilatron Setup"
    echo "=========================================="
    echo ""

    if [ "$YES_MODE" = true ]; then
        log_info "Non-interactive mode: using defaults for all prompts"
        echo ""
    fi

    # Detect system
    detect_distro
    log_info "Detected: $DISTRO_NAME"

    # Ensure git submodules are initialised (handles clones without --recurse-submodules)
    if [ ! -f "external/tge-core/CMakeLists.txt" ]; then
        log_info "Initialising git submodules..."
        git submodule update --init --recursive
        log_success "Submodules initialised"
    fi

    # Locate a suitable cmake
    find_cmake "$CUSTOM_CMAKE"
    CMAKE_OVERRIDE="CMAKE=$CMAKE_EXEC"

    # Locate a suitable compiler
    find_compiler "$CUSTOM_COMPILER"
    COMPILER_OVERRIDE="COMPILER=$COMPILER_EXEC"
    warn_if_runtime_mismatch

    # Handle dependencies
    if [ "$SKIP_DEPS" = true ]; then
        log_info "Skipping dependency check (--no-deps specified)"
    else
        check_and_install_dependencies
    fi

    # Ninja is always required for the build regardless of --no-deps
    ensure_ninja

    # Handle build
    if [ "$SKIP_BUILD" = true ]; then
        log_info "Skipping build (--no-build specified)"
    else
        echo ""
        echo "BUILD COMPILATRON:"
        echo "This will compile the Compilatron GUI application in the current directory."
        echo "No system files will be modified - everything stays local to this folder."
        echo ""
        if [ "$STATIC_BUILD" = true ]; then
            echo "Static linking enabled — binary will be self-contained (no libstdc++ dependency)"
        else
            echo "Tip: use --static for a self-contained binary (recommended with custom compilers)"
        fi
        echo ""
        echo "Using compiler: $COMPILER_EXEC"
        local do_build=true
        if [ "$YES_MODE" = false ]; then
            read -p "Build Compilatron now? (Y/n): " -r
            if [[ $REPLY =~ ^[Nn]$ ]]; then
                do_build=false
            fi
        fi
        if [ "$do_build" = true ]; then
            log_info "Building Compilatron..."
            echo ""
            STATIC_OVERRIDE=""
            if [ "$STATIC_BUILD" = true ]; then
                STATIC_OVERRIDE="STATIC=yes"
            fi
            # Wipe the build directory entirely so no artifacts compiled by a
            # different compiler survive. Then --fresh re-configures from scratch,
            # immune to any stale CMakeCache from moved or trashed build trees.
            BUILD_DIR_SETUP="build/cmake-${CONFIG:-Release}"
            rm -rf "$BUILD_DIR_SETUP"
            STATIC_FLAG_SETUP=""
            if [ "$STATIC_BUILD" = true ]; then
                STATIC_FLAG_SETUP="-DSTATIC_LINKING=ON"
            fi
            "$CMAKE_EXEC" --fresh \
                -S . \
                -B "$BUILD_DIR_SETUP" \
                -GNinja \
                -DCMAKE_BUILD_TYPE="${CONFIG:-Release}" \
                -DCMAKE_CXX_COMPILER="$COMPILER_EXEC" \
                $STATIC_FLAG_SETUP \
                || { log_error "CMake configuration failed"; exit 1; }
            if make $CMAKE_OVERRIDE $COMPILER_OVERRIDE $STATIC_OVERRIDE; then
                log_success "Build completed successfully!"
                BUILT_BIN=$(find build/ -maxdepth 2 -name "compilatron" -type f 2>/dev/null | head -1)
                if [ -n "$BUILT_BIN" ]; then
                    post_build_prompt "$BUILT_BIN"
                fi
            else
                log_error "Build failed!"
                exit 1
            fi
        else
            log_info "Skipping build - you can run 'make $CMAKE_OVERRIDE $COMPILER_OVERRIDE $STATIC_OVERRIDE' manually later"
        fi
    fi

    echo ""
    log_success "Setup completed!"
    FINAL_BIN=$(find build/ -maxdepth 2 -name "compilatron" -type f 2>/dev/null | head -1)
    if [ -z "$FINAL_BIN" ]; then
        log_info "To build:      make"
        log_info "To install:    make install CMAKE_INSTALL_PREFIX=~/.local"
        log_info "               sudo make install  (system-wide)"
    fi
}

# Run main function
main "$@"
