# Playtest checklist

Run through this before each release. `[x]` = checked for version 1.0.0 (how is in brackets),
`[ ]` = still needs a person at the keyboard.

**How the automatic checks were done:** a throwaway copy of the game (not shipped) fed scripted
mouse and keyboard input into the real game code, clicking the real buttons by their labels.
It played a full session under AddressSanitizer + UndefinedBehaviorSanitizer: all maps, every
order, the menus, and the editor. It ran twice, once with fog on and once with fog off, and found
no memory errors, no undefined behaviour and no leaks. Feature tests (exact damage, healing, fog
tiles, ...) were run when each feature was added.

## Orders (fog on and fog off)

- [x] Move: right click ground *(scripted, fog on + off)*
- [x] Attack a unit: right click an enemy *(scripted)*
- [x] Attack a building: right click an enemy building *(scripted)*
- [x] Attack-move: A, then right click *(scripted)*
- [x] Stop (S) and hold position (H) *(scripted)*
- [x] Rally point: select a building, right click the ground *(scripted)*
- [x] Mine: workers + right click gold *(scripted)*
- [x] Build every building through the placement code, incl. the Academy only after a finished Barracks *(scripted + prerequisite tests)*
- [x] Heal: Medics + right click a damaged unit *(scripted + healing tests)*
- [x] Train every unit type at its building *(scripted, queued in code)*
- [ ] Train and Build with the inspector's **buttons** and **hotkeys**, with a real mouse
- [ ] Box select, shift-select, double-check the selection rings and inspector feel right

## Game flow

- [x] Main menu → Play → map picker → Random *(scripted)*
- [x] Pause (Esc), Fog of war button, Controls page, Resume *(scripted)*
- [x] Pause → Main Menu *(scripted)*
- [x] Victory screen and Defeat screen, Play Again, Main Menu *(scripted)*
- [x] All maps: Random, Dire Straight, River Crossing *(scripted)*
- [x] F1 debug wave, F3 overlay on/off *(scripted)*
- [ ] A full game against the AI by hand, start to finish (is it fun, is it clear what to do?)
- [ ] Desktop window resize (the UI should scale with the window height)

## Map editor

- [x] Paint all 4 tiles with brush sizes 1, 3 and 5; Ctrl+Z undo *(scripted)*
- [x] Place Bases for both teams, a Barracks, units, gold; erase an object *(scripted)*
- [x] Save, Load (from the list), Test Play, F2 back to the editor *(scripted)*
- [x] F2 from a game → editor → Exit returns to the paused game *(scripted)*
- [x] New map 32 and 128 *(scripted)*
- [ ] Drag-painting and placing with a real mouse; the green/red ghost looks right

## Web build

- [x] Builds and starts from a clean checkout; the zip runs from an empty folder *(headless Firefox)*
- [x] No Emscripten logo, text box or page text; loading bar shows, then goes away *(headless Firefox)*
- [x] Right click doesn't open the browser menu (`contextmenu` default prevented) *(headless Firefox)*
- [x] F3 doesn't open "find in page" (default prevented) and toggles the overlay *(headless Firefox)*
- [x] Window sizes 1280×800, 900×900, 1920×1080: canvas keeps 16:9, mouse maps to the right game pixel *(headless Firefox)*
- [ ] Editor Save downloads the `.map` file *(worked when the web build was set up, before the new page shell; not re-checked for 1.0.0)*
- [x] 100 workers + 300 vs 300: 59 FPS *(headless Firefox)*
- [ ] Play with a real mouse in Firefox **and** Chrome (and Safari if you can): orders, editor, menus
- [ ] Resize the browser window while playing
- [ ] Upload the zip to itch.io (as a draft) and play it there
