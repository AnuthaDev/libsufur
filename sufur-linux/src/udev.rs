//! libudev-based block device enumeration.
//!
//! Uses the `udev` crate to enumerate block devices with rich metadata:
//! udev-computed properties (`ID_SERIAL`, `ID_VENDOR`, `ID_MODEL`) provide
//! stable identity and human-readable names that sysfs raw attributes lack.
//!
//! At build time this module is only compiled when the `udev` feature is
//! enabled.  At runtime, `lib.rs::list_devices` tries this path first and
//! falls back to `sysfs::enumerate` if libudev is unavailable (e.g. udev
//! daemon not running).

use std::path::PathBuf;

use sufur_platform::{Device, DeviceId, Error};

const SECTOR_SIZE: u64 = 512;

const VIRTUAL_PREFIXES: &[&str] = &["loop", "ram", "zram", "dm-", "md", "sr"];

fn is_virtual(name: &str) -> bool {
    VIRTUAL_PREFIXES.iter().any(|p| name.starts_with(p))
}

/// Enumerate removable block devices via libudev.
///
/// Filters to `subsystem == "block"`, skips virtual devices, and keeps only
/// those with `removable == 1`.  Enriches each [`Device`] with udev properties
/// for vendor/model, and uses `ID_SERIAL` as a stable [`DeviceId`] (falling
/// back to the kernel sysname if no serial is available).
pub fn enumerate() -> Result<Vec<Device>, Error> {
    let mut enumerator = udev::Enumerator::new()?;
    enumerator.match_subsystem("block")?;

    let mut devices = Vec::new();
    for dev in enumerator.scan_devices()? {
        let sysname = dev.sysname().to_string_lossy().into_owned();
        if is_virtual(&sysname) {
            continue;
        }

        let removable = dev
            .attribute_value("removable")
            .and_then(|v| v.to_str())
            .map(|s| s.trim() == "1")
            .unwrap_or(false);
        if !removable {
            continue;
        }

        let vendor = prop_or_attr(&dev, "ID_VENDOR", "device/vendor");
        let model = prop_or_attr(&dev, "ID_MODEL", "device/model");

        let size_sectors = dev
            .attribute_value("size")
            .and_then(|v| v.to_str())
            .and_then(|s| s.trim().parse::<u64>().ok())
            .unwrap_or(0);

        let id = dev
            .property_value("ID_SERIAL")
            .map(|v| v.to_string_lossy().into_owned())
            .map(DeviceId)
            .unwrap_or_else(|| DeviceId(sysname.clone()));

        let path = dev
            .devnode()
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(format!("/dev/{sysname}")));

        devices.push(Device {
            id,
            path,
            vendor,
            model,
            size_bytes: size_sectors * SECTOR_SIZE,
            removable: true,
        });
    }
    Ok(devices)
}

fn prop_or_attr(dev: &udev::Device, prop: &str, attr: &str) -> String {
    dev.property_value(prop)
        .or_else(|| dev.attribute_value(attr))
        .map(|v| v.to_string_lossy().trim().to_string())
        .unwrap_or_default()
}
