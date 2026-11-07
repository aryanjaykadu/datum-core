//! `.datum` files.
//!
//! Plain:      UTF-8 JSON (the feature document), so designs diff and merge in git.
//! Protected:  b"DATUMENC" | u8 version=1 | u32 LE wrapped-key length | wrapped key |
//!             12-byte nonce | AES-256-GCM ciphertext+tag of the JSON.
//!
//! The 256-bit file key is random per save and wrapped with Windows DPAPI for the current
//! user on this machine (CryptProtectData). Copied to another machine or account, the key
//! cannot be unwrapped and the file stays unreadable ciphertext. Nothing "self-destructs":
//! a copied file is just bytes; the protection is that it cannot be decrypted elsewhere.
//! AES-GCM also authenticates the content, so any tampering makes decryption fail.

use aes_gcm::aead::{Aead, KeyInit};
use aes_gcm::{Aes256Gcm, Nonce};
use rand::RngCore;
use serde_json::Value;

const MAGIC: &[u8; 8] = b"DATUMENC";

#[derive(Debug)]
pub struct Loaded {
    pub document: Value,
    pub protected: bool,
}

pub fn load(path: &str) -> Result<Loaded, String> {
    let bytes = std::fs::read(path).map_err(|e| format!("could not read {path}: {e}"))?;
    if bytes.starts_with(MAGIC) {
        let json = decrypt(&bytes)?;
        let document = serde_json::from_slice(&json).map_err(|e| format!("protected file is corrupt: {e}"))?;
        return Ok(Loaded { document, protected: true });
    }
    let document = serde_json::from_slice(&bytes).map_err(|e| format!("{path} is not a Datum document: {e}"))?;
    Ok(Loaded { document, protected: false })
}

pub fn save(path: &str, document: &Value, protect: bool) -> Result<(), String> {
    let json = serde_json::to_vec_pretty(document).map_err(|e| e.to_string())?;
    let bytes = if protect { encrypt(&json)? } else { json };
    // write-then-rename so a crash never leaves a half-written design
    let tmp = format!("{path}.tmp");
    std::fs::write(&tmp, &bytes).map_err(|e| format!("could not write {tmp}: {e}"))?;
    std::fs::rename(&tmp, path).map_err(|e| format!("could not replace {path}: {e}"))
}

fn encrypt(plain: &[u8]) -> Result<Vec<u8>, String> {
    let mut key = [0u8; 32];
    let mut nonce = [0u8; 12];
    rand::rngs::OsRng.fill_bytes(&mut key);
    rand::rngs::OsRng.fill_bytes(&mut nonce);
    let cipher = Aes256Gcm::new_from_slice(&key).map_err(|e| e.to_string())?;
    let sealed = cipher.encrypt(Nonce::from_slice(&nonce), plain).map_err(|_| "encryption failed")?;
    let wrapped = dpapi::protect(&key)?;
    key.fill(0);
    let mut out = Vec::with_capacity(8 + 1 + 4 + wrapped.len() + 12 + sealed.len());
    out.extend_from_slice(MAGIC);
    out.push(1);
    out.extend_from_slice(&(wrapped.len() as u32).to_le_bytes());
    out.extend_from_slice(&wrapped);
    out.extend_from_slice(&nonce);
    out.extend_from_slice(&sealed);
    Ok(out)
}

fn decrypt(bytes: &[u8]) -> Result<Vec<u8>, String> {
    let bad = || "protected file is truncated or corrupt".to_string();
    if bytes.len() < 13 || bytes[8] != 1 {
        return Err("unsupported protected-file version".into());
    }
    let len = u32::from_le_bytes(bytes[9..13].try_into().unwrap()) as usize;
    let wrapped = bytes.get(13..13 + len).ok_or_else(bad)?;
    let nonce = bytes.get(13 + len..25 + len).ok_or_else(bad)?;
    let sealed = bytes.get(25 + len..).ok_or_else(bad)?;
    let mut key = dpapi::unprotect(wrapped)
        .map_err(|_| "this file is locked to another device or user account and cannot be opened here".to_string())?;
    let cipher = Aes256Gcm::new_from_slice(&key).map_err(|e| e.to_string())?;
    key.fill(0);
    cipher
        .decrypt(Nonce::from_slice(nonce), sealed)
        .map_err(|_| "protected file failed its integrity check (modified or corrupt)".to_string())
}

#[cfg(windows)]
mod dpapi {
    use windows_sys::Win32::Foundation::LocalFree;
    use windows_sys::Win32::Security::Cryptography::{
        CryptProtectData, CryptUnprotectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
    };

    fn run(input: &[u8], protect: bool) -> Result<Vec<u8>, String> {
        let entropy = b"datum-core/.datum/v1";
        let mut inp = CRYPT_INTEGER_BLOB { cbData: input.len() as u32, pbData: input.as_ptr() as *mut u8 };
        let mut ent = CRYPT_INTEGER_BLOB { cbData: entropy.len() as u32, pbData: entropy.as_ptr() as *mut u8 };
        let mut out = CRYPT_INTEGER_BLOB { cbData: 0, pbData: std::ptr::null_mut() };
        let ok = unsafe {
            if protect {
                CryptProtectData(&mut inp, std::ptr::null(), &mut ent, std::ptr::null(), std::ptr::null(), CRYPTPROTECT_UI_FORBIDDEN, &mut out)
            } else {
                CryptUnprotectData(&mut inp, std::ptr::null_mut(), &mut ent, std::ptr::null(), std::ptr::null(), CRYPTPROTECT_UI_FORBIDDEN, &mut out)
            }
        };
        if ok == 0 {
            return Err("DPAPI call failed".into());
        }
        let data = unsafe { std::slice::from_raw_parts(out.pbData, out.cbData as usize).to_vec() };
        unsafe { LocalFree(out.pbData as _) };
        Ok(data)
    }
    pub fn protect(data: &[u8]) -> Result<Vec<u8>, String> {
        run(data, true)
    }
    pub fn unprotect(data: &[u8]) -> Result<Vec<u8>, String> {
        run(data, false)
    }
}

#[cfg(not(windows))]
mod dpapi {
    pub fn protect(_: &[u8]) -> Result<Vec<u8>, String> {
        Err("file protection is only available on Windows".into())
    }
    pub fn unprotect(_: &[u8]) -> Result<Vec<u8>, String> {
        Err("file protection is only available on Windows".into())
    }
}

#[cfg(all(test, windows))]
mod tests {
    use super::*;
    use serde_json::json;

    fn temp(name: &str) -> String {
        std::env::temp_dir().join(name).to_string_lossy().into_owned()
    }

    #[test]
    fn plain_round_trip_is_readable_json() {
        let path = temp("datum_plain_test.datum");
        let doc = json!({"version": 1, "features": [{"id": "a", "type": "sphere"}]});
        save(&path, &doc, false).unwrap();
        assert!(std::fs::read_to_string(&path).unwrap().contains("\"sphere\""));
        let loaded = load(&path).unwrap();
        assert!(!loaded.protected);
        assert_eq!(loaded.document, doc);
    }

    #[test]
    fn protected_round_trip_hides_content_and_detects_tampering() {
        let path = temp("datum_locked_test.datum");
        let doc = json!({"version": 1, "name": "secret-bracket", "features": []});
        save(&path, &doc, true).unwrap();
        let bytes = std::fs::read(&path).unwrap();
        assert!(bytes.starts_with(MAGIC));
        assert!(!String::from_utf8_lossy(&bytes).contains("secret-bracket"), "plaintext leaked into the file");
        let loaded = load(&path).unwrap();
        assert!(loaded.protected);
        assert_eq!(loaded.document, doc);

        let mut tampered = bytes.clone();
        let last = tampered.len() - 1;
        tampered[last] ^= 0x01;
        std::fs::write(&path, &tampered).unwrap();
        assert!(load(&path).unwrap_err().contains("integrity"));
    }

    #[test]
    fn a_key_wrapped_elsewhere_cannot_be_opened() {
        // simulate a file from another machine: a wrapped key DPAPI cannot unwrap here
        let path = temp("datum_foreign_test.datum");
        let mut bytes = Vec::from(&MAGIC[..]);
        bytes.push(1);
        bytes.extend_from_slice(&16u32.to_le_bytes());
        bytes.extend_from_slice(&[7u8; 16]);
        bytes.extend_from_slice(&[0u8; 12]);
        bytes.extend_from_slice(&[0u8; 32]);
        std::fs::write(&path, &bytes).unwrap();
        assert!(load(&path).unwrap_err().contains("locked to another device"));
    }
}
