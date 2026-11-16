# Antipode (retro): the 256-colour, CPU-only engine.
#   make host   - the test harness (build/host): PNG frames, timing, a scripted bot
#   make web    - one self-contained page, build/web/index.html (clang + wasm-ld; no Emscripten, no libc)
#   make smoke  - a quick headless run
CC      ?= cc
CFLAGS  ?= -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fno-math-errno
INC      = -Icore
SRC      = core/app.c core/gfx.c core/snd.c core/font_vga.c scenes/afterimage.c scenes/prism.c scenes/tidepool.c

all: host

build/host: $(SRC) platform/host.c core/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -o $@ $(SRC) platform/host.c -lz

host: build/host

# The browser build: freestanding WebAssembly. memcpy/memset become wasm bulk-memory instructions, the math is core.h's own,
# so there is no libc to link and the module is a few tens of kilobytes. The page embeds it, so it also runs from file://.
WASMCC  ?= clang
WASMFLAGS = --target=wasm32 -O2 -std=gnu11 -ffreestanding -fno-builtin-strlen -DAP_FREESTANDING -mbulk-memory -fno-math-errno \
            -Wall -Wextra -Wno-unused-parameter $(INC) -nostdlib \
            -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=4194304 -Wl,--max-memory=4194304 -Wl,-z,stack-size=131072 -Wl,--strip-all

build/web/antipode.wasm: $(SRC) platform/web_main.c platform/libc_shim.c core/*.h
	@mkdir -p build/web
	$(WASMCC) $(WASMFLAGS) -o $@ $(SRC) platform/web_main.c platform/libc_shim.c

build/web/index.html: build/web/antipode.wasm web/index.html web/antipode.js
	python3 web/inline.py web/index.html web/antipode.js build/web/antipode.wasm $@

web: build/web/index.html

# the page the Xbox is sent to: rebuild it and refresh the committed copy
play: build/web/index.html
	@mkdir -p play
	cp build/web/index.html play/index.html

smoke: build/host
	@mkdir -p build/smoke
	./build/host shot 1,2 build/smoke/menu 3
	HOST_INPUT="0.5:+SPACE;0.6:-SPACE;1:+RIGHT;1.6:-RIGHT;1.6:+DOWN;2.2:-DOWN;2.2:+LEFT;3:-LEFT" ./build/host shot 2,4,6 build/smoke/play 3
	./build/host bot 60
	HOST_SCENE=1 ./build/host bot 60
	./build/host bench 20

clean:
	rm -rf build
.PHONY: all host web play smoke clean
