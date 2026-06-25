//! Image analysis.
//!
//! Analyses an ISO or disk image to determine whether it is a Windows ISO,
//! Win2Go-capable, etc.  The public API accepts [`std::io::Read`] + [`Seek`]
//! rather than `&Path` — this is mandatory for WASM compatibility and fuzz
//! targets (architecture § "Testing Strategy" and § "WebUSB & Web Frontend").

use std::io::{Read, Seek, SeekFrom};

use serde::{Serialize, Deserialize};

use crate::Error;

/// Metadata discovered about a disk image.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ImageInfo {
    pub path: Option<String>,
    pub size_bytes: u64,
    pub is_windows_iso: bool,
    pub is_win2go_capable: bool,
}

/// Analyse a readable, seekable image source.
///
/// Scaffold: returns a best-effort [`ImageInfo`] with size from `Seek::seek`
/// and the Windows/Win2Go flags as `false` until format parsers (ISO 9660,
/// WIM, NTFS boot) are implemented in Phase 2.
pub fn analyze(mut source: impl Read + Seek) -> Result<ImageInfo, Error> {
    let size = source.seek(SeekFrom::End(0))?;
    source.seek(SeekFrom::Start(0))?;
    Ok(ImageInfo {
        path: None,
        size_bytes: size,
        is_windows_iso: false,
        is_win2go_capable: false,
    })
}
