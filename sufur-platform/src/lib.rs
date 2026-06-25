//! Platform Abstraction Layer (PAL) for Sufur.
//!
//! Defines the traits and shared types that every platform implementation
//! (`sufur-linux`, `sufur-macos`, `sufur-windows`) must satisfy.  Business
//! logic in `sufur-core` depends only on these traits — never on a concrete
//! platform — so that the same engine runs inside a frontend process, a CLI,
//! or a short-lived elevated helper.
//!
//! See `SUFUR_ARCHITECTURE.md` § "Platform Abstraction Layer" for the design
//! rationale and the "no `#ifdef` soup" rule.

use std::path::{Path, PathBuf};

use futures::stream::BoxStream;

// ──────────────────────────────────────────────────────────────────────
//  Identifiers & device model
// ──────────────────────────────────────────────────────────────────────

/// Stable, platform-specific identifier for a block device.
///
/// On Linux this is the sysfs path or a udev-derived key; on macOS the IOKit
/// registry entry ID; on Windows the SetupAPI instance ID.
#[derive(Debug, Clone, PartialEq, Eq, Hash, serde::Serialize, serde::Deserialize)]
pub struct DeviceId(pub String);

/// A discovered block device.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct Device {
    pub id: DeviceId,
    pub path: PathBuf,
    pub vendor: String,
    pub model: String,
    pub size_bytes: u64,
    pub removable: bool,
}

/// A partition on a block device.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct Partition {
    /// 1-based partition number on the parent device.
    pub number: u32,
    pub path: PathBuf,
    pub start_bytes: u64,
    pub size_bytes: u64,
    pub filesystem: Option<Filesystem>,
    pub label: Option<String>,
}

/// Filesystem kind recognised by Sufur for formatting operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Filesystem {
    Fat32,
    Exfat,
    Ntfs,
    Ext4,
    /// Leave unformatted / raw.
    Raw,
}

/// Options passed to [`Platform::format_partition`].
#[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
pub struct FormatOptions {
    pub quick: bool,
    pub label: Option<String>,
    pub bad_blocks_check: bool,
}

/// Opaque handle to a mount created by [`Platform::mount`].
///
/// Dropping the handle unmounts the target.
#[derive(Debug)]
pub struct MountHandle {
    pub target: PathBuf,
}

impl Drop for MountHandle {
    fn drop(&mut self) {
        // Platform implementations arrange the real unmount via a stored
        // closure or raw fd.  The scaffold leaves this a no-op.
    }
}

/// Hotplug event emitted by [`Platform::watch_devices`].
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum DeviceEvent {
    Added { device: Device },
    Removed { id: DeviceId },
}

// ──────────────────────────────────────────────────────────────────────
//  Traits
// ──────────────────────────────────────────────────────────────────────

/// Raw handle to an open block device.
///
/// Implementations provide read/write/seek at the block layer.  This is only
/// ever constructed by [`Platform::open_device`] and consumed by the write
/// pipeline inside `sufur-core`.
pub trait BlockDevice: Send {
    fn read(&mut self, buf: &mut [u8], offset: u64) -> Result<usize, Error>;
    fn write(&mut self, buf: &[u8], offset: u64) -> Result<usize, Error>;
    fn size(&self) -> u64;
    fn sync(&mut self) -> Result<(), Error>;
}

/// The Platform Abstraction Layer trait.
///
/// Every platform crate implements this.  `sufur-core` holds an
/// `Arc<dyn Platform>` and dispatches all I/O through it.
///
/// **Object safety:** `watch_devices` returns a `BoxStream` rather than
/// `impl Stream` so that `Platform` remains usable as `dyn Platform`
/// (required by `sufur-core`'s `Arc<dyn Platform>` field).
pub trait Platform: Send + Sync {
    fn list_devices(&self) -> Result<Vec<Device>, Error>;
    fn open_device(&self, id: &DeviceId) -> Result<Box<dyn BlockDevice>, Error>;
    fn format_partition(
        &self,
        partition: &Partition,
        fs: Filesystem,
        opts: FormatOptions,
    ) -> Result<(), Error>;
    fn mount(&self, src: &Path, tgt: &Path, fs: Option<&str>) -> Result<MountHandle, Error>;
    fn watch_devices(&self) -> BoxStream<'static, DeviceEvent>;
}

// ──────────────────────────────────────────────────────────────────────
//  Error model
// ──────────────────────────────────────────────────────────────────────

/// Machine-actionable error codes.  These map 1:1 to the `code` field in the
/// CLI / helper NDJSON error envelope (see architecture § "CLI Interface").
#[derive(
    Debug, Clone, Copy, PartialEq, Eq, thiserror::Error, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ErrorCode {
    #[error("device busy")]
    DeviceBusy,
    #[error("device not found")]
    DeviceNotFound,
    #[error("permission denied")]
    PermissionDenied,
    #[error("invalid image")]
    InvalidImage,
    #[error("checksum mismatch")]
    ChecksumMismatch,
    #[error("unsupported filesystem")]
    UnsupportedFilesystem,
    #[error("cancelled")]
    Cancelled,
    #[error("internal error")]
    Internal,
}

/// Remediation hint attached to an error, surfaced in the NDJSON envelope as
/// the `remediation` object.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct Remediation {
    pub action: String,
    pub command: Option<String>,
    pub auto_fixable: bool,
}

/// The unified error type for all platform operations.
#[derive(Debug, thiserror::Error, serde::Serialize, serde::Deserialize)]
#[serde(tag = "type", rename_all = "lowercase")]
pub enum Error {
    #[error("{code}: {message}")]
    Platform {
        code: ErrorCode,
        message: String,
        #[serde(default)]
        remediation: Option<Remediation>,
    },
    #[error("io: {0}")]
    Io(String),
    #[error("cancelled")]
    Cancelled,
}

impl Error {
    pub fn platform(code: ErrorCode, message: impl Into<String>) -> Self {
        Error::Platform {
            code,
            message: message.into(),
            remediation: None,
        }
    }

    pub fn with_remediation(mut self, remediation: Remediation) -> Self {
        if let Error::Platform { remediation: r, .. } = &mut self {
            *r = Some(remediation);
        }
        self
    }
}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Error::Io(e.to_string())
    }
}
