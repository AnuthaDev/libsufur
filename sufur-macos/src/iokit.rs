//! IOKit-based block device enumeration for macOS.
//!
//! Enumerates `IOMedia` services that are **removable** and **whole** (i.e.
//! entire disks, not partitions).  For each device reads the BSD name
//! (`/dev/diskN`), size, and vendor/model strings.
//!
//! The approach follows the one-shot `IOServiceGetMatchingServices` pattern
//! (no `IOServiceAddMatchingNotification` run-loop callback) validated in
//! <https://nachtimwald.com/2020/12/06/macos-usb-enumeration-in-c/> — correct
//! for a short-lived CLI process that needs a snapshot, not hotplug events.

use std::ffi::CStr;

use objc2_core_foundation::{CFBoolean, CFDictionary, CFNumber, CFRetained, CFString};
use objc2_io_kit::{
    io_iterator_t, io_object_t, io_registry_entry_t, kIOReturnSuccess, kIOServicePlane,
    IOIteratorNext, IOObjectConformsTo, IOObjectRelease, IORegistryEntryCreateCFProperty,
    IORegistryEntryGetParentEntry, IOServiceGetMatchingServices, IOServiceMatching,
};

use crate::usb;
use sufur_platform::{Device, DeviceId, Error, ErrorCode};

// ──────────────────────────────────────────────────────────────────────
//  IOKit property keys (string constants from IOMedia.h, IOBSD.h, etc.)
// ──────────────────────────────────────────────────────────────────────

/// IOMedia class name for `IOServiceMatching`.
const IOMEDIA_CLASS: &CStr = c"IOMedia";
/// `"Size"` — IOMedia size key (kIOMediaSizeKey).
const SIZE_KEY: &str = "Size";
/// `"Removable"` — IOMedia removable key (kIOMediaRemovableKey).
const REMOVABLE_KEY: &str = "Removable";
/// `"Whole"` — IOMedia whole-disk key (kIOMediaWholeKey).
const WHOLE_KEY: &str = "Whole";
/// `"BSD Name"` — disk BSD name key (kIOBSDNameKey).
const BSD_NAME_KEY: &str = "BSD Name";
/// `"IOBlockStorageDriver"` — class to validate the parent.
const BLOCK_STORAGE_DRIVER_CLASS: &CStr = c"IOBlockStorageDriver";
/// `"Device Characteristics"` — dictionary on IOBlockStorageDevice ancestors.
const DEVICE_CHARACTERISTICS_KEY: &str = "Device Characteristics";
/// `"Vendor Name"` — inside Device Characteristics dict.
const VENDOR_NAME_KEY: &str = "Vendor Name";
/// `"Product Name"` — inside Device Characteristics dict.
const PRODUCT_NAME_KEY: &str = "Product Name";
/// `"USB Vendor Name"` — on IOUSBDevice ancestors.
const USB_VENDOR_NAME_KEY: &str = "USB Vendor Name";
/// `"USB Product Name"` — on IOUSBDevice ancestors.
const USB_PRODUCT_NAME_KEY: &str = "USB Product Name";

/// Maximum depth for the parent walk.
const MAX_PARENT_DEPTH: usize = 32;

// ──────────────────────────────────────────────────────────────────────
//  RAII guard for IOKit objects
// ──────────────────────────────────────────────────────────────────────

/// Owns an IOKit object handle and releases it on drop.
struct IoObject(io_object_t);

impl IoObject {
    fn new(obj: io_object_t) -> Option<Self> {
        if obj == 0 {
            None
        } else {
            Some(Self(obj))
        }
    }
}

impl Drop for IoObject {
    fn drop(&mut self) {
        if self.0 != 0 {
            let _ = IOObjectRelease(self.0);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
//  Property readers
// ──────────────────────────────────────────────────────────────────────

/// Read a string property from a registry entry.
fn read_string(entry: io_registry_entry_t, key: &str) -> Option<String> {
    let cf_key = CFString::from_str(key);
    let prop = unsafe { IORegistryEntryCreateCFProperty(entry, Some(&cf_key), None, 0) }?;
    let s = prop.downcast_ref::<CFString>()?;
    Some(s.to_string())
}

/// Read a numeric property from a registry entry as `u64`.
fn read_number(entry: io_registry_entry_t, key: &str) -> Option<u64> {
    let cf_key = CFString::from_str(key);
    let prop = unsafe { IORegistryEntryCreateCFProperty(entry, Some(&cf_key), None, 0) }?;
    let num = prop.downcast_ref::<CFNumber>()?;
    // CFNumber may store the value as any integer width.  Try i64 first.
    num.as_i64().map(|v| v as u64)
}

/// Read a boolean property from a registry entry.
fn read_bool(entry: io_registry_entry_t, key: &str) -> bool {
    let cf_key = CFString::from_str(key);
    let Some(prop) = (unsafe { IORegistryEntryCreateCFProperty(entry, Some(&cf_key), None, 0) })
    else {
        return false;
    };
    // IOMedia's Removable/Whole are OSBoolean → CFBoolean in CF.
    prop.downcast_ref::<CFBoolean>()
        .is_some_and(|b| b.as_bool())
}

// ──────────────────────────────────────────────────────────────────────
//  Parent walk for vendor / model (Tier 1 — registry properties)
// ──────────────────────────────────────────────────────────────────────

/// Walk up the `IOService` plane from `media` looking for vendor and model
/// strings.
///
/// On each ancestor, tries (in order):
///   1. `"Device Characteristics"` dict → `"Vendor Name"` / `"Product Name"`
///   2. `"USB Vendor Name"` / `"USB Product Name"` directly
///
/// Stops as soon as both are found or `MAX_PARENT_DEPTH` is exhausted.
fn vendor_and_model(media: io_registry_entry_t) -> (String, String) {
    let mut vendor = String::new();
    let mut model = String::new();
    let mut current = media;

    for _ in 0..MAX_PARENT_DEPTH {
        // 1. Device Characteristics dictionary
        if let Some(chars) = read_dictionary(current, DEVICE_CHARACTERISTICS_KEY) {
            if vendor.is_empty() {
                vendor = dict_string(&chars, VENDOR_NAME_KEY);
            }
            if model.is_empty() {
                model = dict_string(&chars, PRODUCT_NAME_KEY);
            }
        }

        // 2. USB string properties on the ancestor itself
        if vendor.is_empty() {
            vendor = read_string(current, USB_VENDOR_NAME_KEY).unwrap_or_default();
        }
        if model.is_empty() {
            model = read_string(current, USB_PRODUCT_NAME_KEY).unwrap_or_default();
        }

        if !vendor.is_empty() && !model.is_empty() {
            break;
        }

        // Walk to parent
        let Some(parent) = parent_entry(current) else {
            break;
        };
        // Release intermediate parents (but never the original `media` —
        // it's owned by the caller's IoObject guard).
        if current != media {
            let _ = IOObjectRelease(current);
        }
        current = parent;
    }

    if current != media {
        let _ = IOObjectRelease(current);
    }

    (vendor, model)
}

/// Read a CFDictionary property.
fn read_dictionary(entry: io_registry_entry_t, key: &str) -> Option<CFRetained<CFDictionary>> {
    let cf_key = CFString::from_str(key);
    let prop = unsafe { IORegistryEntryCreateCFProperty(entry, Some(&cf_key), None, 0) }?;
    // Downcast the CFRetained<CFType> to CFRetained<CFDictionary>.
    // SAFETY: The property is an OSDictionary in the kernel, which maps to
    // CFDictionary in CF.  If it's not, the downcast returns Err (→ None).
    prop.downcast::<CFDictionary>().ok()
}

/// Extract a string value from a CFDictionary by key.
fn dict_string(dict: &CFDictionary, key: &str) -> String {
    let cf_key = CFString::from_str(key);
    // Cast the opaque dictionary to typed <CFString, CFType> so we can use .get().
    // SAFETY: IOKit registry dictionaries use CFString keys and CFType values.
    let typed_dict = unsafe { dict.cast_unchecked::<CFString, objc2_core_foundation::CFType>() };
    let value = typed_dict.get(&cf_key);
    match value {
        Some(v) => v
            .downcast::<CFString>()
            .ok()
            .map(|s| s.to_string())
            .unwrap_or_default(),
        None => String::new(),
    }
}

/// Get the parent entry in the `IOService` plane.
fn parent_entry(entry: io_registry_entry_t) -> Option<io_registry_entry_t> {
    let mut parent: io_registry_entry_t = 0;
    let kr = unsafe {
        IORegistryEntryGetParentEntry(entry, kIOServicePlane.as_ptr() as *mut _, &mut parent)
    };
    if kr != kIOReturnSuccess || parent == 0 {
        None
    } else {
        Some(parent)
    }
}

// ──────────────────────────────────────────────────────────────────────
//  Validation
// ──────────────────────────────────────────────────────────────────────

/// Check that the immediate parent of `media` conforms to
/// `IOBlockStorageDriver` — confirms this is a real block-storage-backed
/// whole disk, not a virtual or synthetic IOMedia object.
fn has_block_storage_parent(media: io_registry_entry_t) -> bool {
    let Some(parent) = parent_entry(media) else {
        return false;
    };
    let ok = unsafe { IOObjectConformsTo(parent, BLOCK_STORAGE_DRIVER_CLASS.as_ptr() as *mut _) };
    let _ = IOObjectRelease(parent);
    ok
}

// ──────────────────────────────────────────────────────────────────────
//  Main enumeration
// ──────────────────────────────────────────────────────────────────────

/// Enumerate removable whole-disk IOMedia objects on macOS.
///
/// Returns a list of [`Device`] entries suitable for `sufur list`.
pub fn enumerate() -> Result<Vec<Device>, Error> {
    // 1. Build matching dictionary for IOMedia class.
    let matching = unsafe { IOServiceMatching(IOMEDIA_CLASS.as_ptr()) }.ok_or_else(|| {
        Error::platform(
            ErrorCode::Internal,
            "IOServiceMatching(IOMedia) returned null",
        )
    })?;

    // Convert CFRetained<CFMutableDictionary> → CFRetained<CFDictionary>
    // (IOServiceGetMatchingServices consumes the dictionary).
    let matching = CFRetained::<CFDictionary>::from(&matching);

    // 2. Get an iterator over matching services.
    //    Pass 0 as the master port (equivalent to kIOMasterPortDefault /
    //    MACH_PORT_NULL).
    let mut iterator: io_iterator_t = 0;
    let kr = unsafe { IOServiceGetMatchingServices(0, Some(matching), &mut iterator) };
    if kr != kIOReturnSuccess {
        return Err(Error::platform(
            ErrorCode::Internal,
            format!("IOServiceGetMatchingServices failed: {kr}"),
        ));
    }
    let _iter_guard = IoObject::new(iterator)
        .ok_or_else(|| Error::platform(ErrorCode::Internal, "invalid iterator"))?;

    let mut devices = Vec::new();

    // 3. Iterate IOMedia objects.
    loop {
        let media = IOIteratorNext(iterator);
        if media == 0 {
            break;
        }
        let _media_guard = IoObject::new(media);

        // ── Read core properties ──

        let bsd_name = read_string(media, BSD_NAME_KEY).unwrap_or_default();
        if bsd_name.is_empty() {
            continue;
        }

        // Filter: only removable whole disks.
        let removable = read_bool(media, REMOVABLE_KEY);
        if !removable {
            continue;
        }
        let whole = read_bool(media, WHOLE_KEY);
        if !whole {
            continue;
        }

        let size_bytes = read_number(media, SIZE_KEY).unwrap_or(0);

        // Sanity check: parent must be a real block storage driver.
        if !has_block_storage_parent(media) {
            continue;
        }

        // ── Vendor / model (Tier 1: registry properties) ──

        let (mut vendor, mut model) = vendor_and_model(media);

        // ── USB info (Tier 2: IOUSBDeviceInterface descriptors) ──

        let usb_info = usb::get_usb_info(media);

        // Fill in missing vendor/model from USB descriptor data.
        if vendor.is_empty() {
            vendor = usb_info
                .as_ref()
                .and_then(|u| u.manufacturer.clone())
                .unwrap_or_default();
        }
        if model.is_empty() {
            model = usb_info
                .as_ref()
                .and_then(|u| u.product.clone())
                .unwrap_or_default();
        }

        // ── Device ID ──
        //
        // Prefer a composite USB identifier (vendor_id:product_id:serial)
        // analogous to Linux's `ID_SERIAL`.  Fall back to the BSD name
        // (e.g. "disk2") when USB info is unavailable — matching the
        // Linux crate's sysname fallback.
        let device_id = usb_info
            .as_ref()
            .and_then(|u| u.composite_id())
            .unwrap_or_else(|| bsd_name.clone());

        devices.push(Device {
            id: DeviceId(device_id),
            path: std::path::PathBuf::from(format!("/dev/{bsd_name}")),
            vendor,
            model,
            size_bytes,
            removable: true,
        });
    }

    Ok(devices)
}

// ──────────────────────────────────────────────────────────────────────
//  Tests
// ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn iomedia_class_constant() {
        assert_eq!(IOMEDIA_CLASS.to_str().unwrap(), "IOMedia");
    }

    #[test]
    fn block_storage_driver_constant() {
        assert_eq!(
            BLOCK_STORAGE_DRIVER_CLASS.to_str().unwrap(),
            "IOBlockStorageDriver"
        );
    }

    #[test]
    fn property_key_constants() {
        assert_eq!(SIZE_KEY, "Size");
        assert_eq!(REMOVABLE_KEY, "Removable");
        assert_eq!(WHOLE_KEY, "Whole");
        assert_eq!(BSD_NAME_KEY, "BSD Name");
    }
}
