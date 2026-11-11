//! Output helpers: 3MF packaging of the engine's binary STL, PNG snapshots, and handing a
//! file to the system's default app (the slicer for .3mf / .stl).

use std::collections::HashMap;
use std::fmt::Write as _;
use std::path::{Path, PathBuf};

use serde_json::{json, Value};

fn exports_dir() -> PathBuf {
    let dir = crate::support::data_dir().join("exports");
    let _ = std::fs::create_dir_all(&dir);
    dir
}

/// Folder for files that are written only to be opened in another app.
#[tauri::command]
pub fn export_dir() -> String {
    exports_dir().to_string_lossy().into_owned()
}

/// Mesh from a binary STL: welded vertices (exact float match) and triangles.
fn read_stl(path: &Path) -> Result<(Vec<[f32; 3]>, Vec<[u32; 3]>), String> {
    let data = std::fs::read(path).map_err(|e| format!("read {}: {e}", path.display()))?;
    if data.len() < 84 {
        return Err("not a binary STL".into());
    }
    let count = u32::from_le_bytes(data[80..84].try_into().unwrap()) as usize;
    if data.len() < 84 + count * 50 {
        return Err("truncated STL".into());
    }
    let mut index: HashMap<[u32; 3], u32> = HashMap::with_capacity(count / 2);
    let mut verts = Vec::with_capacity(count / 2);
    let mut tris = Vec::with_capacity(count);
    for t in 0..count {
        let rec = &data[84 + t * 50..84 + t * 50 + 50];
        let mut tri = [0u32; 3];
        for (k, slot) in tri.iter_mut().enumerate() {
            let at = 12 + k * 12;
            let v: [f32; 3] = std::array::from_fn(|a| f32::from_le_bytes(rec[at + a * 4..at + a * 4 + 4].try_into().unwrap()));
            let key = v.map(f32::to_bits);
            *slot = *index.entry(key).or_insert_with(|| {
                verts.push(v);
                (verts.len() - 1) as u32
            });
        }
        if tri[0] != tri[1] && tri[1] != tri[2] && tri[0] != tri[2] {
            tris.push(tri);
        }
    }
    Ok((verts, tris))
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut table = [0u32; 256];
    for (i, e) in table.iter_mut().enumerate() {
        let mut c = i as u32;
        for _ in 0..8 {
            c = if c & 1 != 0 { 0xEDB8_8320 ^ (c >> 1) } else { c >> 1 };
        }
        *e = c;
    }
    let mut c = !0u32;
    for &b in bytes {
        c = table[((c ^ b as u32) & 0xFF) as usize] ^ (c >> 8);
    }
    !c
}

/// Minimal ZIP writer (deflated entries): enough for an OPC package such as 3MF.
fn zip_store(entries: &[(&str, Vec<u8>)]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut central = Vec::new();
    for (name, body) in entries {
        let crc = crc32(body);
        // deflate (method 8) as the 3MF spec allows: model XML shrinks several times over
        let packed = miniz_oxide::deflate::compress_to_vec(body, 6);
        let offset = out.len() as u32;
        let local = |buf: &mut Vec<u8>, sig: u32, central: bool| {
            buf.extend_from_slice(&sig.to_le_bytes());
            if central {
                buf.extend_from_slice(&20u16.to_le_bytes()); // version made by
            }
            for v in [20u16, 0, 8, 0, 0x21] {
                buf.extend_from_slice(&v.to_le_bytes()); // version, flags, method=deflate, time, date
            }
            for v in [crc, packed.len() as u32, body.len() as u32] {
                buf.extend_from_slice(&v.to_le_bytes());
            }
            buf.extend_from_slice(&(name.len() as u16).to_le_bytes());
            buf.extend_from_slice(&0u16.to_le_bytes()); // extra
            if central {
                for v in [0u16, 0, 0] {
                    buf.extend_from_slice(&v.to_le_bytes()); // comment, disk, internal attrs
                }
                buf.extend_from_slice(&0u32.to_le_bytes()); // external attrs
                buf.extend_from_slice(&offset.to_le_bytes());
            }
            buf.extend_from_slice(name.as_bytes());
        };
        local(&mut out, 0x0403_4b50, false);
        out.extend_from_slice(&packed);
        local(&mut central, 0x0201_4b50, true);
    }
    let cd_offset = out.len() as u32;
    out.extend_from_slice(&central);
    out.extend_from_slice(&0x0605_4b50u32.to_le_bytes());
    for v in [0u16, 0, entries.len() as u16, entries.len() as u16] {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out.extend_from_slice(&(central.len() as u32).to_le_bytes());
    out.extend_from_slice(&cd_offset.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out
}

fn model_xml(name: &str, verts: &[[f32; 3]], tris: &[[u32; 3]]) -> String {
    let mut s = String::with_capacity(verts.len() * 48 + tris.len() * 40 + 512);
    let safe: String = name.chars().filter(|c| !matches!(c, '<' | '>' | '&' | '"')).collect();
    s.push_str(r#"<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
<metadata name="Application">Datum Core</metadata>
<resources><object id="1" type="model" name=""#);
    s.push_str(&safe);
    s.push_str("\"><mesh><vertices>\n");
    for v in verts {
        // 0.001 mm is far below any printer's resolution
        let _ = writeln!(s, r#"<vertex x="{:.3}" y="{:.3}" z="{:.3}"/>"#, v[0], v[1], v[2]);
    }
    s.push_str("</vertices><triangles>\n");
    for t in tris {
        let _ = writeln!(s, r#"<triangle v1="{}" v2="{}" v3="{}"/>"#, t[0], t[1], t[2]);
    }
    s.push_str("</triangles></mesh></object></resources>\n<build><item objectid=\"1\"/></build>\n</model>\n");
    s
}

/// Package an engine-written STL as a 3MF file (welded mesh, millimetres).
#[tauri::command]
pub async fn stl_to_3mf(stl: String, out: String, name: String) -> Result<Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let (verts, tris) = read_stl(Path::new(&stl))?;
        let types = br#"<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>"#;
        let rels = br#"<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>"#;
        let zip = zip_store(&[
            ("[Content_Types].xml", types.to_vec()),
            ("_rels/.rels", rels.to_vec()),
            ("3D/3dmodel.model", model_xml(&name, &verts, &tris).into_bytes()),
        ]);
        std::fs::write(&out, zip).map_err(|e| format!("write {out}: {e}"))?;
        let _ = std::fs::remove_file(&stl);
        Ok(json!({ "path": out, "triangles": tris.len(), "vertices": verts.len() }))
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Write a PNG snapshot to a path the user chose in the save dialog.
#[tauri::command]
pub fn save_png(path: String, data: Vec<u8>) -> Result<(), String> {
    if !path.to_ascii_lowercase().ends_with(".png") || !data.starts_with(b"\x89PNG") {
        return Err("only PNG images can be saved here".into());
    }
    std::fs::write(&path, data).map_err(|e| e.to_string())
}

/// Open a file we exported (inside the exports folder) with its default application.
#[tauri::command]
pub fn open_export(path: String) -> Result<(), String> {
    let p = std::fs::canonicalize(&path).map_err(|e| e.to_string())?;
    let root = std::fs::canonicalize(exports_dir()).map_err(|e| e.to_string())?;
    if !p.starts_with(&root) {
        return Err("only files in the exports folder can be opened".into());
    }
    std::process::Command::new("explorer").arg(&p).spawn().map(|_| ()).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc_matches_reference() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
    }

    #[test]
    fn three_mf_round_trip_welds_vertices() {
        // two triangles sharing an edge -> 4 vertices
        let mut stl = vec![0u8; 80];
        stl.extend_from_slice(&2u32.to_le_bytes());
        for tri in [[[0f32, 0., 0.], [1., 0., 0.], [0., 1., 0.]], [[1., 0., 0.], [1., 1., 0.], [0., 1., 0.]]] {
            stl.extend_from_slice(&[0u8; 12]);
            for v in tri {
                for c in v {
                    stl.extend_from_slice(&c.to_le_bytes());
                }
            }
            stl.extend_from_slice(&[0u8; 2]);
        }
        let dir = std::env::temp_dir();
        let src = dir.join("dk_test.stl");
        std::fs::write(&src, &stl).unwrap();
        let (v, t) = read_stl(&src).unwrap();
        assert_eq!((v.len(), t.len()), (4, 2));
        let zip = zip_store(&[("3D/3dmodel.model", model_xml("p", &v, &t).into_bytes())]);
        assert_eq!(&zip[..4], b"PK\x03\x04");
        assert!(zip.windows(4).any(|w| w == b"PK\x05\x06"));
    }
}
