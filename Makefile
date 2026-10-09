# Shortcuts around CMake. The real build config lives in CMakeLists.txt.

desktop:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build

web:
	emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
	cmake --build build-web

run: desktop
	./build/game

# Browsers refuse to load .wasm from file://, so serve it over local HTTP.
serve: web
	@echo "Open http://localhost:8080/index.html"
	python3 -m http.server 8080 -d build-web

# The web build as one zip, index.html at the top level: upload it to itch.io as-is.
# (Python's zip module, so no extra tool is needed; it stores each file under its own name.)
WEB_FILES = build-web/index.html build-web/index.js build-web/index.wasm build-web/index.data
web-zip: web
	rm -f build-web/rts-kit-web.zip
	python3 -m zipfile -c build-web/rts-kit-web.zip $(WEB_FILES)
	@echo "Made build-web/rts-kit-web.zip"

# Release: clean builds of both versions, then the web zip and a source zip in dist/.
VERSION := $(shell sed -n 's/^\#define GAME_VERSION "\(.*\)"/\1/p' src/game/config.h)
RELEASE  = rts-kit-$(VERSION)
SOURCE_FILES = README.md LICENSE THIRD_PARTY.md PLAYTEST.md Makefile CMakeLists.txt .gitignore src web assets maps tests
release:
	rm -rf build build-web dist tests/build
	$(MAKE) desktop
	$(MAKE) web-zip
	mkdir -p dist/$(RELEASE)
	cp build-web/rts-kit-web.zip dist/$(RELEASE)-web.zip
	cp -r $(SOURCE_FILES) dist/$(RELEASE)/
	cd dist && python3 -m zipfile -c $(RELEASE)-source.zip $(RELEASE)
	rm -rf dist/$(RELEASE)
	@echo "Release $(VERSION):"; ls -l dist

# Automated tests (tests/): build them, then run each; fails if any test fails.
# They open a hidden window, so they need a display (on a server: xvfb-run make test).
test:
	cmake -S tests -B tests/build -DCMAKE_BUILD_TYPE=Release
	cmake --build tests/build
	./tests/build/controls_overflow_test
	./tests/build/controls_overflow_test_longnames
	./tests/build/tile_class_test
	./tests/build/naval_test tests/build
	./tests/build/ai_mix_test

clean:
	rm -rf build build-web dist tests/build

.PHONY: desktop web run serve web-zip release test clean
