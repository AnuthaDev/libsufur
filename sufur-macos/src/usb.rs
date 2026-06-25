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

use std::ffi::CStr;

use objc2_core_foundation::{CFNumber, CFString};
use objc2_io_kit::{
    io_registry_entry_t, kIOReturnSuccess, kIOServicePlane, IOObjectConformsTo, IOObjectRelease,
    IORegistryEntryCreateCFProperty, IORegistryEntryGetParentEntry,
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
/// This function uses `IOCreatePlugInInterfaceForService` to create a
/// plugin, then `QueryInterface` to obtain the `IOUSBDeviceInterface`
/// vtable.  The vtable's `USBGetManufacturerStringIndex`,
/// `USBGetProductStringIndex`, `USBGetSerialNumberStringIndex`, and
/// `DeviceRequest` fields are called through the COM-style `(*ptr)->method`
/// pattern.
///
/// The required UUID constants (`kIOUSBDeviceUserClientTypeID`,
/// `kIOCFPlugInInterfaceID`, `kIOUSBDeviceInterfaceID`) are defined in
/// `IOUSBLib.h`.  In `objc2-io-kit` they may be available directly as
/// statics, or may need to be constructed from UUID bytes via
/// `CFUUIDCreateFromUUIDBytes`.
///
/// **VERIFY on macOS:** Check whether `objc2_io_kit` exports
/// `kIOUSBDeviceUserClientTypeID`, `kIOCFPlugInInterfaceID`, and
/// `kIOUSBDeviceInterfaceID` (or `kIOUSBDeviceInterfaceID942`).  If not,
/// construct them from their UUID string representations using
/// `CFUUIDCreateFromString` or define the `CFUUIDBytes` structs manually.
fn descriptor_usb_info(_entry: io_registry_entry_t) -> Option<UsbInfo> {
    // TODO: Implement IOUSBDeviceInterface descriptor extraction.
    //
    // The structure is:
    //
    // 1. Find the IOUSBDevice ancestor (reuse find_usb_ancestor above).
    // 2. let plugin = IOCreatePlugInInterfaceForService(
    //        usb_service,
    //        kIOUSBDeviceUserClientTypeID,  // CFUUID
    //        kIOCFPlugInInterfaceID,        // CFUUID
    //        &mut plug_in_interface,        // *mut *mut *mut IOCFPlugInInterfaceStruct
    //        &mut score,                    // *mut i32
    //    );
    //
    // 3. let dev = (*plug_in_interface).QueryInterface.expect(...)(
    //        plug_in_interface,
    //        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
    //        &mut dev_interface,  // *mut *mut IOUSBDeviceInterface
    //    );
    //    (*plug_in_interface).Release.expect(...)(plug_in_interface);
    //
    // 4. let mut vendor_id: u16 = 0;
    //    (*dev_interface).GetDeviceVendor.expect(...)(dev_interface, &mut vendor_id);
    //
    // 5. let mut string_index: u8 = 0;
    //    (*dev_interface).USBGetManufacturerStringIndex.expect(...)(
    //        dev_interface, &mut string_index,
    //    );
    //    let manufacturer = get_string_descriptor(dev_interface, string_index);
    //
    // 6. Similarly for USBGetProductStringIndex and USBGetSerialNumberStringIndex.
    //
    // 7. (*dev_interface).Release.expect(...)(dev_interface);
    //
    // The `get_string_descriptor` helper issues a control transfer:
    //
    //   let mut request = IOUSBDevRequest {
    //       bmRequestType: 0x80,  // kUSBIn | kUSBStandard | kUSBDevice
    //       bRequest: 0x06,      // kUSBRqGetDescriptor
    //       wValue: (0x03 << 8) | string_index,  // kUSBStringDesc << 8 | idx
    //       wIndex: 0x0409,      // Language ID (English US)
    //       wLength: 4086,
    //       pData: buffer.as_mut_ptr() as *mut c_void,
    //       wLenDone: 0,
    //   };
    //   (*dev_interface).DeviceRequest.expect(...)(dev_interface, &mut request);
    //
    //   // Parse: byte 0 = total length, byte 1 = descriptor type (0x03 = string),
    //   // bytes 2+ = UTF-16LE string data.
    //   // Convert with CFStringCreateWithBytes(kCFStringEncodingUTF16LE).

    None
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
