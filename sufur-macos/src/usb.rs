//! USB device info extraction for macOS.
//!
//! Implements two complementary approaches for obtaining USB metadata
//! (vendor ID, product ID, manufacturer, product, serial number):
//!
//! **Approach A — Registry properties (primary):**
//! Walk up the IOService plane to find an `IOUSBDevice` / `IOUSBHostDevice`
//! ancestor, then read `idVendor`, `idProduct`, and `USB Serial Number`
//! directly from the IOKit registry.  This covers the vast majority of USB
//! storage devices and requires no device access.
//!
//! **Approach B — `IOUSBDeviceInterface` descriptor extraction (fallback):**
//! When registry properties are incomplete, obtain the `IOUSBDeviceInterface`
//! COM vtable via `IOCreatePlugInInterfaceForService` and issue USB control
//! transfers (`DeviceRequest`) to read string descriptors directly from the
//! device.  No `USBDeviceOpen` is needed — string descriptors are readable
//! without claiming the device, even when a kernel driver has attached.
//! This follows the technique described in
//! <https://nachtimwald.com/2020/12/06/macos-usb-enumeration-in-c/>.
//!
//! See `SUFUR_ARCHITECTURE.md` § "Phase 5 — macOS" for design context.

use std::ffi::{c_void, CStr};

use objc2_core_foundation::{CFNumber, CFString, CFUUIDBytes, CFUUID};
use objc2_io_kit::{
    io_registry_entry_t, kIOReturnSuccess, kIOServicePlane, IOCFPlugInInterface,
    IOCreatePlugInInterfaceForService, IODestroyPlugInInterface, IOObjectConformsTo,
    IOObjectRelease, IORegistryEntryCreateCFProperty, IORegistryEntryGetParentEntry, IOReturn,
    IOUSBDevRequest, IOUSBDeviceInterface,
};

// ──────────────────────────────────────────────────────────────────────
//  Constants
// ──────────────────────────────────────────────────────────────────────

/// `"IOUSBDevice"` — legacy USB device class name.
const IOUSB_DEVICE_CLASS: &CStr = c"IOUSBDevice";
/// `"IOUSBHostDevice"` — modern macOS USB device class name.
const IOUSB_HOST_DEVICE_CLASS: &CStr = c"IOUSBHostDevice";

/// `"idVendor"` — USB vendor ID (u16) in the registry.
const ID_VENDOR_KEY: &str = "idVendor";
/// `"idProduct"` — USB product ID (u16) in the registry.
const ID_PRODUCT_KEY: &str = "idProduct";
/// `"USB Serial Number"` — serial string in the registry.
const USB_SERIAL_NUMBER_KEY: &str = "USB Serial Number";
/// `"USB Vendor Name"` — manufacturer string in the registry.
const USB_VENDOR_NAME_KEY: &str = "USB Vendor Name";
/// `"USB Product Name"` — product string in the registry.
const USB_PRODUCT_NAME_KEY: &str = "USB Product Name";

/// Maximum depth for the parent walk when searching for a USB ancestor.
const MAX_DEPTH: usize = 32;

// ──────────────────────────────────────────────────────────────────────
//  Public types
// ──────────────────────────────────────────────────────────────────────

/// USB metadata extracted for a device.
#[derive(Debug, Clone)]
pub struct UsbInfo {
    /// Numeric USB vendor ID (e.g. `0x0781` for SanDisk).
    pub vendor_id: Option<u16>,
    /// Numeric USB product ID.
    pub product_id: Option<u16>,
    /// Manufacturer string (from registry or USB descriptor).
    pub manufacturer: Option<String>,
    /// Product name string.
    pub product: Option<String>,
    /// Device serial number.
    pub serial: Option<String>,
}

impl UsbInfo {
    /// Construct a composite device identifier: `"vendor_id:product_id:serial"`.
    ///
    /// Returns `None` if neither vendor_id nor product_id is available.
    /// Analogous to Linux's `ID_SERIAL` from udev.
    pub fn composite_id(&self) -> Option<String> {
        let vid = self.vendor_id?;
        let pid = self.product_id?;
        let serial = self.serial.as_deref().unwrap_or("");
        Some(format!("{vid:04x}:{pid:04x}:{serial}"))
    }
}

// ──────────────────────────────────────────────────────────────────────
//  Registry property helpers
// ──────────────────────────────────────────────────────────────────────

fn read_string(entry: io_registry_entry_t, key: &str) -> Option<String> {
    let cf_key = CFString::from_str(key);
    let prop = unsafe { IORegistryEntryCreateCFProperty(entry, Some(&cf_key), None, 0) }?;
    let s = prop.downcast_ref::<CFString>()?;
    Some(s.to_string())
}

fn read_number_u16(entry: io_registry_entry_t, key: &str) -> Option<u16> {
    let cf_key = CFString::from_str(key);
    let prop = unsafe { IORegistryEntryCreateCFProperty(entry, Some(&cf_key), None, 0) }?;
    let num = prop.downcast_ref::<CFNumber>()?;
    num.as_i64().map(|v| v as u16)
}

// ──────────────────────────────────────────────────────────────────────
//  USB ancestor discovery
// ──────────────────────────────────────────────────────────────────────

/// Walk up the IOService plane from `entry` to find an ancestor that
/// conforms to `IOUSBDevice` or `IOUSBHostDevice`.
///
/// Returns the ancestor's registry entry (caller must release it) or `None`.
fn find_usb_ancestor(entry: io_registry_entry_t) -> Option<io_registry_entry_t> {
    let mut current = entry;
    // Don't release the original entry — caller owns it.
    for _ in 0..MAX_DEPTH {
        // Check current node
        if unsafe { IOObjectConformsTo(current, IOUSB_DEVICE_CLASS.as_ptr() as *mut _) }
            || unsafe { IOObjectConformsTo(current, IOUSB_HOST_DEVICE_CLASS.as_ptr() as *mut _) }
        {
            // If we walked up, `current` is a parent we need to return.
            // If it's the original entry, the caller already owns it, but
            // we return it as-is and let the caller handle it.
            // To simplify ownership: if current != entry, we transfer
            // ownership to the caller. If current == entry, caller already
            // owns it, but we return it and the caller should NOT release
            // it again. We handle this by returning None when current ==
            // entry (meaning the entry itself is a USB device, not a disk
            // — unusual for IOMedia but possible).
            if current == entry {
                return None;
            }
            return Some(current);
        }

        // Walk to parent
        let mut parent: io_registry_entry_t = 0;
        let kr = unsafe {
            IORegistryEntryGetParentEntry(current, kIOServicePlane.as_ptr() as *mut _, &mut parent)
        };
        if kr != kIOReturnSuccess || parent == 0 {
            break;
        }
        // Release intermediate parents (but not the original entry).
        if current != entry {
            let _ = IOObjectRelease(current);
        }
        current = parent;
    }

    // Exhausted depth without finding a USB ancestor.
    if current != entry {
        let _ = IOObjectRelease(current);
    }
    None
}

// ──────────────────────────────────────────────────────────────────────
//  Approach A: Registry properties
// ──────────────────────────────────────────────────────────────────────

/// Extract USB metadata from the IOKit registry (Approach A).
///
/// Walks up to find a USB device ancestor, then reads `idVendor`,
/// `idProduct`, `USB Serial Number`, `USB Vendor Name`, and
/// `USB Product Name` from the registry.
fn registry_usb_info(entry: io_registry_entry_t) -> Option<UsbInfo> {
    let usb_dev = find_usb_ancestor(entry)?;

    let info = UsbInfo {
        vendor_id: read_number_u16(usb_dev, ID_VENDOR_KEY),
        product_id: read_number_u16(usb_dev, ID_PRODUCT_KEY),
        manufacturer: read_string(usb_dev, USB_VENDOR_NAME_KEY),
        product: read_string(usb_dev, USB_PRODUCT_NAME_KEY),
        serial: read_string(usb_dev, USB_SERIAL_NUMBER_KEY),
    };

    let _ = IOObjectRelease(usb_dev);

    // Only return if we found something useful.
    if info.vendor_id.is_some()
        || info.product_id.is_some()
        || info.manufacturer.is_some()
        || info.serial.is_some()
    {
        Some(info)
    } else {
        None
    }
}

// ──────────────────────────────────────────────────────────────────────
//  Approach B: IOUSBDeviceInterface descriptor extraction
// ──────────────────────────────────────────────────────────────────────

/// Extract USB metadata via `IOUSBDeviceInterface` control transfers
/// (Approach B — the nachtimwald article's method, in Rust).
///
/// This is a fallback for when registry properties are incomplete.
/// It obtains the `IOUSBDeviceInterface` COM vtable, reads string
/// descriptor indices, and issues control transfers to get the actual
/// UTF-16LE string data, converting to UTF-8.
///
/// **No `USBDeviceOpen` is needed** — string descriptors are readable
/// without claiming the device, even when a kernel driver has attached.
///
/// # Implementation notes
///
/// Uses `IOCreatePlugInInterfaceForService` to create a plugin, then
/// `QueryInterface` to obtain the `IOUSBDeviceInterface` vtable.  The
/// vtable's `USBGetManufacturerStringIndex`, `USBGetProductStringIndex`,
/// `USBGetSerialNumberStringIndex`, and `DeviceRequest` fields are called
/// through the COM-style `(*ptr)->method` pattern.
///
/// The UUID constants (`kIOUSBDeviceUserClientTypeID`,
/// `kIOCFPlugInInterfaceID`, `kIOUSBDeviceInterfaceID`) are not exported
/// by `objc2-io-kit` (skipped in translation-config.toml), so we define
/// them manually from the byte values in the system headers
/// (`IOUSBLib.h`, `IOCFPlugIn.h`) using `CFUUID::constant_uuid_with_bytes`.
fn descriptor_usb_info(entry: io_registry_entry_t) -> Option<UsbInfo> {
    // 1. Find the IOUSBDevice/IOUSBHostDevice ancestor.
    let usb_service = find_usb_ancestor(entry)?;

    // 2. Create CFUUID constants for the COM interface types.
    //    kIOUSBDeviceUserClientTypeID — from IOUSBLib.h
    let plugin_type = CFUUID::constant_uuid_with_bytes(
        None, 0x9d, 0xc7, 0xb7, 0x80, 0x9e, 0xc0, 0x11, 0xD4, 0xa5, 0x4f, 0x00, 0x0a, 0x27, 0x05,
        0x28, 0x61,
    )?;
    //    kIOCFPlugInInterfaceID — from IOCFPlugIn.h
    let interface_type = CFUUID::constant_uuid_with_bytes(
        None, 0xC2, 0x44, 0xE8, 0x58, 0x10, 0x9C, 0x11, 0xD4, 0x91, 0xD4, 0x00, 0x50, 0xE4, 0xC6,
        0x42, 0x6F,
    )?;

    // 3. Create the plugin interface.
    let mut plug_in: *mut *mut IOCFPlugInInterface = std::ptr::null_mut();
    let mut score: i32 = 0;
    let kr = unsafe {
        IOCreatePlugInInterfaceForService(
            usb_service,
            Some(&plugin_type),
            Some(&interface_type),
            &mut plug_in,
            &mut score,
        )
    };
    let _ = IOObjectRelease(usb_service);

    if kr != kIOReturnSuccess || plug_in.is_null() {
        return None;
    }

    // 4. QueryInterface for IOUSBDeviceInterface.
    //    kIOUSBDeviceInterfaceID (= kIOUSBDeviceInterfaceID100) — from IOUSBLib.h
    let device_iid = CFUUID::constant_uuid_with_bytes(
        None, 0x5c, 0x81, 0x87, 0xd0, 0x9e, 0xf3, 0x11, 0xD4, 0x8b, 0x45, 0x00, 0x0a, 0x27, 0x05,
        0x28, 0x61,
    )?;
    let device_iid_bytes: CFUUIDBytes = device_iid.uuid_bytes();

    let mut dev: *mut c_void = std::ptr::null_mut();
    let plug_vtable = unsafe { &**plug_in };
    let query = plug_vtable.QueryInterface.expect("QueryInterface");
    let hr = unsafe { query(plug_in as *mut c_void, device_iid_bytes, &mut dev) };

    // Release the plugin — we have the device interface now.
    let _ = unsafe { IODestroyPlugInInterface(plug_in) };

    // HRESULT S_OK == 0
    if hr != 0 || dev.is_null() {
        return None;
    }

    let dev_interface = dev as *mut *mut IOUSBDeviceInterface;
    let dev_vtable = unsafe { &**dev_interface };
    let self_ptr = dev_interface as *mut c_void;

    // 5. Get vendor/product IDs (no device open needed).
    let mut vendor_id: u16 = 0;
    let mut product_id: u16 = 0;
    if let Some(f) = dev_vtable.GetDeviceVendor {
        let _ = unsafe { f(self_ptr, &mut vendor_id) };
    }
    if let Some(f) = dev_vtable.GetDeviceProduct {
        let _ = unsafe { f(self_ptr, &mut product_id) };
    }

    // 6. Get string descriptors via control transfers.
    let manufacturer = get_usb_string(
        dev_vtable,
        self_ptr,
        dev_vtable.USBGetManufacturerStringIndex,
    );
    let product = get_usb_string(dev_vtable, self_ptr, dev_vtable.USBGetProductStringIndex);
    let serial = get_usb_string(
        dev_vtable,
        self_ptr,
        dev_vtable.USBGetSerialNumberStringIndex,
    );

    // 7. Release the device interface.
    if let Some(release) = dev_vtable.Release {
        let _ = unsafe { release(self_ptr) };
    }

    Some(UsbInfo {
        vendor_id: if vendor_id != 0 {
            Some(vendor_id)
        } else {
            None
        },
        product_id: if product_id != 0 {
            Some(product_id)
        } else {
            None
        },
        manufacturer,
        product,
        serial,
    })
}

/// Look up a USB string descriptor by its string index.
///
/// Calls `get_index_fn` to obtain the string index, then issues a
/// `DeviceRequest` control transfer to read the actual string data.
fn get_usb_string(
    vtable: &IOUSBDeviceInterface,
    self_ptr: *mut c_void,
    get_index_fn: Option<unsafe extern "C-unwind" fn(*mut c_void, *mut u8) -> IOReturn>,
) -> Option<String> {
    let get_index = get_index_fn?;
    let mut idx: u8 = 0;
    let ret = unsafe { get_index(self_ptr, &mut idx) };
    if ret != kIOReturnSuccess || idx == 0 {
        return None;
    }
    get_string_descriptor(vtable, self_ptr, idx)
}

/// Issue a USB control transfer to read a string descriptor.
///
/// The descriptor layout is:
/// - byte 0: total length (including header)
/// - byte 1: descriptor type (0x03 = string)
/// - bytes 2+: UTF-16LE encoded string data
///
/// We convert the UTF-16LE data to a Rust `String` and trim whitespace.
fn get_string_descriptor(
    vtable: &IOUSBDeviceInterface,
    self_ptr: *mut c_void,
    idx: u8,
) -> Option<String> {
    let device_request = vtable.DeviceRequest?;

    let mut buffer = [0u8; 4086];
    let mut request = IOUSBDevRequest {
        bmRequestType: 0x80,              // kUSBIn | kUSBStandard | kUSBDevice
        bRequest: 0x06,                   // kUSBRqGetDescriptor
        wValue: (0x03 << 8) | idx as u16, // kUSBStringDesc << 8 | idx
        wIndex: 0x0409,                   // Language ID (English US)
        wLength: buffer.len() as u16,
        pData: buffer.as_mut_ptr() as *mut c_void,
        wLenDone: 0,
    };

    let ret = unsafe { device_request(self_ptr, &mut request) };
    if ret != kIOReturnSuccess || request.wLenDone <= 2 {
        return None;
    }

    // Parse: skip first 2 bytes (length + type), rest is UTF-16LE.
    let data_len = (request.wLenDone - 2) as usize;
    let utf16: Vec<u16> = (0..data_len / 2)
        .map(|i| u16::from_le_bytes([buffer[2 + i * 2], buffer[2 + i * 2 + 1]]))
        .collect();

    let s = String::from_utf16_lossy(&utf16);
    let trimmed = s.trim();
    if trimmed.is_empty() {
        None
    } else {
        Some(trimmed.to_string())
    }
}

// ──────────────────────────────────────────────────────────────────────
//  Combined entry point
// ──────────────────────────────────────────────────────────────────────

/// Get USB metadata for a device, combining both approaches.
///
/// Tries registry properties first (fast, no device access).  Falls back
/// to `IOUSBDeviceInterface` descriptor extraction if the registry data is
/// incomplete.
pub fn get_usb_info(entry: io_registry_entry_t) -> Option<UsbInfo> {
    // Approach A: registry properties.
    let mut info = registry_usb_info(entry);

    // Approach B: descriptor extraction (fallback).
    // Only attempt if registry data is missing key fields.
    let needs_fallback = info.as_ref().map_or(true, |i| {
        i.manufacturer.is_none() || i.product.is_none() || i.serial.is_none()
    });

    if needs_fallback {
        if let Some(desc) = descriptor_usb_info(entry) {
            // Merge: registry takes priority, fill gaps from descriptors.
            match &mut info {
                Some(existing) => {
                    if existing.manufacturer.is_none() {
                        existing.manufacturer = desc.manufacturer;
                    }
                    if existing.product.is_none() {
                        existing.product = desc.product;
                    }
                    if existing.serial.is_none() {
                        existing.serial = desc.serial;
                    }
                    if existing.vendor_id.is_none() {
                        existing.vendor_id = desc.vendor_id;
                    }
                    if existing.product_id.is_none() {
                        existing.product_id = desc.product_id;
                    }
                }
                None => info = Some(desc),
            }
        }
    }

    info
}

// ──────────────────────────────────────────────────────────────────────
//  Tests
// ──────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn usb_class_constants() {
        assert_eq!(IOUSB_DEVICE_CLASS.to_str().unwrap(), "IOUSBDevice");
        assert_eq!(IOUSB_HOST_DEVICE_CLASS.to_str().unwrap(), "IOUSBHostDevice");
    }

    #[test]
    fn composite_id_formats_correctly() {
        let info = UsbInfo {
            vendor_id: Some(0x0781),
            product_id: Some(0x5588),
            manufacturer: Some("SanDisk".into()),
            product: Some("Cruzer Blade".into()),
            serial: Some("ABC123".into()),
        };
        assert_eq!(info.composite_id().unwrap(), "0781:5588:ABC123");
    }

    #[test]
    fn composite_id_without_serial() {
        let info = UsbInfo {
            vendor_id: Some(0x0781),
            product_id: Some(0x5588),
            manufacturer: None,
            product: None,
            serial: None,
        };
        assert_eq!(info.composite_id().unwrap(), "0781:5588:");
    }

    #[test]
    fn composite_id_none_without_vendor() {
        let info = UsbInfo {
            vendor_id: None,
            product_id: Some(0x5588),
            manufacturer: None,
            product: None,
            serial: None,
        };
        assert!(info.composite_id().is_none());
    }
}
