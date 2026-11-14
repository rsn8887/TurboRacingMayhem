# Turbo Racing Mayhem

A pseudo-3D arcade racer in the style of Outrun and Lotus Esprit Turbo Challenge, written in C++17 with SDL2. It has 11 stages (from a palm-lined coast to a chequerboard road in a sci-fi city, a rainbow road in Rio, Paris, the Autobahn and Florence), 6 car types, 20 cars per race, championship and single-race modes, three difficulty levels for the CPU drivers (Easy, Medium, Hard) and 2-player split screen.

![title screen](screenshot_title.png)

![in game](screenshot_ingame.png)

## Controls

| Action | Player 1 | Player 2 | Controller |
|---|---|---|---|
| Steer | ← → | A D | Left stick / D-pad |
| Accelerate | ↑ | W | A or right trigger |
| Brake | ↓ | S | B or left trigger |
| Nitro (hold) | Space | Left Shift or Tab | X or Y |
| Gear up / down (manual) | X / Z | E / Q | RB / LB |
| Pause | Esc or P | | Start |

**Drift:** tap the brake while steering into a bend at speed (over about 130 km/h) and keep the throttle down. The tail swings out and the car holds a tighter line with little loss of speed. Countersteer or straighten up to end it.

**Nitro:** once the bar (bottom left) is full, hold the nitro button for a burst of power and a higher top speed until the bar is empty or you let go. It can only be fired again when the bar is full; it refills slowly on its own and much faster while you drift. The CPU drivers use nitro and drift too.

**Turbo start:** hit the gas just before the lights turn green (within the last third of a second) and the car leaps off the line with a short nitro push. Hold the gas down any earlier and the wheels spin. The CPU drivers sometimes get a turbo start too, more often on harder difficulties.

F11 or Alt+Enter toggles fullscreen.

## Building

Needs CMake, SDL2 and SDL2_ttf. The build makes the game (`turboracingmayhem`) and the asset tool (`turbo_assetgen`). All builds go into a folder next to the project, so the project folder stays clean: run the commands from the folder that contains the `TurboRacingMayhem` folder.

**macOS**
```sh
brew install sdl2 sdl2_ttf cmake
cmake -S TurboRacingMayhem -B build && cmake --build build -j
open build/TurboRacingMayhem.app
```
To make a `.dmg` for sharing, run `TurboRacingMayhem/make_dmg.sh`. It builds into `build-macos/` and puts the SDL libraries into the app, so it runs on Macs without Homebrew. The result is `build-macos/TurboRacingMayhem-<version>-macOS-arm64.dmg`. The app is signed ad hoc, so the first time it has to be opened with right-click > Open.

**Linux**
```sh
sudo apt install libsdl2-dev libsdl2-ttf-dev cmake g++
cmake -S TurboRacingMayhem -B build && cmake --build build -j
./build/turboracingmayhem
```

**Nintendo Switch**
```sh
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src devkitpro/devkita64 \
  sh -c 'cmake -S ./TurboRacingMayhem -B build-switch -DPLATFORM=SWITCH -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake && cmake --build build-switch -j'
```
This creates `build-switch/turboracingmayhem.nro`.

On the Switch:
- Launch the game through title takeover (hold **R** while starting an installed game). The album applet doesn't have enough memory for it.
- For less memory and CPU load, add `-DTURBO_RENDER_SCALE=1` to the first `cmake` command. The game then renders at 640×360.

**Android** (handhelds like the Ayaneo Pocket Air Mini, and phones or TVs with a controller)
```sh
docker build --platform linux/amd64 -t turboracingmayhem-android TurboRacingMayhem/android
docker run --rm --platform linux/amd64 -v "$PWD":/work turboracingmayhem-android
```
The first command builds the Docker image once, with the Android SDK and NDK, Gradle and the SDL sources (about 5 GB). The second builds `build-android/TurboRacingMayhem-<version>.apk`. Install it with `adb install -r`, or copy it to the device and open it there.
- The build tools only exist for x86-64, so on Apple Silicon Docker emulates them and the first build takes a while. Turning on "Use Rosetta for x86_64/amd64 emulation" in Docker Desktop's settings makes it much faster.
- `build-android/turboracing.keystore` signs the APK. Keep it: with the same key, new versions install over old ones.
- For slower devices, add `-e RENDER_SCALE=1` to `docker run` to render at 640×360.
- The game needs a controller (built in or connected); there are no touch controls. The Back button pauses the race.

## Assets

All graphics are PNG atlases in `assets/` and can be edited with any image editor that keeps the alpha channel. Each picture has a 1-pixel magenta (255, 0, 255) frame around it. Its position is listed in the `.txt` file next to the PNG. Car bodies are grey and get recoloured where the matching `_paint.png` mask is white.

`./build/turbo_assetgen TurboRacingMayhem/assets` regenerates all assets. **Careful: this overwrites your hand edits.**

The app icons are in `icon/`: `icon_switch.jpg` for the Switch, `TurboRacingMayhem.icns` for macOS, `android/` for Android, and `icon.png` as the 1024×1024 original. `./build/turbo_assetgen --icons TurboRacingMayhem/icon` paints them again.

assisted by Claude
