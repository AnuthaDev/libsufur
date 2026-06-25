//! sysfs-based block device enumeration.
//!
//! Reads `/sys/block/<name>/` to discover physical, removable block devices
//! (USB flash drives, SD cards).  Pure sysfs — no libudev link dependency —
//! which keeps `cargo build` self-contained (architecture § "Vendored C
//! Dependencies": self-contained build principle).  The privilege table
//! itself lists the enumeration mechanism as "udev sysfs read (Linux)";
//! sysfs is the source udev reads from.

use std::path::{Path, PathBuf};

use sufur_platform::{Device, DeviceId, Error};

/// Logical block size reported by sysfs `size` (in 512-byte sectors).
const SECTOR_SIZE: u64 = 512;

/// Default sysfs block-class root.
pub const SYS_BLOCK: &str = "/sys/block";

/// Block device name prefixes for virtual / non-physical devices to skip.
const VIRTUAL_PREFIXES: &[&str] = &["loop", "ram", "zram", "dm-", "md", "sr"];

fn is_virtual(name: &str) -> bool {
    VIRTUAL_PREFIXES.iter().any(|p| name.starts_with(p))
}

/// Enumerate removable physical block devices under `block_dir`.
///
/// `block_dir` is normally `/sys/block`; tests pass a fixture path.  A
/// missing or unreadable directory yields an empty list (not an error) so
/// the CLI degrades gracefully on systems without sysfs.
pub fn enumerate(block_dir: &Path) -> Result<Vec<Device>, Error> {
    let mut devices = Vec::new();
    let entries = match std::fs::read_dir(block_dir) {
        Ok(rd) => rd,
        Err(_) => return Ok(Vec::new()),
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if is_virtual(&name) {
            continue;
        }
        let sysfs = entry.path();
        if !is_removable(&sysfs) {
            continue;
        }
        devices.push(read_device(&sysfs, &name));
    }
    Ok(devices)
}

fn is_removable(sysfs: &Path) -> bool {
    read_attr(sysfs, "removable")
        .and_then(|s| s.trim().parse::<u8>().ok())
        .map(|v| v == 1)
        .unwrap_or(false)
}

fn read_device(sysfs: &Path, name: &str) -> Device {
    let vendor = read_attr(sysfs, "device/vendor")
        .unwrap_or_default()
        .trim()
        .to_string();
    let model = read_attr(sysfs, "device/model")
        .unwrap_or_default()
        .trim()
        .to_string();
    let size_sectors = read_attr(sysfs, "size")
        .and_then(|s| s.trim().parse::<u64>().ok())
        .unwrap_or(0);
    Device {
        id: DeviceId(name.to_string()),
        path: PathBuf::from(format!("/dev/{name}")),
        vendor,
        model,
        size_bytes: size_sectors * SECTOR_SIZE,
        removable: true,
    }
}

fn read_attr(sysfs: &Path, name: &str) -> Option<String> {
    std::fs::read_to_string(sysfs.join(name)).ok()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn make_device(
        root: &Path,
        name: &str,
        removable: bool,
        size: &str,
        vendor: &str,
        model: &str,
    ) {
        let dev = root.join("sys/block").join(name);
        fs::create_dir_all(&dev).unwrap();
        fs::write(dev.join("removable"), if removable { "1" } else { "0" }).unwrap();
        fs::write(dev.join("size"), size).unwrap();
        let device_dir = dev.join("device");
        fs::create_dir_all(&device_dir).unwrap();
        fs::write(device_dir.join("vendor"), vendor).unwrap();
        fs::write(device_dir.join("model"), model).unwrap();
    }

    #[test]
    fn enumerates_a_removable_device() {
        let tmp = tempfile::tempdir().unwrap();
        make_device(
            tmp.path(),
            "sda",
            true,
            "7864320",
            "SanDisk",
            "Cruzer Blade",
        );
        let devices = enumerate(&tmp.path().join("sys/block")).unwrap();
        assert_eq!(devices.len(), 1);
        let d = &devices[0];
        assert_eq!(d.id, DeviceId("sda".into()));
        assert_eq!(d.path, PathBuf::from("/dev/sda"));
        assert_eq!(d.vendor, "SanDisk");
        assert_eq!(d.model, "Cruzer Blade");
        assert_eq!(d.size_bytes, 7864320 * 512);
        assert!(d.removable);
    }

    #[test]
    fn skips_non_removable_devices() {
        let tmp = tempfile::tempdir().unwrap();
        make_device(tmp.path(), "sda", true, "1000", "V1", "M1");
        make_device(tmp.path(), "sdb", false, "2000", "V2", "M2");
        let devices = enumerate(&tmp.path().join("sys/block")).unwrap();
        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].id, DeviceId("sda".into()));
    }

    #[test]
    fn skips_virtual_block_devices() {
        let tmp = tempfile::tempdir().unwrap();
        make_device(tmp.path(), "loop0", true, "100", "L", "Loop");
        make_device(tmp.path(), "ram0", true, "100", "R", "Ram");
        make_device(tmp.path(), "sr0", true, "100", "O", "Optical");
        make_device(tmp.path(), "sda", true, "1000", "S", "Real");
        let devices = enumerate(&tmp.path().join("sys/block")).unwrap();
        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].id, DeviceId("sda".into()));
    }

    #[test]
    fn empty_when_block_dir_missing() {
        let devices = enumerate(Path::new("/no/such/path/__sufur_test__")).unwrap();
        assert!(devices.is_empty());
    }

    #[test]
    fn missing_vendor_attr_defaults_empty() {
        let tmp = tempfile::tempdir().unwrap();
        let dev = tmp.path().join("sys/block/sda");
        fs::create_dir_all(&dev).unwrap();
        fs::write(dev.join("removable"), "1").unwrap();
        fs::write(dev.join("size"), "2048").unwrap();
        // no device/ subdir → vendor/model attrs absent
        let devices = enumerate(&tmp.path().join("sys/block")).unwrap();
        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].vendor, "");
        assert_eq!(devices[0].model, "");
        assert_eq!(devices[0].size_bytes, 2048 * 512);
    }

    #[test]
    fn whitespace_in_attrs_is_trimmed() {
        let tmp = tempfile::tempdir().unwrap();
        make_device(
            tmp.path(),
            "sda",
            true,
            "1000",
            "  SanDisk  \n",
            " Cruzer \n",
        );
        let devices = enumerate(&tmp.path().join("sys/block")).unwrap();
        assert_eq!(devices[0].vendor, "SanDisk");
        assert_eq!(devices[0].model, "Cruzer");
    }
}
