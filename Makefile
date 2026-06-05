.DEFAULT_GOAL := _usage

CMAKE_BIN := $(or $(wildcard /usr/local/bin/cmake),$(wildcard /usr/bin/cmake),$(wildcard /snap/bin/cmake),cmake)
EMCMAKE_BIN ?= $(or $(shell command -v emcmake 2>/dev/null),$(wildcard $(HOME)/emsdk/upstream/emscripten/emcmake),emcmake)

.PHONY: _usage web switch

_usage:
	@echo "Run 'make web' or 'make switch'."

web:
	$(CMAKE_BIN) -E rm -rf dist/web
	$(EMCMAKE_BIN) $(CMAKE_BIN) -S . -B build/web -DCMAKE_BUILD_TYPE=Release -DWEB_DIST_DIR=dist/web
	$(CMAKE_BIN) --build build/web
	$(CMAKE_BIN) -E echo "Web build output:"
	find dist/web -maxdepth 3 -type f

switch:
	$(CMAKE_BIN) -E rm -rf dist/switch
	$(CMAKE_BIN) -E make_directory dist/switch
	$(MAKE) -B -f Makefile.switch
	$(CMAKE_BIN) -E copy build/switch/probable-disco.nro dist/switch/probable-disco.nro
	$(CMAKE_BIN) -E copy build/switch/probable-disco.elf dist/switch/probable-disco.elf
	$(CMAKE_BIN) -E echo "Switch build output:"
	find dist/switch -maxdepth 1 -type f
