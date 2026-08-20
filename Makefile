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

.PHONY: device
device:
	cd src/device && pio run
