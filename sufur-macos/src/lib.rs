//! macOS platform implementation for Sufur.
//!
//! Device enumeration uses IOKit to find removable whole-disk `IOMedia`
//! objects (USB flash drives, SD cards).  Vendor and model strings are
//! extracted via two tiers:
//!
//!   **Tier 1 — Registry properties (fast path, no device access):**
//!   Walk up the `IOService` plane reading `"Device Characteristics"` and
//!   `"USB Vendor Name"` / `"USB Product Name"` from ancestor nodes.
//!
//!   **Tier 2 — USB descriptor via `IOUSBDeviceInterface` (reliable fallback):**
//!   When Tier 1 yields empty strings, obtain the `IOUSBDeviceInterface`
//!   COM vtable via `IOCreatePlugInInterfaceForService` and issue a control
//!   transfer (`DeviceRequest`) to read string descriptors directly from the
//!   USB device.  No `USBDeviceOpen` is needed — string descriptors are
//!   readable without claiming the device (even when a kernel driver has
//!   attached).
//!
//! Partitioning, formatting, and mount remain stubs pending Phase 5
//! (see `SUFUR_ARCHITECTURE.md` § "Phase 5 — macOS").
//!
//! The entire crate compiles to an empty library on non-macOS targets via
//! `#![cfg(target_os = "macos")]`, so the workspace builds on Linux without
//! the macOS-only dependencies being fetched.

#![cfg(target_os = "macos")]

mod iokit;
mod usb;

use std::path::Path;

use futures::stream::{self, BoxStream, StreamExt};
use sufur_platform::{
    BlockDevice, Device, DeviceEvent, DeviceId, Error, ErrorCode, Filesystem,
    FormatOptions, MountHandle, Partition, Platform,
};

/// macOS implementation of [`Platform`].
pub struct MacosPlatform;

impl Platform for MacosPlatform {
    fn list_devices(&self) -> Result<Vec<Device>, Error> {
        iokit::enumerate()
    }

    fn open_device(&self, id: &DeviceId) -> Result<Box<dyn BlockDevice>, Error> {
        Err(Error::platform(
            ErrorCode::DeviceNotFound,
            format!("device not found: {}", id.0),
        ))
    }

    fn format_partition(
        &self,
        _partition: &Partition,
        _fs: Filesystem,
        _opts: FormatOptions,
    ) -> Result<(), Error> {
        // Phase 5: delegate to diskutil or a vendored formatter.
        Err(Error::platform(
            ErrorCode::Internal,
            "format not yet implemented",
        ))
    }

    fn mount(
        &self,
        _src: &Path,
        tgt: &Path,
        _fs: Option<&str>,
    ) -> Result<MountHandle, Error> {
        Ok(MountHandle {
            target: tgt.to_owned(),
        })
    }

    fn watch_devices(&self) -> BoxStream<'static, DeviceEvent> {
        // Phase 5: IOServiceAddMatchingNotification → Stream of DeviceEvent.
        stream::empty().boxed()
    }
}
