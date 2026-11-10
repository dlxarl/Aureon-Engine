# Aureon Engine

**A real-time 3D engine and desktop editor built with C++, OpenGL, GLFW and Dear ImGui.**

Aureon Engine is a C++17 graphics project focused on real-time rendering, scene management, object transformations, lighting, shadows and collision-based physics, with retro rendering inspired by the PlayStation 1 and PlayStation 2 as its direction.

The repository holds three things:

- **`engine/`**: the rendering engine, as a static library with no UI code.
- **`editor/`**: a desktop editor with a project launcher, dockable panels and an integrated viewport. See [The Editor](#the-editor).
- **`demo/`**: the original `DemoPhysics` and `DemoScene` sample scenes, as a standalone executable.

## Features

### Rendering
- Real-time rendering with **OpenGL 3.3 Core Profile**.
- GLSL vertex and fragment shaders for object rendering and lighting.
- Perspective camera projection and view transformations.
- Textured and colored 3D geometry.
- Directional-light depth rendering and shadow mapping.
- HDR cubemap-based skybox support.
- Framebuffer-based post-processing pipeline.
- Motion blur post-processing shader.
- FPS counter and optional frame-rate limiting.
- Retro rendering: low internal resolution, per-vertex lighting, affine texture mapping, vertex snapping, texture filter control, color quantization and ordered dithering.
- Sunlight: a directional light whose shadows follow the camera across a large world, in steps of whole shadow-map texels so that their edges do not crawl.
- A day and night cycle that moves the sun and moon and changes the sky, ambient light and fog.
- Cut-out transparency for plants, glowing surfaces for windows, lanterns and fire, and swelling water that mirrors the sky and catches the sun.
- Fires, lanterns and lit windows that light what is near them: the eight nearest to the camera, per vertex in the retro modes as the PlayStation did it, with a slight flicker.
- Under the sun: light from the sky above and the ground below, sunlight that wraps a little round surfaces, and haze that glows towards the sun.
- An optional gradient sky: the fog color at the horizon deepening overhead, drifting clouds, the sun or the moon, and stars at night.
- Trees and plants that bend in the wind.
- Image grading: saturation and a vignette, before color quantization.
- Frustum and fog-distance culling, and cached uniform locations, for scenes with thousands of objects.

### Geometry and scene management
- Reusable shape base class and scene abstraction.
- Data-driven scenes with stable object ids, a parent/child hierarchy and JSON serialization.
- OBJ model loading, with a binary cache.
- Built-in primitives:
  - Cube
  - Plane
  - UV sphere
  - Cylinder
- Object position, rotation, scale, color, and texture controls.
- Separate scene update, render, and depth-rendering passes.
- Demo scenes for rendering and physics experiments.

### Physics and interaction
- Basic gravity and velocity integration.
- Movable objects that fall, land flush on terrain and on each other, slide to rest, and can be pushed or knocked about.
- Colliders that can be turned about the vertical axis, so that buildings and walls can stand at any angle, sorted into a grid so that a world of thousands of them stays fast.
- A character controller with acceleration, sliding along walls and round their corners, step climbing, a slope limit, sprinting, fall damage, swimming and being knocked back.
- People and animals that walk round walls, steep ground and deep water, keep out of each other's way and step aside for the player; birds and dragons that fly.
- Game rules driven by scene data, from a course of pickups to an open world with quests, dialogue, trade, levels, combat and wildlife.
- Collision checks between objects.
- Static and dynamic scene objects.
- First-person player movement.
- Walking, running, crouching, and jumping.
- Runtime object selection and transformations.
- Keyboard toggles for lighting, shadows, and post-processing.

> The physics implementation is a lightweight custom demonstration, not a full rigid-body physics engine. Collision handling and dynamics are intentionally limited.

## Demo controls

The demo starts in `DemoPhysics`. Pass `--basic` to run `DemoScene` instead.

| Key | Action |
| --- | --- |
| `W` `A` `S` `D` | Move the player |
| `Left Shift` | Run |
| `C` | Crouch |
| `Space` | Jump; rise when swimming |
| `Ctrl` or `C` | Dive, in deep water |
| `Mouse` | Look around |
| `Mouse Wheel` | Adjust camera field of view |
| `N` | Select the next scene object |
| `Arrow keys` | Move the selected object horizontally |
| `+` / `-` | Move the selected object vertically |
| `R` | Rotate the selected object |
| `I` | Increase the selected object's scale |
| `O` | Decrease the selected object's scale |
| `L` | Toggle lighting |
| `K` | Toggle shadows |
| `M` | Toggle post-processing |
| `T` | Reload the active scene |
| `1` | Limit frame rate to 30 FPS |
| `2` | Limit frame rate to 70 FPS |
| `0` | Disable the frame-rate limit |
| `Esc` | Close the application |

The camera uses a captured mouse cursor. Use the window close button or `Esc` to exit.

## Tech stack

| Technology | Purpose |
| --- | --- |
| C++17 | Core application and engine implementation |
| OpenGL 3.3 | Graphics rendering API |
| GLSL | GPU shaders |
| GLFW 3.3.8 | Window creation, OpenGL context, and input |
| GLAD | OpenGL function loading |
| GLM 1.0.1 | Vector and matrix mathematics |
| stb_image | Image and texture loading |
| nlohmann/json | Project, settings and scene files |
| Dear ImGui (docking) | Editor interface |
| ImGuizmo | Scene view transform gizmos |
| CMake 3.20+ | Build configuration |

Dependencies are declared through CMake FetchContent, in `cmake/Dependencies.cmake`. GLAD source and headers are included in the repository. The Editor's fonts come from the Dear ImGui source tree.

## Repository structure

```text
3Dengine/
├── cmake/                   # Dependencies and resource copying
├── engine/
│   ├── include/             # Public headers
│   ├── src/
│   └── shaders/             # Built-in shaders, copied next to each executable
├── editor/
│   ├── resources/shaders/   # Grid, outline and picking shaders
│   └── src/
│       ├── app/             # Windows, the launcher, theme, widgets
│       ├── core/            # Document, Play mode, console, dialogs, shared context
│       ├── panels/          # Hierarchy, Inspector, Project, Console, Render Settings, viewport
│       ├── platform/        # Native file dialogs, Finder, Trash
│       ├── project/         # Project format, recent projects, asset import
│       └── viewport/        # Editor camera, overlays, picking
├── demo/
│   ├── assets/              # Sample textures and skyboxes
│   ├── scenes/
│   └── main.cpp
└── tests/
```

## Requirements

- CMake **3.20 or newer**
- A C++17-compatible compiler
- Git
- An OpenGL 3.3-compatible graphics driver
- Internet access during the first CMake configure step, to fetch dependencies

On macOS, the Xcode Command Line Tools are enough. The Editor and the demo run on Apple Silicon.

## Build

```bash
git clone https://github.com/dlxarl/3Dengine.git
cd 3Dengine
cmake -S . -B build
cmake --build build -j
```

Set `-DENGINE_BUILD_EDITOR=OFF` or `-DENGINE_BUILD_DEMO=OFF` to leave a target out.

## Run

Open the Editor:

```bash
open build/editor/Editor.app
```

Or start it from a terminal, which also accepts a project to open directly:

```bash
./build/editor/Editor.app/Contents/MacOS/Editor --project ~/path/to/MyGame
```

Run the demo, from any directory:

```bash
./build/demo/3D_Engine
```

## The Editor

### Project launcher

The launcher opens first. It lists recent projects with their details, creates new projects from a template, and opens existing ones. Projects live wherever you choose, independent of the engine, and the Editor refuses locations inside its own bundle or source tree.

The list and the last used location are kept per user, in `~/Library/Application Support/Aureon`.

### Project layout

```text
MyGame/
├── Assets/                  # Your files: Models, Textures, Materials, Scenes, Audio, Scripts
├── ProjectSettings/
│   ├── Project.json         # The startup scene
│   └── Graphics.json        # Render settings
├── Packages/
├── Library/                 # Generated data: mesh cache, thumbnails, panel layout
├── .gitignore               # Excludes Library
└── Project.json             # Name, id, engine version
```

Scenes refer to assets by project-relative path, so a project can be moved or renamed on disk. `Library` can be deleted at any time; it is rebuilt as needed.

### Panels

| Panel | What it does |
| --- | --- |
| Scene and Game | The engine's renderer, in a resizable viewport. Scene adds a grid, selection outline, gizmos and object markers. Game looks through the first camera in the scene. |
| Hierarchy | Selects, creates, renames, duplicates and deletes objects. Drag a row onto another to nest it, or between rows to reorder. |
| Inspector | Edits the live object: name, transform, color, texture, shadows, physics, and light or camera properties. |
| Project | Browses `Assets`, imports files and creates folders. Drag a texture onto an object or a slot, a model into the Scene view, or a scene to open it. |
| Console | Engine and Editor messages, filtered by level and text. |
| Render Settings | The renderer's settings, saved in the project, and the scene's background and skybox. |

Every control is connected: `tests/render_tests.cpp` checks that each render setting changes the rendered image.

| Render setting | Effect |
| --- | --- |
| Rendering Mode | Modern shades per pixel. PS1 lights per vertex and maps textures affinely. PS2 lights per vertex with perspective-correct textures. Selecting a mode also loads a matching set of the settings below. |
| Internal Resolution | Lines rendered before upscaling, with nearest-neighbor pixels. Width follows the viewport's shape. |
| Texture Filtering | Nearest, bilinear or trilinear, for all material textures. Nearest, mipmapped keeps square texels up close and uses smaller copies of the texture further off, so that distant ground does not sparkle. |
| Color Quantization | Bits of color for the final image. |
| Dithering | A 4x4 Bayer pattern in internal-resolution pixels. Needs quantization. |
| Vertex Precision | Snaps vertices to a screen grid, for the wobble of early 3D. |
| Saturation, Vignette | Strength of color, and darkening towards the corners. |

Lighting, shadows and motion blur are also available there.

#### Post-processing

Render Settings also holds a cinematic stack, in collapsible sections. Every effect has its own switch and costs nothing while off: its passes are not run and its buffers are not allocated. The scene is rendered into an HDR buffer when tone mapping or bloom is on, and with a normal buffer when SSAO is on; the passes then ping-pong between two buffers: SSAO, depth of field, motion blur, grade (bloom, tone mapping, color), FXAA, and a final pass for the lens, vignette, grain and the retro screen. The settings are saved in `ProjectSettings/Graphics.json` under `post`.

| Section | Effects |
| --- | --- |
| Tone Mapping & Color Grading | Tone mapping (None, Reinhard, Neutral, ACES), exposure, contrast, color filter, saturation, white balance, split toning. |
| Camera & Optics | Bloom (threshold, soft knee, intensity, scatter, over a 5-level dual-filter pyramid), depth of field (focal distance, range, max blur, near and far), vignette (intensity, smoothness, roundness), motion blur strength. |
| Lens Artifacts | Chromatic aberration, lens distortion (barrel or pincushion), film grain. |
| Screen Space Lighting | SSAO with a 16 or 32 sample hemisphere kernel, a 4x4 noise tile and a depth-aware blur. |
| Anti-Aliasing | FXAA 3.11. |
| Retro / Stylized | Color quantization, dithering, scanlines, CRT curvature, phosphor glow. |

Colors are graded as the engine renders them, without a gamma conversion, so exposure and tone mapping act on display values.

### Effects: water, fire and particles

Water, fire and particle streams are objects of their own (Hierarchy, **Create > Effects**), not textures on geometry. They are placed, named, parented, saved with the scene and edited in the Inspector, and they run in the Scene view as well as in Play.

| Object | What it is |
| --- | --- |
| Particles | A stream of soft camera-facing discs: rate, lifetime, speed, spread, gravity, drag, turbulence, size and a three-color fade over life, additive (glowing) or alpha blended. Simulated on the CPU and drawn as one instanced call per stream, against the scene's depth. |
| Water mesh | A mesh with the Water game role (a river) is drawn with the same waves, current and sky reflection as a Water object, translucent, and its waves depend on world position so that pieces meet without a seam. |
| Audio Source | A WAV clip placed in the world (Create > Audio Source). Settings: clip (drag a sound from the Project panel), volume, pitch and random pitch variation, loop, 3D sound with near and far distance (the viewport draws both as rings while it is selected), fade in, start delay, repeat interval for a sound that does not loop, and mute. It starts with the scene in Play mode and in a built game; the Inspector's Preview button plays it in the editor. Clips under Assets are found by their asset path. |
| 2D elements | Create > 2D adds Panel, Text, Image, Bar and Button elements, or a ready-made HUD or Menu. They are laid out in pixels inside their parent element (anchor, size, offset, or fill the parent), scale with the window, and are drawn over the Game view in hierarchy order. A Bar can follow Health or Stamina by itself; Buttons expose `hovered` and `pressed` to scripts. |
| Player | A new Player has no game of its own: it stays where it is until a script moves it. The Player section's "Game controls" switch gives it the engine's game (walking, camera, combat, HUD), which is what scenes from before the option still use. |
| Scripts | A script is a C++ class in Assets (`AUREON_SCRIPT(Name)`, see `Script.h`) with `onStart` and `onUpdate`. Create one from a template (PlayerControl, Spinner, EmptyScript) with right-click > New script in the Project panel, then choose it under an object's Script. Play compiles the project's scripts with `clang++` and loads them; a scene whose scripts do not compile does not play. |
| Fonts | Text and Button elements each have their own Font (any .ttf or .otf from the project); Inter, bundled, when none is chosen. |
| Fullscreen preview | Play > Play fullscreen (Shift+Cmd+F) or the Fullscreen button: the game fills the screen with no panels. Esc releases the mouse, then leaves. The menu bar is repeated in the macOS system menu. |
| Lights | Create > Light adds a Point, Spot or Directional light, as many as you like. Each has color, temperature in Kelvin, intensity, specular, range, cone angle and softness (spot), cast shadows, source size (soft shadows), and flicker (a candle, a faulty neon sign). The sun is the first directional light; the nearest 15 others are used. Surfaces are shaded with Cook-Torrance (GGX, Smith, Schlick) instead of Phong, with a hemisphere ambient. |
| Ray traced shadows | Render Settings > Rendering: point and spot lights with Cast shadows get shadows from rays traced in the fragment shader, through a bounding volume hierarchy of the scene's Static geometry held in two float textures (soft with Source size, 1 to 8 rays per pixel). "Ray traced sun" adds the sun's shadow beyond the shadow map's range. Moving objects are not in the hierarchy; they cast shadows only through the sun's shadow map. On the Dreadhollow town the traced sun roughly triples the frame time, so it is off there. |
| MCP server | `tools/mcp/server.py` gives an AI assistant tools to read and edit scenes and settings, build, run tests, and render a scene to an image through `aureon_shot`, without opening the Editor. See `tools/mcp/README.md`. |
| Tools | The Tools menu opens the Code Editor, Audio Editor, Terrain Editor and Model Editor inside the Editor. **Code Editor**: tabs, line numbers, find and replace, Cmd+S saves the file; "IDE settings" sends code files to VS Code, CLion, Xcode, Sublime, Nova or any command with `{file}` and `{line}`. **Audio Editor**: a WAV as a waveform; select, play, trim, cut, silence, fades, gain, normalize, reverse, speed, to mono, undo, save or save as. **Terrain Editor**: a Terrain object (Create > Terrain) with Raise, Lower, Smooth, Flatten and Paint brushes in the Scene view, painting with the tiles of an atlas texture, and generated hills; it is walkable ground in Play. **Model Editor**: a model drawn by the engine's own renderer, edited by selecting faces or vertices (click, Shift adds), move/rotate/scale with a gizmo, extrude, subdivide, flip, delete, merge by distance, box UVs, smooth shading, primitives to add (cube, plane, cylinder, cone, sphere), a preview texture, undo, and saving `.obj` (right-click a model in the Project panel > Edit in Model Editor). |
| Fire | Flames, smoke and sparks built from a few numbers (size, intensity, smoke, sparks), and a flickering light that falls on what is near. It is one of the lights the lighting shader uses, nearest first. In a game with combat, standing in the flames costs health (Damage / s, 8 by default, 0 makes it harmless); swimming puts it out. |
| Water | A surface of a given size (a rectangle, or the ellipse inside it) at the object's height, with rolling waves, drifting ripples, a current, sky reflection, sun glints and a different look from below. Its outline is also what the player swims and dives in. |

Water is as wide as `size` times the object's scale; it is not rotated. Meshes with the Water role still work as before, for shapes that a rectangle or an ellipse cannot follow, such as a river.

**Diving.** In water deeper than the player, `Ctrl` or `C` dives and keeps going down, `W` swims where the camera looks, `Space` rises, and reaching the surface ends the dive. The third-person camera goes under with the diver. Under the surface there are 20 seconds of air, shown as a bar; without it the player is hurt each second until they come up. While the camera is under water, one extra pass shows it: red is absorbed first, distance turns to the water's color, the picture wobbles, and light shimmers from above. It runs only then.

### Play mode

Play takes a snapshot of the scene, runs the engine's physics and game rules on it and switches to the Game view. Stop restores the snapshot, so nothing done during play reaches the saved scene. Objects with Gravity fall and collide with what has Collision.

### Sound

Play mode has sound when the project has an `Assets/Audio` folder; without it a game is silent. Files are WAV (8, 16 or 24 bit PCM, or 32 bit float; mono or stereo) and are found by name, so a game only needs the ones it wants:

| Folder | Files | Heard |
| --- | --- | --- |
| `player/` | `grass`, `stone`, `wood`, `sand`, `snow` `-step1` and `-step2`; `pick-sword-inventory` | Footsteps for the surface underfoot (snow high up, sand along the water, wood in houses and on bridges and docks, stone in chapels, towers and the cobbled heart of a town, grass elsewhere); picking things up and changing weapon. |
| `weapon/` | `sword1`, `sword2`, `metal-hit1`, `metal-hit2`, `arrow` | Swings, blows that land, arrows, being hurt. |
| `animals/`, `horse/` | `attacking-wolf` (to `3`), `crow` (to `3`), `deer`, `deer-snort`, `sheep`, `pigeons`; `horse-snort`, `horse-snort2`, `horse-galloping` | Creatures attacking, hurt, dying or taking fright, in the world where they are; crows, sheep and horses now and then; hooves while riding. |
| `outdoor/` | `river`, `lake`, `fire` | Only what is really there: no background noise in the countryside. |
| `town/` | `town-ambience`, `crowd`, `ambience-music`, `church-bell` | Town noise near houses, stalls and wells; music only from a character named Minstrel, Bard or Musician, while they live; the bell tolls at 6, 12 and 18 from the nearest Chapel. |
| `sfx/` | `string-and-stomp`, `string-and-stomp2` | Being defeated, and gaining a level. |

Places carry their own sound by name: objects named `River...` or `Lake...`, or ending in `Fire`, play theirs from where they stand, getting louder as the player nears and panned to the side they are on. `GameAudio` turns what `GameSession` reports into sound, and `AudioEngine` mixes it: it needs an output device, which it opens through CoreAudio on macOS and does not yet on other platforms, where a game is silent.

### Head tracking

The Game view can follow your head. **Window > Head Tracking** opens a panel with the source (**Off**, **Mock** or **AirPods**), a **Recenter** button, live diagnostics and the view settings. While you play in the Game view, the head's turn is added to the camera: it looks around in place, in first and third person, and what you hear turns with it. The player still walks and aims where the mouse and keys say, so a glance sideways does not turn the character.

| Diagnostic | Meaning |
| --- | --- |
| Status | `Stopped`, `Unavailable` (this system cannot do it), `Waiting` (no device, or none sending), `Tracking`, `Error` |
| Update rate, Samples | Measured from the sensor's own timestamps |
| Quaternion | The latest raw orientation, in engine axes |
| View offset | Yaw, pitch and roll of what is added to the camera, after recentering and settings |
| Message | Why it is waiting or failed: no AirPods, access denied, system too old |

Sensitivity, smoothing (seconds the view takes to catch up) and which of yaw, pitch and roll are followed are in the same panel. A device that starts sending, or comes back, is recentered by itself. **Mock** is a head that looks around by itself, with buttons to simulate a disconnect and a failure, so everything can be tried without AirPods. `aureon_headtrack` prints the mock's output in a terminal.

**AirPods requirements**

- macOS 14 (Sonoma) or later, built with an SDK that has `CMHeadphoneMotionManager`.
- Headphones with motion sensors: AirPods (3rd generation), AirPods Pro (1st and 2nd generation) or AirPods Max, connected to this Mac. Other headphones are not supported.
- Allow **Motion & Fitness** for Aureon Editor when macOS asks, or later in System Settings > Privacy & Security > Motion & Fitness. The Editor's Info.plist carries the required `NSMotionUsageDescription`.
- Open the Editor as an app (`open build/editor/Editor.app`). macOS judges motion access against the program that started a process, so an Editor started from the middle of another app's terminal can be ended by the system. This is also why `aureon_headtrack` only has the mock.
- AirPods report orientation only, at a rate the panel shows (not measured here, as no AirPods were available). There is no position, so the camera turns but does not lean or move.

Head tracking is made of three parts, so another sensor needs no change to the camera:

- `HeadTracker` (`HeadTracking.h`) is the interface of a provider: start, stop, and diagnostics holding the latest orientation. It has no platform types.
- `HeadTrackingController` turns a provider's raw orientation into the rotation a camera adds: recentered, scaled, masked and smoothed. `GameInput::headRotation` carries it into `GameSession`, which applies it to the game camera.
- Providers: `MockHeadTracker`, and AirPods in `CoreMotionHeadTracker.mm`, the only file that knows Core Motion. Elsewhere `createHeadTracker(HeadTrackerKind::AirPods)` gives a provider that reports `Unavailable`. A webcam or headset provider is a subclass of `HeadTrackerBase` that calls `publishSample()`, handed to `HeadTrackingController::setTracker()`.

### Making a game

Any scene with a **Player** object is playable, with no code. Create one from the **Create** menu, and give other objects a role in the Inspector's **Gameplay** section:

| Role | In play |
| --- | --- |
| Pickup | Collected by touching it, and spins until then. |
| Goal | Wins the game once every pickup is collected. |
| Hazard | Sends the player back to the start. Patrol makes it swing back and forth. |

Without terrain, the player also goes back to the start after falling 14 units below it, keeping what they collected. The Game view shows a counter and a timer, and the first camera in the scene follows the player.

| Key, in the Game view | Action |
| --- | --- |
| `W` `A` `S` `D` or arrows | Move, relative to the camera |
| `Space` | Jump |
| `Q` `E`, or right mouse button | Turn the camera |
| Mouse | Look around. Click the view to capture the mouse, `Esc` to free it |
| `V` | Switch between first and third person |
| `Shift` | Run, while stamina lasts |
| Click, `1` `2` | Attack; sword or bow |
| `F` | Talk, take, open, read, sleep, ride or get down |
| `1` to `9` | Choose a reply in a conversation |
| `H` | Drink the best potion carried |
| `Tab`, `J` | The bag, the journal |
| `R` | Restart |

Tick **First person** on the camera in the Inspector to start a game in that view.

Larger worlds come from more roles:

| Role | In play |
| --- | --- |
| Ground | A mesh to walk on, such as a terrain or a bridge deck. Several pieces make one ground; its edges are the world's edges. |
| Water | The surface of deep water, where the player swims. A river can be made of pieces at different heights. |
| Villager | Wanders near where it was placed, goes home at night and runs from danger. One named in the game data can be talked to. |
| Bandit | Chases and strikes the player on sight. Without quests, defeating them all wins the game. |
| Wolf | Hunts in packs and sees further at night. |
| Boar, Bear | Charge whoever comes too close. A bear is slow to notice and hits much harder. |
| Deer, Rabbit, Livestock | Deer and rabbits bolt when the player comes near; livestock only runs when hurt. |
| Bird | Pecks about, and takes to the air when disturbed, circling before it settles again. |
| Dragon | Sleeps where it is placed until the player comes near. Then it flies: it circles, breathes fire and swoops to bite, and once badly hurt it lands to fight on the ground. Far from the player, it flies home and heals. |
| Fireball | What dragons breathe. Add a few, hidden until a dragon needs them; with a light range, they light the ground as they fly. |
| Horse | Ridden with `F`, at about twice the speed. |
| Player body | The player's visible model. The Player object is then only its collider. |
| Sword, Bow, Arrow | Weapons in the player's hands, which turn on health and combat, and the arrows the bow fires. |
| Item | Taken with `F`. Its parameter names the item: `moonleaf` or `moonleaf:2`. |
| Chest | Opened once with `F`. Its parameter lists the contents: `potion:2, gold:25`. |
| Bed | Sleeping passes the night and restores health, unless enemies are near. |
| Sign | Read with `F`; its parameter is the text. |
| Limb | Swings with its parent's walk, by its Swing and Phase: forward and back like a leg, side to side like a tail, or up and down like a wing. |
| Night glow | Lit from dusk to dawn, like a window. |

Meshes do not block anyone by themselves: place invisible cubes as colliders, turned to match a building if it stands at an angle. A cube that is not Static, with Gravity and Collision, is a movable prop. Fog, ambient light and the gradient sky are set per scene, in Render Settings. In the Inspector, Sway bends an object in the wind, and Light range makes an emissive object light what is near it.

#### Game data

Quests, items, conversations and time live in the scene file, in a `game` section next to the objects:

```json
"game": {
  "world": { "dayMinutes": 16, "startHour": 8.5, "startGold": 15,
             "startItems": [{ "item": "arrow", "count": 15 }] },
  "items": [{ "id": "wolf_pelt", "name": "Wolf Pelt", "kind": "pelt", "value": 9 }],
  "characters": [{ "name": "Shepherd Tom", "lines": ["Mind the sheep."] }],
  "quests": [{ "id": "wolves", "title": "Wolves at the Fold", "giver": "Shepherd Tom",
               "objective": "collect", "target": "wolf_pelt", "count": 3,
               "offer": "Bring me three pelts.", "progress": "Three pelts.", "complete": "My flock can sleep tonight.",
               "rewardGold": 45, "rewardXp": 80 }]
}
```

- **Characters** are matched to Villager objects by name. They keep to their place unless `"stays": false`, can sell (`"sells"`) and buy pelts, meat and valuables (`"buys": true`).
- **Quests** are offered by their `giver` and handed in to `turnIn`, which defaults to the giver. `after` makes one wait for another. Objectives are `talk` (to a character), `reach` (an object by name), `collect` (items, handed over at the end) and `kill` (a role such as `wolf`, or the start of a name such as `Raider`).
- **Items** have a `kind`. `potion` heals with `H`, `weapon` raises sword damage, and `pelt`, `meat` and `valuable` can be sold. With an `arrow` item defined, the bow uses arrows from the bag.
- Defeated wolves leave a `wolf_pelt`, deer `venison`, boars a `boar_tusk`, bears a `bear_pelt`, rabbits a `rabbit_pelt` and dragons `dragon_scale`, when the game defines those items. Experience raises the player's level, which brings more health and harder blows.

Losing all health returns the player to the start, with a tenth of their gold lost.

### Keyboard and mouse

| Input | Action |
| --- | --- |
| Hold right mouse button | Look around; add `W` `A` `S` `D` `Q` `E` to fly, and `Shift` to go faster |
| `Option` + left mouse button | Orbit |
| Middle mouse button, or `Option` + `Shift` + left mouse button | Pan |
| Scroll | Zoom |
| `W` `E` `R` | Move, rotate and scale tools |
| `F` | Focus the selection |
| `Cmd` + `S`, `Shift` + `Cmd` + `S` | Save, save as |
| `Cmd` + `P`, `Shift` + `Cmd` + `P` | Play or stop, pause |
| `Cmd` + `D`, `Delete`, `F2` | Duplicate, delete, rename |

Docked panels keep their arrangement per project. **Window > Reset layout** restores the default.

## Architecture overview

Dependencies point one way: `editor` uses `engine`, and `engine` knows nothing about the Editor or Dear ImGui.

- **Renderer** runs a frame: a shadow pass, the scene pass at the internal resolution, then a post-process pass into the output. The demo and the Editor share it.
- **Scene** is the interface the renderer draws. **SceneGraph** is its data-driven implementation, and the demo scenes are hand-written ones.
- **SceneObject** gives everything in a scene an id, a name, a transform and a place in the hierarchy. **Shape** adds geometry, and **LightObject** and **CameraObject** are objects too.
- **GameSession** applies the game rules to a scene from objects' roles, taking input as plain data, so it depends on neither the Editor nor a keyboard. It brings together **CharacterController** (the player's movement), **GameActors** (people, animals and arrows), **GameProgress** (inventory, quests, conversations and experience, with no knowledge of the scene) and **GameClock** (time of day, sky and sun).
- **Physics** moves props, pushing them out of what they run into by the shortest way; the demo keeps its original response. **ColliderGrid** sorts the solids of a world by place, for the controller, the actors, the props, arrows and the camera.
- **SceneSerializer** reads and writes scene files. **AssetManager** loads assets by project-relative path.
- **Engine** is the runtime loop for standalone executables. The Editor has its own, in **EditorApp**.
- **EditorSession** draws one frame of the whole UI and leaves the event loop to its caller, which is what lets the tests run the real UI with scripted input.
- **EditorContext** holds what panels share, and the operations they have in common, so that a menu item and a shortcut do the same thing.

Built-in resources are found next to the executable by **Resources**, never relative to the working directory.

## Tests

```bash
ctest --test-dir build
```

| Suite | Covers | Needs a display |
| --- | --- | --- |
| `project_tests` | Project creation and validation, relocation, recent projects, asset paths, import, the OBJ loader | No |
| `head_tracking_tests` | The mock provider and its states, update rate, recentering, smoothing and axis settings, loss and return of a device, the Core Motion axis conversion | No |
| `render_tests` | Each render setting on the real pipeline, picking, hierarchy, serialization, physics and props, the character controller, game rules, quests, trade, interactions, wildlife, dragons and birds, turned colliders, terrain in pieces, rivers, day and night, cut-outs, glow, point lights, water, following shadows, culling, Play mode, the head's turn on the game camera | Yes |
| `ui_tests` | The real UI in a hidden window with scripted mouse and keyboard: selection, drag and drop, renaming, shortcuts, gizmos, asset drops, Play, game controls, head tracking with the mock, the unsaved changes dialog | Yes |

Suites that need a display report as skipped when there is none.

## Status and limitations

### Not implemented

- **Undo and redo.** Deleting an object cannot be reversed, except by reopening the saved scene.
- **Scripts.** Games are limited to the built-in roles. The `Scripts` and `Audio` folders exist, but the engine does not run scripts or play sound.
- **Standalone games.** A game runs in the Editor's Game view; there is no export to an executable.
- **Saving progress.** Stopping Play mode ends the run; there is no save game.
- **Editing quests in the Editor.** Game data is edited in the scene file; the Editor keeps it when it saves, but has no panel for it.
- **Materials.** Objects have a color and one albedo texture. Importing an OBJ also copies its `.mtl`, but the engine does not read it.
- **Model formats other than OBJ.**
- **More than one light.** The renderer uses the first light in the scene, and the Editor says so instead of offering a second.
- **Multiple selection, and renaming assets in the Project panel.**
- **Floating panels outside the main window.** Dear ImGui's multi-viewport mode is off.

### Limits worth knowing

- Colliders are boxes, turned only about the vertical axis; a box tilted further is held by the upright box around it. Props do not tumble or rotate.
- People and animals turn aside when something is in their way, but do not plan a path: a long wall with one gap can keep them on the wrong side.
- Only the eight point lights nearest the camera shine, and they cast no shadows.
- The third-person camera stops short of colliders, but not of tree crowns, which have no collider.
- Nothing watches the disk continuously: the Project panel rescans every two seconds while it has focus.
- The Scene view renders with the project's render settings, so retro settings apply there too. The grid and gizmos stay at full resolution.

### Verification

Built and run on an Apple Silicon Mac with macOS 26. The demo's output was compared with the previous version of the engine, patched only to open a window on macOS, and frame 300 matched on every pixel.

- **Head tracking with real AirPods is not tested.** No AirPods were available: the Core Motion provider compiles and starts, but its samples and the assumed axes (x right, y forward, z up) have not been checked against hardware. The mock and the conversion are covered by tests. To check: connect AirPods, choose AirPods in the panel, and see that turning, nodding and tilting move yaw, pitch and roll in View offset.
- **Tested only through the test suites:** dropping files on the window was exercised from the point where the Editor receives them, not with a real drag from Finder.
- **Partly tested:** the Import button opens the native file dialog, but choosing files in it was not exercised. The import behind it is covered by `project_tests`. The same applies to **Browse** and **Open project** in the launcher.
- **Not built:** Windows and Linux. The code keeps them in mind, with typed paths in place of native dialogs, but neither has been compiled.

## Contributing

Contributions and improvements are welcome. For larger changes, open an issue to discuss the design first. Please keep pull requests focused and include build or runtime verification steps when possible.

## License

No license file is currently present in this repository. Until a license is added, the source code remains subject to the default copyright rules; do not assume permission for unrestricted reuse or redistribution.
