# Third-party software and assets

The RTS Kit's own code is in `src/`, `web/`, `CMakeLists.txt` and `Makefile`. Everything below
was written by others and is used under the licence shown. All of these licences allow
commercial use; most ask that their notice travels with your game (they're all reproduced here).

## raylib (zlib licence)

<https://www.raylib.com/> · raylib 6.0. Window, input, drawing, textures, text and the default
font. Linked into every build: on desktop the installed raylib is used, or CMake downloads and
compiles it; the web build always compiles it from source.

```
Copyright (c) 2013-2026 Ramon Santamaria (@raysan5)

This software is provided "as-is", without any express or implied warranty. In no event
will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial
applications, and to alter it and redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not claim that you
  wrote the original software. If you use this software in a product, an acknowledgment
  in the product documentation would be appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not be misrepresented
  as being the original software.

  3. This notice may not be removed or altered from any source distribution.
```

### Libraries bundled inside raylib

When raylib is compiled from source (web build, or desktop without an installed raylib), these
come with it. Licences as stated in each file's header (in `raylib/src/external/`):

| Library | Used for | Licence |
|---|---|---|
| GLFW | desktop windows and input | zlib (text below) |
| glad | OpenGL loading | MIT (glad_gles2: Apache 2.0) |
| stb_image, stb_image_write, stb_image_resize2, stb_truetype, stb_rect_pack, stb_perlin | images, fonts | public domain / MIT (dual) |
| stb_vorbis | audio | public domain / MIT (dual) |
| miniaudio, dr_wav, dr_mp3, dr_flac | audio | public domain / MIT-0 (dual) |
| jar_xm, jar_mod | audio | public domain / MIT |
| qoi, qoa | images, audio | MIT |
| cgltf, tinyobj_loader_c, par_shapes, vox_loader, m3d | 3D models | MIT |
| sdefl, sinfl | compression | MIT / public domain |
| rprand, rlsw | random numbers, software renderer | MIT |

The kit doesn't use audio or 3D models, but raylib compiles them in by default.

**GLFW**

```
Copyright (c) 2002-2006 Marcus Geelnard
Copyright (c) 2006-2019 Camilla Löwy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
```

## Emscripten (MIT / University of Illinois-NCSA)

<https://emscripten.org/> · Compiles the web build. Its JavaScript runtime and system
libraries end up inside `index.js` and `index.wasm`: musl libc (MIT), compiler-rt (Apache 2.0
with LLVM exceptions) and dlmalloc (public domain). Emscripten offers MIT or NCSA; under MIT:

```
Copyright (c) 2010-2014 Emscripten authors, see AUTHORS file.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

The full licence texts (incl. NCSA, musl, compiler-rt) are in your Emscripten install:
`emscripten/LICENSE`, `system/lib/libc/musl/COPYRIGHT`, `system/lib/compiler-rt/LICENSE.TXT`.

## Art, fonts, maps

- **All art in `assets/sprites/` is placeholder art** made for this kit (simple shapes drawn by a
  small program), not third-party. It's meant to be replaced: see "Art (sprites)" in the README.
  Without it the game draws coloured shapes.
- **Font:** raylib's built-in default font (part of raylib, zlib licence above).
- **Maps** in `maps/` were made for this kit with its own editor.
- No sound or music is included.
