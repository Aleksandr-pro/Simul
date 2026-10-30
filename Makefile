# РАЗЛОМ — локальная сборка (Linux/macOS). Windows/Android собираются через CMake.
CXX      ?= g++
BUILD    ?= build
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -pthread
CXXFLAGS += -Iinclude -Ithird_party/sokol -Ithird_party/stb -Ithird_party/fonts -MMD -MP
LDFLAGS  ?= -pthread

CORE_SRC   := $(wildcard src/core/*.cpp) $(wildcard src/net/*.cpp)
CLIENT_SRC := $(wildcard src/client/*.cpp)
CORE_OBJ   := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CORE_SRC))
CLIENT_OBJ := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CLIENT_SRC))

.PHONY: all clean test bench check-client server

all: $(BUILD)/razlom $(BUILD)/razlom_server $(BUILD)/razlom_tests $(BUILD)/razlom_bench

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

$(BUILD)/razlom_tests: $(CORE_OBJ) $(BUILD)/tests/test_core.o $(BUILD)/tests/test_net.o
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD)/razlom_bench: $(CORE_OBJ) $(BUILD)/tests/bench.o
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD)/razlom_server: $(CORE_OBJ) $(BUILD)/server/main.o
	$(CXX) $^ -o $@ $(LDFLAGS)

# Клиент требует sokol + системные библиотеки (X11/GL/ALSA). На headless-машинах
# доступна проверка синтаксиса: make check-client
$(BUILD)/razlom: $(CORE_OBJ) $(CLIENT_OBJ) $(BUILD)/ui/sokol_impl.o $(BUILD)/client/main.o
	$(CXX) $^ -o $@ $(LDFLAGS) -lGL -lX11 -lXi -lXcursor -ldl -lasound -lm

$(BUILD)/ui/sokol_impl.o: src/ui/sokol_impl.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Ithird_party/sokol -c $< -o $@

$(BUILD)/client/main.o: src/client/main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

check-client:
	@for f in $(CLIENT_SRC) src/client/main.cpp; do \
		echo "syntax: $$f"; \
		$(CXX) $(CXXFLAGS) -fsyntax-only $$f || exit 1; \
	done
	@echo "клиентские исходники компилируются"

test: $(BUILD)/razlom_tests
	./$(BUILD)/razlom_tests

bench: $(BUILD)/razlom_bench
	./$(BUILD)/razlom_bench

server: $(BUILD)/razlom_server

clean:
	rm -rf $(BUILD)
