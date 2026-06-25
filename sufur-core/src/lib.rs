//! Sufur core — the single source of truth for all domain logic.
//!
//! `sufur-core` owns image analysis, the create pipeline, progress streaming,
//! and the [`Sufur`] engine that fronts a platform implementation.  It is
//! process-agnostic: the same engine executes inside an unprivileged frontend
//! (analysis/planning) or a short-lived elevated helper (writes).
//!
//! See `SUFUR_ARCHITECTURE.md` § "Core Philosophy" and § "Progress &
//! Cancellation".

pub use sufur_platform::{
    BlockDevice, Device, DeviceEvent, DeviceId, Error, ErrorCode, Filesystem, FormatOptions,
    MountHandle, Partition, Platform, Remediation,
};

pub mod image;
pub mod pipeline;
pub mod progress;

use std::sync::Arc;

// ──────────────────────────────────────────────────────────────────────
//  Engine
// ──────────────────────────────────────────────────────────────────────

/// The Sufur engine.  Constructed with an injected [`Platform`] so that all
/// I/O is testable without real devices.
///
/// ```ignore
/// let engine = Sufur::new(sufur_linux::LinuxPlatform);
/// ```
pub struct Sufur {
    platform: Arc<dyn Platform>,
}

impl Sufur {
    /// Primary constructor — platform injected for testability.
    pub fn new(platform: impl Platform + 'static) -> Self {
        Self {
            platform: Arc::new(platform),
        }
    }

    /// Convenience constructor — selects the correct platform for the current
    /// OS.  Linux and macOS are wired up; other targets return an error.
    pub fn for_current_platform() -> Result<Self, Error> {
        #[cfg(target_os = "linux")]
        {
            Ok(Self::new(sufur_linux::LinuxPlatform))
        }
        #[cfg(target_os = "macos")]
        {
            Ok(Self::new(sufur_macos::MacosPlatform))
        }
        #[cfg(not(any(target_os = "linux", target_os = "macos")))]
        {
            Err(Error::platform(
                ErrorCode::Internal,
                "no platform implementation for this OS",
            ))
        }
    }

    /// Borrow the injected platform.  Exposed for the CLI / helper to call
    /// analysis-level operations (device enumeration, etc.) directly.
    pub fn platform(&self) -> &dyn Platform {
        self.platform.as_ref()
    }

    /// Enumerate removable devices via the platform.
    pub fn list_devices(&self) -> Result<Vec<Device>, Error> {
        self.platform.list_devices()
    }
}
