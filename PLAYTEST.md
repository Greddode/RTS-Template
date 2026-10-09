# Playtest checklist

Run through this before each release. `[x]` = checked for version 1.1.0 (how is in brackets),
`[ ]` = still needs a person at the keyboard.

**How the automatic checks were done:** a throwaway copy of the game (not shipped) fed scripted
mouse and keyboard input into the real game code, clicking the real buttons by their labels, and
checked after every order that it took effect (555 checks). It played a full session under
AddressSanitizer + UndefinedBehaviorSanitizer: all five maps (fog on for three, off for two), every
order, the menus, transports, towers, naval units, the F4 armies and the editor. No memory errors,
no undefined behaviour, no leaks. Feature tests (exact damage, healing, fog tiles, naval rules, ...)
run in `make test` or were run when each feature was added.

## Orders

- [x] Move: right click ground *(scripted, every map)*
- [x] Attack a unit: right click an enemy *(scripted)*
- [x] Attack a building: right click an enemy building *(scripted)*
- [x] Attack-move: A, then right click *(scripted)*
- [x] Stop (S) and hold position (H) *(scripted)*
- [x] Rally point: select a building, right click the ground *(scripted, every building type)*
- [x] Mine: workers + right click gold *(scripted)*
- [x] Build every building with the hotkeys and a click, incl. the Academy, Air Factory, Guard Tower and Dock only after a finished Barracks *(scripted; the Dock on maps with shore near the base)*
- [x] Heal: Medics + right click a damaged unit, HP goes up *(scripted)*
- [x] Train every unit type at its building: the first with the inspector's **button**, the rest with **hotkeys** *(scripted)*
- [x] Airship: L loads nearby idle units, Ctrl + right click flies and unloads, right click your Airship boards it, U unloads below *(scripted, every map)*
- [x] Guard Tower picks a target and shoots *(scripted)*
- [x] Boats and Ships: box select on water, attack an enemy Ship, move, stay on water *(scripted, maps with water)*
- [ ] Train and Build with the inspector's buttons and hotkeys, with a real mouse
- [ ] Box select, shift-select, double-check the selection rings and inspector feel right
- [ ] Click an Airship hovering over your own units (it should be the one selected)

## Game flow

- [x] Main menu → Controls (both tabs) → Back; Play → map picker → Back *(scripted)*
- [x] Pause (Esc), Fog of war button, Controls page, Resume *(scripted)*
- [x] Pause → Main Menu *(scripted)*
- [x] Victory screen, Play Again, Defeat screen, Main Menu *(scripted)*
- [x] All maps: Random, Dire Straight, Harbor, Islands, River Crossing *(scripted)*
- [x] F1 debug wave, F3 overlay, F4 armies (+500 a side) *(scripted, every map)*
- [x] Controls page at 1280×720: every row readable, both tabs scroll to the end *(screenshots; fits at 6 window sizes in `make test`)*
- [ ] A full game against the AI by hand, start to finish (is it fun, is it clear what to do?)
- [ ] Desktop window resize (the UI should scale with the window height)

## Map editor

- [x] Paint all 6 tiles (incl. gravel and lava) with brush sizes 1, 3 and 5; Ctrl+Z undo *(scripted)*
- [x] Place Bases for both teams, a Barracks, a Guard Tower, a Dock on a painted lake; gold; erase an object *(scripted)*
- [x] Unit brush: every unit type, Boats and Ships on water *(scripted; they are in the Test Play game)*
- [x] Save, Load (from the list), Test Play, F2 back to the editor *(scripted)*
- [x] F2 from a game → editor → Exit returns to the paused game *(scripted, every map)*
- [x] New map 32 and 128 *(scripted)*
- [ ] Drag-painting and placing with a real mouse; the green/red ghost looks right

## Builds

- [x] Clean clone of the branch, README steps only: desktop with gcc and with clang, 0 warnings; `make test` passes
- [x] `make release`: both zips made, 0 warnings

## Web build

- [x] Builds from a clean checkout; the release zip runs from an empty folder *(headless Firefox, no JS errors)*
- [x] 100 workers + 300 vs 300, with and without 30 Boats + 10 Ships: 60 FPS *(headless Firefox)*
- [ ] No Emscripten logo, text box or page text; loading bar shows, then goes away *(checked for 1.0.0, not again)*
- [ ] Right click doesn't open the browser menu; F3 doesn't open "find in page" *(checked for 1.0.0, not again)*
- [ ] Window sizes 1280×800, 900×900, 1920×1080: canvas keeps 16:9 *(checked for 1.0.0, not again)*
- [ ] Editor Save downloads the `.map` file
- [ ] Play with a real mouse in Firefox **and** Chrome (and Safari if you can): orders, editor, menus
- [ ] Resize the browser window while playing
- [ ] Upload the zip to itch.io (as a draft) and play it there
