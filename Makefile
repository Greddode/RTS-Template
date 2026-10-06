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
	@echo "Open http://localhost:8080/game.html"
	python -m http.server 8080 -d build-web

clean:
	rm -rf build build-web

.PHONY: desktop web run serve clean
