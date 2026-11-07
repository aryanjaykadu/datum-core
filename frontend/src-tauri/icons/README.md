# Datum Core Enterprise — icon assets

Drop the production `icon.ico` (and `icon.png`/`icon.icns`) here before
running `tauri build`. Required sizes for NSIS/MSI:

- `32x32.png`
- `128x128.png`
- `256x256.png`
- `icon.icns`
- `icon.ico` (multi-size Windows favicon)

The Next.js favicon in `frontend/public/` is already updated from the
uploaded brand image; export the same source at the sizes above into
this folder for the installer.
