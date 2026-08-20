# Desktop build for RSSpaper — simulator, tests and the font packer.
#
# This exists so a fresh clone can build with nothing but a C++17 compiler.
# CMakeLists.txt builds the same targets and is what CI uses; if you have
# cmake, prefer it. The two must stay in sync.
#
#   make sim      -> bin/rsspaper-sim
#   make tests    -> bin/rsspaper-tests  (and `make check` runs them)
#   make fonts    -> build/literata.rfp
#   make edition  -> renders PNGs from the fixture feeds into out/

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Wshadow
# third_party is -isystem: vendored headers must not spray warnings over
# ours, and we are not going to patch stb.
INCLUDES  = -I. -Isrc -isystem third_party -isystem third_party/stb \
            -isystem third_party/doctest
BUILD     = build
BIN       = bin

CORE_SRCS := $(shell find src/core -name '*.cpp' 2>/dev/null | sort)
SIM_SRCS  := $(shell find src/sim src/hal -name '*.cpp' 2>/dev/null | sort)
TEST_SRCS := $(shell find test -name '*.cpp' 2>/dev/null | sort)
FONT_SRCS := $(shell find tools/fontgen -name '*.cpp' 2>/dev/null | sort)
HYPH_SRCS := $(shell find tools/hyphgen -name '*.cpp' 2>/dev/null | sort)

CORE_OBJS := $(CORE_SRCS:%.cpp=$(BUILD)/%.o)
SIM_OBJS  := $(SIM_SRCS:%.cpp=$(BUILD)/%.o)
TEST_OBJS := $(TEST_SRCS:%.cpp=$(BUILD)/%.o)
FONT_OBJS := $(FONT_SRCS:%.cpp=$(BUILD)/%.o)
HYPH_OBJS := $(HYPH_SRCS:%.cpp=$(BUILD)/%.o)

FONT_PACK := $(BUILD)/literata.rfp

.PHONY: all sim tests check fonts hyphgen edition core clean fmt portability
all: sim tests fonts

core: $(CORE_OBJS)

sim: $(BIN)/rsspaper-sim
$(BIN)/rsspaper-sim: $(CORE_OBJS) $(SIM_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

tests: $(BIN)/rsspaper-tests
$(BIN)/rsspaper-tests: $(CORE_OBJS) $(TEST_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

check: $(BIN)/rsspaper-tests portability
	$(BIN)/rsspaper-tests

# The invariant everything else rests on.
portability:
	@./tools/check-portability.sh

fontgen: $(BIN)/fontgen
$(BIN)/fontgen: $(FONT_OBJS) $(CORE_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

# Development-only: the generated table is checked in, so no build needs this.
hyphgen: $(BIN)/hyphgen
$(BIN)/hyphgen: $(HYPH_OBJS) $(CORE_OBJS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

fonts: $(FONT_PACK)
$(FONT_PACK): $(BIN)/fontgen $(wildcard assets/fonts/*.ttf)
	@mkdir -p $(BUILD)
	$(BIN)/fontgen --fonts assets/fonts --out $(FONT_PACK)

edition: sim fonts
	$(BIN)/rsspaper-sim compose --config config/feeds.toml --fonts $(FONT_PACK) --out out --fresh

read: sim fonts
	$(BIN)/rsspaper-sim read --fonts $(FONT_PACK)

# Pattern rule with automatic dependency generation.
$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD) $(BIN) out

.PHONY: device device-portrait
device:
	cd src/device && pio run -e inkplate6flick

device-portrait:
	cd src/device && pio run -e inkplate6flick-portrait

# The device is talked to over its serial console. The port is found
# automatically; set RSSPAPER_PORT if a machine has more than one board.
#
# The console needs pyserial. A plain python3 usually has it; PlatformIO ships
# one that always does, so fall back to that rather than asking anyone to
# install anything.
DEVICE_PY := $(shell for p in python3 \
	"$$HOME/.platformio/penv/bin/python" \
	/opt/homebrew/opt/platformio/libexec/bin/python3 \
	/usr/local/opt/platformio/libexec/bin/python3; do \
	command -v "$$p" >/dev/null 2>&1 && "$$p" -c "import serial" >/dev/null 2>&1 \
	  && echo "$$p" && break; done)

# Both guards explain themselves rather than failing several lines later with
# something about a missing file.
define need_pio
	@command -v pio >/dev/null 2>&1 || { 	  echo "PlatformIO is not installed. It builds and flashes the firmware:"; 	  echo "  brew install platformio    # or: pip install platformio"; 	  exit 1; }
endef

define need_device_py
	@test -n "$(DEVICE_PY)" || { 	  echo "No python with pyserial found, which the serial console needs:"; 	  echo "  pip install pyserial       # or install PlatformIO, which ships one"; 	  exit 1; }
endef

DEVICE := $(DEVICE_PY) tools/device.py

.PHONY: device-flash device-flash-portrait device-log device-ls device-put device-rm device-compose
device-flash:
	$(call need_pio)
	cd src/device && pio run -e inkplate6flick -t upload

# The orientation experiment on the board. Same firmware, panel rotated 90
# degrees in the blit; see src/core/layout/page.h for what it is answering.
device-flash-portrait:
	$(call need_pio)
	cd src/device && pio run -e inkplate6flick-portrait -t upload

device-log:
	$(call need_device_py)
	$(DEVICE) log

device-ls:
	$(call need_device_py)
	$(DEVICE) ls

# make device-put FILE=config/feeds.local.toml DEST=/feeds.toml
#
# DEST, not AS: make defines AS itself as the assembler, so an unset AS
# silently expands to "as" and the file lands on the card called /as.
device-put:
	$(call need_device_py)
	@test -n "$(FILE)" || { echo "usage: make device-put FILE=<path> [DEST=/name]"; exit 2; }
	$(DEVICE) put $(FILE) $(DEST)

# make device-rm PATH_ON_CARD=/read.dat
device-rm:
	$(call need_device_py)
	@test -n "$(PATH_ON_CARD)" || { echo "usage: make device-rm PATH_ON_CARD=/name"; exit 2; }
	$(DEVICE) rm $(PATH_ON_CARD)

# Fetch and compose now, rather than waiting for wake_at.
device-compose:
	$(call need_device_py)
	$(DEVICE) compose

.PHONY: help
help:
	@echo "On a computer:"
	@echo "  make                 simulator, tests and the font pack"
	@echo "  make check           run the tests and the portability gate"
	@echo "  make edition         compose from the fixture feeds into out/"
	@echo "  make read            read that edition from the keyboard"
	@echo ""
	@echo "On the device (needs PlatformIO):"
	@echo "  make device          build the firmware"
	@echo "  make device-flash    build it and put it on the board"
	@echo "  make device-flash-portrait   the same, rotated 90 degrees"
	@echo "  make device-log      watch what it says"
	@echo "  make device-ls       what is on the card"
	@echo "  make device-put FILE=<path> [DEST=/name]"
	@echo "  make device-rm PATH_ON_CARD=/name"
	@echo "  make device-compose  fetch and compose now"
	@echo ""
	@echo "RSSPAPER_PORT picks the board if more than one is attached."
