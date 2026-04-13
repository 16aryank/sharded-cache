GTEST_PREFIX := $(shell brew --prefix googletest)
GTEST_INC    := -I$(GTEST_PREFIX)/include
GTEST_LIBDIR := -L$(GTEST_PREFIX)/lib
GTEST_LIBS   := -lgtest -lgtest_main -pthread

CXX ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=address -fsanitize=undefined -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG -O2 -g
INCLUDES := -Iinclude $(GTEST_INC)

LDFLAGS  := $(GTEST_LIBDIR)
LDLIBS   := $(GTEST_LIBS)

BUILD_DIR ?= build

LIB_SRC := $(wildcard src/*.cpp)
LIB_OBJ := $(patsubst src/%.cpp,$(BUILD_DIR)/src/%.o,$(LIB_SRC))
LIB_AR := $(BUILD_DIR)/libconcurrent_cache.a

TEST_SRC := $(wildcard tests/*.cpp)
TEST_BIN := $(patsubst tests/%.cpp,$(BUILD_DIR)/tests/%,$(TEST_SRC))

LIB_LINK :=
ifneq ($(strip $(LIB_OBJ)),)
LIB_LINK := $(LIB_AR)
endif

.PHONY: all lib tests clean single_threaded multi_threaded

all: lib tests

ifeq ($(strip $(LIB_SRC)),)
lib:
	@echo "No sources found in src/; skipping library build."
else
lib: $(LIB_AR)
endif

$(LIB_AR): $(LIB_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(BUILD_DIR)/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

ifeq ($(strip $(TEST_SRC)),)
tests:
	@echo "No sources found in tests/; skipping tests build."
else
tests: $(TEST_BIN)
endif

$(BUILD_DIR)/tests/%: tests/%.cpp $(LIB_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LIB_LINK) $(LDFLAGS) $(LDLIBS)

single_threaded: $(BUILD_DIR)/tests/single_threaded
	$(BUILD_DIR)/tests/single_threaded

multi_threaded: $(BUILD_DIR)/tests/multi_threaded
	$(BUILD_DIR)/tests/multi_threaded

clean:
	rm -rf $(BUILD_DIR)
