# Datum Core

A Windows desktop CAD app built on a GPU implicit-modeling kernel. Geometry is an exact field program, so booleans never fail, and analyses run directly on the field.

| Part | Path | What it is |
|---|---|---|
| Desktop shell | `src-tauri/` (Rust, Tauri 2) | Window, `.msi` bundle, `.datum` file association, kernel sidecar lifecycle (Job Object), strict JSON-RPC contract, AES-256-GCM device-locked files (DPAPI-wrapped keys) |
| Workspace UI | `src/` (Next.js static export) | Full-bleed WebGPU ray-marched viewport, floating glass panels, feature tree, property editor, feature graph, analysis / quote / robotics tabs, Ctrl+K command palette |
| Kernel | `../kernel/` (C++20, separate repo) | Field DAG, interval pruning, dual contouring, GPU brick bytecode, mass properties, analyses, robotics; built as `dk_engine.exe` |
| Legacy engine | `backend/` (Python) | v1 SolidWorks-COM analysis engine; not used by the current app |

## Build

Prerequisites: Node 20+, Rust (MSVC) with Visual Studio Build Tools (C++ workload), and the kernel toolchain (`python -m pip install ziglang cmake ninja`).

```bash
../kernel/build.cmd                                              # builds dk_engine.exe and the tests
cp ../kernel/build/dk_engine.exe src-tauri/bin/dk_engine-x86_64-pc-windows-msvc.exe
npm install
npx tauri build --bundles msi
```

Output: `src-tauri/target/release/bundle/msi/Datum Core_1.0.0_x64_en-US.msi`.

## Tests

```bash
../kernel/build/dk_tests.exe                                     # kernel: geometry, meshing, robotics
python ../kernel/tests/engine_smoke.py ../kernel/build/dk_engine.exe   # engine protocol
npm run test:urdf                                                # URDF import against the real kernel
cd src-tauri && cargo test --release                             # .datum encryption
```

## What the numbers are

Mass properties and inertia are exact for the meshed field. The drop test is an energy-balance screen, not FEA. Quotes are geometry-based estimates until a manufacturing-partner API is configured. Robot reachability uses positional dexterity (the inverse condition number of the translational Jacobian) and a capsule self-collision model.
