# Datum Core

A Windows desktop CAD app built on a GPU implicit-modeling kernel. Geometry is an exact field program, so booleans never fail, and analyses run directly on the field.

| Part | Path | What it is |
|---|---|---|
| Desktop shell | `src-tauri/` (Rust, Tauri 2) | Window, `.msi` bundle, `.datum` file association, kernel sidecar lifecycle (Job Object), strict JSON-RPC contract, AES-256-GCM device-locked files (DPAPI-wrapped keys) |
| Workspace UI | `src/` (Next.js static export) | Full-bleed WebGPU ray-marched viewport, floating glass panels, feature tree, property editor, feature graph, analysis / quote / robotics tabs, Ctrl+K command palette |
| Kernel | `kernel/` (C++20) | Field DAG, interval pruning, dual contouring, GPU brick bytecode, mass properties, analyses, robotics; built as `dk_engine.exe` |
| Legacy engine | `backend/` (Python) | v1 SolidWorks-COM analysis engine; not used by the current app |

## Build

The repo has two parts: `kernel/` (C++ geometry engine) and `frontend/` (Tauri desktop app). Run the commands below from `frontend/`; paths to the kernel use `../kernel/`.

Prerequisites: Node 20+, Rust (MSVC) with Visual Studio Build Tools (C++ workload), and the kernel toolchain (`python -m pip install ziglang cmake ninja`).

```bash
../kernel/build.cmd                                              # builds dk_engine.exe and the tests
cp ../kernel/build/dk_engine.exe src-tauri/bin/dk_engine-x86_64-pc-windows-msvc.exe
# STEP converter sidecar (clean venv keeps it ~38 MB)
python -m venv .pyinstaller/convenv
.pyinstaller/convenv/Scripts/pip install cascadio numpy pyinstaller
.pyinstaller/convenv/Scripts/pyinstaller --onefile --distpath .pyinstaller/dist converter/step_convert.py
cp .pyinstaller/dist/step_convert.exe src-tauri/bin/step_convert-x86_64-pc-windows-msvc.exe
npm install
npx tauri build --bundles msi
```

Output: `src-tauri/target/release/bundle/msi/Datum Core_1.0.0_x64_en-US.msi`.

## Tests

```bash
kernel/build/dk_tests.exe                                     # kernel: geometry, meshing, robotics
python ../kernel/tests/engine_smoke.py ../kernel/build/dk_engine.exe   # engine protocol
npm run test:urdf                                                # URDF import against the real kernel
npm run test:params                                              # parameter expressions
npm run test:calc                                                # engineering calculators
cd src-tauri && cargo test --release                             # .datum encryption
```

## Modelling and output (v1.1)

- **Parts**: involute spur gears, ISO metric threaded rods (M3–M20), hole wizard (simple / counterbore / countersink), surface textures (knurl, bumps, ribs), loft between two sketches.
- **Parameters**: named values in the Part panel; type `=width / 2` in any number field to link it.
- **Manufacturability view**: wall thickness (green > 1.5 mm, amber, red < 0.5 mm) and > 45° overhangs; the status bar shows the wall under the cursor.
- **Output**: STL, 3MF, *Send to slicer* (Ctrl+P opens the 3MF in your default slicer), PNG screenshots, 3D-print time/filament estimate.
- **Workflow**: view cube, animated camera, zoom to cursor, double-click a face to look at it, History timeline, starter templates (File → New from template).

## Disciplines and workspaces (v1.2)

- **Workspaces**: switch between Mechanical, Robotics, Automotive and Aerospace (title bar); the ribbon shows that discipline's tools.
- **Simple / Pro**: Simple mode shows a short set of tools with one-line explanations; Pro shows everything.
- **New parts**: NACA wing (sweep, taper, dihedral), nose cones (ogive, conical, elliptical, parabolic), coil springs, ISO ball bearings (608, 6000–6205), structural beams (IPE 80–200, channel, angle, T, tube), drilled brake discs, spoked wheels.
- **Engineering calculators** (Tools, Ctrl+Shift+C): bolt torque, gear pair, beam bending, coil spring, speed and braking, wing sizing.
- **Templates** (File → New from template, Ctrl+Shift+N): 12 starters across the four disciplines.
- **Performance presets** (Settings → Viewport): Auto adapts resolution to hold the frame rate; Performance suits low-end PCs (fewer pixels, no shadows, no glass blur or animation).

## Personalise and interact (v1.3)

- **Themes** (Settings → Appearance): Midnight Teal, Graphite, Deep Ocean, Aurora, Ember, Forest, Rose Gold, Neon, plus a custom accent colour. The 3D viewport's background and lights follow the theme.
- **Sound packs** (Settings → Sound): Studio Guitar, Soft, Mechanical, Sci-Fi, Chiptune, Marimba, with volume. All synthesised, no audio files.
- **Click to select**: click a face in the viewport to select the feature that made it (a hole wall selects the hole).
- **Robot jogging**: move the tool ±X/Y/Z by a step, or click a point on the part to send it there (damped least-squares inverse kinematics within joint limits).

## Smooth on any PC (v1.4)

- While the camera moves, the part is drawn from its watertight triangle mesh (a few operations per pixel) instead of ray marching the exact field, so orbiting holds 60 fps at full resolution on every template, including the swept wing (was 2–10 fps).
- When the camera stops, Auto shows the exact ray-traced image; if this PC is slow at that, it keeps the mesh for stills too.
- The Performance preset (Settings → Viewport) uses the mesh all the time, without shadows, glass blur or animation: suited to integrated graphics.
- Section, X-ray, normals and manufacturability views always use the exact field.

## Live liquid glass (v1.5)

Panels floating over the 3D view (title bar, ribbon, tree, inspector, dock, toolbar, status bar) are drawn by the viewport as liquid glass: they refract, frost and tint the live scene behind them as it moves. The refraction model is adapted from [liquid-glass-js](https://github.com/dashersw/liquid-glass-js) (MIT, see `THIRD_PARTY_NOTICES.md`) and runs as one GPU pass, so it costs well under a millisecond. Toggle it in Settings → Appearance; it is off on the Performance preset.

## Sketch and import

- **Sketch → Extrude / Revolve**: edit the profile in the property panel (drag points, `+` to insert, per-point fillet radius). The profile becomes an exact signed-distance field.
- **File → Import STEP / STL / OBJ (Ctrl+I)**: STEP is tessellated by OpenCASCADE, then every mesh is baked into a signed-distance grid (Resolution, default 160 cells). Imported parts are accurate to the grid (flywheel test: −0.07 % volume vs. SolidWorks) and reference the cached mesh in `%LOCALAPPDATA%\DatumCore\imports`, so re-import on another machine.

Autosave runs every few seconds while there are unsaved changes; logs live in `%LOCALAPPDATA%\DatumCore` (Help → Open log folder).

## What the numbers are

Mass properties and inertia are exact for the meshed field. The drop test is an energy-balance screen, not FEA. Quotes are geometry-based estimates until a manufacturing-partner API is configured. Robot reachability uses positional dexterity (the inverse condition number of the translational Jacobian) and a capsule self-collision model.

## License

MIT — see [LICENSE](LICENSE). The bundled nlohmann/json (kernel/third_party) is MIT; the STEP converter uses OpenCASCADE via cascadio (LGPL-2.1).
