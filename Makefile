.PHONY: all build debug run test clean distclean install help

all: build

build:
	@cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null || true
	@cmake --build build -j
	@ln -sf μEmacs build/bin/muEmacs 2>/dev/null || true

debug:
	@cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null || true
	@cmake --build build -j
	@ln -sf μEmacs build/bin/muEmacs 2>/dev/null || true

debug-log:
	@cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DUEMACS_DEBUG_LOG=ON >/dev/null || true
	@cmake --build build -j
	@ln -sf μEmacs build/bin/muEmacs 2>/dev/null || true

run: build
	@./build/bin/muEmacs

test: build
	@./build/bin/full_integration_test

install:
	@cmake --install build

clean:
	@cmake --build build --target clean >/dev/null 2>&1 || true

distclean:
	@rm -rf build

help:
	@echo "μEmacs Build Targets:"
	@echo "  make          - Build release version"
	@echo "  make run      - Build and run μEmacs"
	@echo "  make install  - Install to /usr/local (requires sudo)"
	@echo "  make test     - Build and run tests"
	@echo "  make clean    - Clean build artifacts"
	@echo "  make distclean - Remove build directory"
