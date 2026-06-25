//! Linux platform implementation for Sufur.
//!
//! Device enumeration prefers libudev (`src/udev.rs`) when the `udev` feature
//! is enabled at build time and libudev is available at runtime, falling back
//! to direct sysfs reads (`src/sysfs.rs`).  Partitioning (libfdisk) and mount
//! remain stubs pending Phase 2.  Hotplug monitoring (`watch_devices`) will
//! use a udev monitor socket — listed as "not yet designed" in the
//! architecture's open-decisions table.
//! //! The entire crate compiles to an empty library on non-Linux targets via
//! `#![cfg(target_os = "linux")]`, so the workspace builds on other platforms without
//! the linux-only dependencies being fetched.

#![cfg(target_os = "linux")]

mod blockdev;
mod sysfs;
#[cfg(feature = "udev")]
mod udev;

use std::fs::OpenOptions;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;

use futures::stream::{self, BoxStream, StreamExt};
use sufur_platform::{
    BlockDevice, Device, DeviceEvent, DeviceId, Error, ErrorCode, Filesystem, FormatOptions,
    MountHandle, Partition, Platform,
};

use blockdev::LinuxBlockDevice;

/// Linux implementation of [`Platform`].
pub struct LinuxPlatform;

impl Platform for LinuxPlatform {
    fn list_devices(&self) -> Result<Vec<Device>, Error> {
        #[cfg(feature = "udev")]
        {
            if let Ok(devices) = udev::enumerate() {
                return Ok(devices);
            }
        }
        sysfs::enumerate(std::path::Path::new(sysfs::SYS_BLOCK))
    }

    fn open_device(&self, id: &DeviceId) -> Result<Box<dyn BlockDevice>, Error> {
        let devices = self.list_devices()?;
        let device = devices.iter().find(|d| &d.id == id).ok_or_else(|| {
            Error::platform(
                ErrorCode::DeviceNotFound,
                format!("device not found: {}", id.0),
            )
        })?;

        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .custom_flags(libc::O_EXCL)
            .open(&device.path)
            .map_err(|e| {
                if e.kind() == std::io::ErrorKind::PermissionDenied {
                    Error::platform(
                        ErrorCode::PermissionDenied,
                        format!("cannot open {}: {}", device.path.display(), e),
                    )
                } else {
                    Error::platform(
                        ErrorCode::DeviceBusy,
                        format!("cannot open {}: {}", device.path.display(), e),
                    )
                }
            })?;

        Ok(Box::new(LinuxBlockDevice::new(file, device.size_bytes)))
    }

    fn format_partition(
        &self,
        _partition: &Partition,
        _fs: Filesystem,
        _opts: FormatOptions,
    ) -> Result<(), Error> {
        // Phase 2: delegate to sufur-fs (ntfs-3g / dosfstools / exfatprogs).
        Err(Error::platform(
            ErrorCode::Internal,
            "format not yet implemented",
        ))
    }

    fn mount(&self, _src: &Path, tgt: &Path, _fs: Option<&str>) -> Result<MountHandle, Error> {
        Ok(MountHandle {
            target: tgt.to_owned(),
        })
    }

    fn watch_devices(&self) -> BoxStream<'static, DeviceEvent> {
        // Phase 2: udev monitor socket → Stream of DeviceEvent.
        stream::empty().boxed()
    }
}
