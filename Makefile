CC       ?= gcc
CXX      ?= g++
CFLAGS   := -std=c11 -O3 -Wall -Wextra
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -Wpedantic
INCLUDES := -Iengine/include -Ithird_party/sokol -Ithird_party/stb -Ithird_party/fonts -Igame/src

CORE_SRCS := \
	engine/src/core/audience_pool.cpp \
	engine/src/core/recommender.cpp \
	engine/src/core/minigames.cpp \
	engine/src/core/narrative.cpp \
	engine/src/core/binary_save.cpp \
	engine/src/core/engine.cpp \
	engine/src/cloud/world_sync.cpp \
	engine/src/audio/synth.cpp

UI_CXX_SRCS := \
	engine/src/ui/utf8_atlas.cpp \
	engine/src/ui/renderer_2d.cpp \
	engine/src/ui/widgets.cpp \
	game/src/studio_scene.cpp \
	game/src/viral_app.cpp

.PHONY: all test bench clean

all: test bench

build/sokol_headless.o: engine/src/ui/sokol_impl.c
	@mkdir -p build
	$(CC) $(CFLAGS) -DSIMUL_HEADLESS_GFX=1 $(INCLUDES) -c engine/src/ui/sokol_impl.c -o build/sokol_headless.o

build/simul_tests: build/sokol_headless.o $(CORE_SRCS) $(UI_CXX_SRCS) tests/test_engine.cpp
	$(CXX) $(CXXFLAGS) -DSIMUL_HEADLESS_GFX=1 $(INCLUDES) build/sokol_headless.o $(CORE_SRCS) $(UI_CXX_SRCS) tests/test_engine.cpp -o build/simul_tests -lm -lpthread

build/simul_bench: $(CORE_SRCS) tests/bench_100k_agents.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(CORE_SRCS) tests/bench_100k_agents.cpp -o build/simul_bench -lm -lpthread

test: build/simul_tests
	./build/simul_tests

bench: build/simul_bench
	./build/simul_bench

clean:
	rm -rf build career_slot1.simsave career_autosave.simsave
