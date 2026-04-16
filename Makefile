# Compilatron Makefile — thin CMake wrapper
# All compilation is delegated to CMake/Ninja; this file just provides
# familiar make targets and preset/config shorthand.

# === PROJECT CONFIGURATION ===
PROJECT_NAME := Compilatron
.DEFAULT_GOAL := all

# === CMAKE EXECUTABLE ===
CMAKE ?= cmake

# === USER-FACING VARIABLES ===
PRESET               ?=
CONFIG               ?= Release
STATIC               ?= no
JOBS                 ?= $(shell nproc 2>/dev/null || echo 4)
COMPILER             ?=
CMAKE_INSTALL_PREFIX ?=

# === DISTRIBUTION DETECTION (for install-deps only) ===
DISTRO         := $(shell . /etc/os-release 2>/dev/null && echo "$$ID" || echo "unknown")
DISTRO_VERSION := $(shell . /etc/os-release 2>/dev/null && echo "$$VERSION_ID" || echo "unknown")

ifneq ($(PRESET),)

# ============================================================
# PRESET MODE  —  cmake --preset <name>
# ============================================================

# Derive BUILD_DIR from the preset's binaryDir field
BUILD_DIR := $(shell python3 -c "import json,os; src=os.path.abspath('.'); presets=sum([json.load(open(f,encoding='utf-8')).get('configurePresets',[]) for f in ['CMakePresets.json','CMakeUserPresets.json'] if os.path.exists(f)],[]); m=next((p for p in presets if p.get('name')=='$(PRESET)'),{}); d=m.get('binaryDir',''); print(d.replace(chr(36)+'{sourceDir}',src))" 2>/dev/null)

.PHONY: all
all:
	@$(CMAKE) --preset $(PRESET)
	@$(CMAKE) --build --preset $(PRESET) -- -j$(JOBS)
	@echo ""
	@echo "Build complete: $(BUILD_DIR)/$(PROJECT_NAME)"

.PHONY: configure
configure:
	@$(CMAKE) --preset $(PRESET)

.PHONY: reconfigure
reconfigure:
	@$(CMAKE) --preset $(PRESET) --fresh

.PHONY: clean
clean:
	@echo "Cleaning build directory $(BUILD_DIR)..."
	@rm -rf $(BUILD_DIR)

.PHONY: run
run: all
	@echo "Running $(PROJECT_NAME)..."
	@$(BUILD_DIR)/$(PROJECT_NAME)

.PHONY: install
install: all
	@$(CMAKE) --install $(BUILD_DIR) $(if $(CMAKE_INSTALL_PREFIX),--prefix "$(CMAKE_INSTALL_PREFIX)")

.PHONY: uninstall
uninstall:
	@$(CMAKE) --build $(BUILD_DIR) --target uninstall

else

# ============================================================
# MANUAL MODE  —  cmake -S . -B build/cmake-$(CONFIG)
# ============================================================

BUILD_DIR := build/cmake-$(CONFIG)

CMAKE_FLAGS := -GNinja -DCMAKE_BUILD_TYPE=$(CONFIG)

ifneq ($(COMPILER),)
	CMAKE_FLAGS += -DCMAKE_CXX_COMPILER=$(COMPILER)
endif

ifeq ($(STATIC),yes)
	CMAKE_FLAGS += -DSTATIC_LINKING=ON
endif

CMAKE_LISTS := CMakeLists.txt code/CMakeLists.txt external/imgui/CMakeLists.txt external/glfw/CMakeLists.txt

$(BUILD_DIR)/CMakeCache.txt: $(CMAKE_LISTS)
	@$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)

.PHONY: all
all: configure
	@$(CMAKE) --build $(BUILD_DIR) -j$(JOBS)
	@echo ""
	@echo "Build complete: $(BUILD_DIR)/$(PROJECT_NAME)"

.PHONY: configure
configure: $(BUILD_DIR)/CMakeCache.txt

.PHONY: reconfigure
reconfigure:
	@$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)

.PHONY: clean
clean:
	@echo "Cleaning build directory $(BUILD_DIR)..."
	@rm -rf $(BUILD_DIR)

.PHONY: run
run: all
	@echo "Running $(PROJECT_NAME)..."
	@$(BUILD_DIR)/$(PROJECT_NAME)

.PHONY: install
install: all
	@$(CMAKE) --install $(BUILD_DIR) $(if $(CMAKE_INSTALL_PREFIX),--prefix "$(CMAKE_INSTALL_PREFIX)")

.PHONY: uninstall
uninstall:
	@$(CMAKE) --build $(BUILD_DIR) --target uninstall

.PHONY: static
static:
	@$(MAKE) --no-print-directory STATIC=yes all

.PHONY: test-compiler
test-compiler:
	@echo "CMake version:"
	@$(CMAKE) --version | head -1
	@echo ""
	@echo "Probing compiler selection..."
	@$(CMAKE) -S . -B /tmp/compilatron-compiler-test $(CMAKE_FLAGS) 2>&1 | grep -E "(Compiler|C\+\+|cxx)" || true
	@rm -rf /tmp/compilatron-compiler-test
	@echo "C++23 check:"
	@if [ -n "$(COMPILER)" ]; then \
		echo 'int main(){}' | $(COMPILER) -std=c++23 -x c++ - -o /dev/null 2>&1 && echo "$(COMPILER): C++23 OK" || echo "$(COMPILER): C++23 FAILED"; \
	else \
		echo 'int main(){}' | c++ -std=c++23 -x c++ - -o /dev/null 2>&1 && echo "c++: C++23 OK" || echo "c++: C++23 FAILED"; \
	fi

endif

# ============================================================
# SHARED TARGETS (both modes)
# ============================================================

.PHONY: clean-all
clean-all:
	@echo "Cleaning all build directories..."
	@rm -rf build/

.PHONY: install-deps
install-deps:
	@echo "Installing dependencies for $(DISTRO)..."
	@case "$(DISTRO)" in \
		ubuntu|debian) \
			echo "Detected Debian/Ubuntu - installing via apt"; \
			sudo apt update && sudo apt install -y \
				ninja-build pkg-config \
				libgl1-mesa-dev \
				libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev ;; \
		fedora|centos|rhel|rocky|almalinux) \
			echo "Detected Red Hat family - installing via dnf/yum"; \
			if command -v dnf >/dev/null 2>&1; then \
				sudo dnf install -y ninja-build \
					mesa-libGL-devel \
					libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel; \
			else \
				sudo yum install -y ninja-build \
					mesa-libGL-devel \
					libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel; \
			fi ;; \
		opensuse*) \
			echo "Detected openSUSE - installing via zypper"; \
			sudo zypper install -y ninja pkg-config \
				Mesa-libGL-devel \
				libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel ;; \
		arch|manjaro) \
			echo "Detected Arch Linux - installing via pacman"; \
			sudo pacman -S --needed ninja \
				mesa libx11 libxrandr libxinerama libxcursor libxi ;; \
		*) \
			echo "Unknown distribution: $(DISTRO)"; \
			echo "Please install manually:"; \
			echo "- C++ compiler (g++ or clang++)"; \
			echo "- CMake, Ninja, Git, curl"; \
			echo "- OpenGL development files (libGL)"; \
			echo "- X11 development files"; \
			exit 1 ;; \
	esac
	@echo "Dependencies installed successfully!"

.PHONY: help
help:
	@echo "Compilatron Build System (CMake wrapper)"
	@echo ""
	@echo "Preset mode (primary):"
	@echo "  make PRESET=ctrn-linux-clang-debug          Debug build with Clang"
	@echo "  make PRESET=ctrn-linux-clang-release        Release build with Clang"
	@echo "  make PRESET=ctrn-linux-gcc-debug            Debug build with GCC"
	@echo "  make PRESET=ctrn-linux-gcc-release          Release build with GCC"
	@echo "  (set CTRN_CLANG_PATH / CTRN_GCC_PATH in env or CMakeUserPresets.json for custom compilers)"
	@echo "  make reconfigure PRESET=<name>              Fresh configure (--fresh)"
	@echo "  make clean PRESET=<name>                    Remove preset build dir"
	@echo ""
	@echo "Manual mode (fallback):"
	@echo "  make CONFIG=Debug                Debug build (system compiler)"
	@echo "  make CONFIG=Release              Release build (default)"
	@echo "  make COMPILER=/path/to/clang++   Override compiler"
	@echo "  make STATIC=yes                  Static C++ runtime"
	@echo ""
	@echo "Shared:"
	@echo "  make JOBS=16                     Override parallel jobs (default: $(JOBS))"
	@echo "  make clean-all                   Remove all build directories"
	@echo "  make install-deps                Install system dependencies"
	@echo "  make install CMAKE_INSTALL_PREFIX=~/.local  Install to custom prefix"
	@echo "  make uninstall                   Remove installed files (reads install_manifest.txt)"
	@echo "  make help                        Show this help"
	@echo ""
	@echo "Detected distro: $(DISTRO) $(DISTRO_VERSION)"
	@echo "CMake:           $(CMAKE)"
