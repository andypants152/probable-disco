.DEFAULT_GOAL := _usage

CMAKE ?= $(or $(wildcard /usr/bin/cmake),cmake)
EMCMAKE ?= emcmake

.PHONY: _usage web switch

_usage:
	@echo "Run 'make web' or 'make switch'."

web:
	$(CMAKE) -E rm -rf dist/web
	$(EMCMAKE) $(CMAKE) -S . -B build/web -DCMAKE_BUILD_TYPE=Release -DWEB_DIST_DIR=dist/web
	$(CMAKE) --build build/web
	$(CMAKE) -E echo "Web build output:"
	find dist/web -maxdepth 3 -type f

switch:
	$(CMAKE) -E rm -rf dist/switch
	$(CMAKE) -E make_directory dist/switch
	$(MAKE) -B -f Makefile.switch
	$(CMAKE) -E copy build/switch/probable-disco.nro dist/switch/probable-disco.nro
	$(CMAKE) -E copy build/switch/probable-disco.elf dist/switch/probable-disco.elf
	$(CMAKE) -E echo "Switch build output:"
	find dist/switch -maxdepth 1 -type f
