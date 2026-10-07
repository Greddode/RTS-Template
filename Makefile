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
	python -m http.server 8080 -d build-web

# The web build as one zip, index.html at the top level: upload it to itch.io as-is.
# (Python's zip module, so no extra tool is needed; it stores each file under its own name.)
WEB_FILES = build-web/index.html build-web/index.js build-web/index.wasm build-web/index.data
web-zip: web
	rm -f build-web/rts-kit-web.zip
	python -m zipfile -c build-web/rts-kit-web.zip $(WEB_FILES)
	@echo "Made build-web/rts-kit-web.zip"

clean:
	rm -rf build build-web

.PHONY: desktop web run serve web-zip clean
