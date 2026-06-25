//! Disk Arbitration framework integration for unmounting disks.
//!
//! Replaces the `diskutil unmountDisk` shell-out with direct
//! `DADisk::unmount` calls using `objc2-disk-arbitration`.
//!
//! Equivalent to:
//!   `diskutil unmountDisk /dev/diskN`
//!
//! Uses `kDADiskUnmountOptionWhole` to unmount all volumes on the disk.

use std::ffi::{c_void, CString};
use std::ptr::NonNull;
use std::sync::mpsc;

use dispatch2::{DispatchQueue, DispatchQueueAttr};
use objc2_disk_arbitration::{
    kDADiskUnmountOptionWhole, kDAReturnNotMounted, DADisk, DADissenter, DAReturn, DASession,
};

use sufur_platform::{Error, ErrorCode};

unsafe extern "C-unwind" fn unmount_callback(
    _disk: NonNull<DADisk>,
    dissenter: *const DADissenter,
    context: *mut c_void,
) {
    let tx = &*(context as *const mpsc::Sender<Result<(), DAReturn>>);

    let result = if dissenter.is_null() {
        Ok(())
    } else {
        let status = (&*dissenter).status();
        Err(status)
    };

    let _ = tx.send(result);
}

/// Unmount all volumes on the disk identified by `bsd_name` (e.g. `"disk4"`).
///
/// Blocks until the DiskArbitration framework reports completion via callback.
/// Tolerates `kDAReturnNotMounted` as success (the disk wasn't mounted to
/// begin with).
pub fn unmount_disk(bsd_name: &str) -> Result<(), Error> {
    let c_name = CString::new(bsd_name)
        .map_err(|_| Error::platform(ErrorCode::Internal, "invalid BSD name"))?;

    let session = unsafe { DASession::new(None) }
        .ok_or_else(|| Error::platform(ErrorCode::Internal, "DASessionCreate returned null"))?;

    let queue = DispatchQueue::new("com.sufur.diskarb", DispatchQueueAttr::SERIAL);
    unsafe { session.set_dispatch_queue(Some(&*queue)) };

    let name_ptr = NonNull::new(c_name.as_ptr() as *mut core::ffi::c_char)
        .ok_or_else(|| Error::platform(ErrorCode::Internal, "empty BSD name"))?;

    let disk = unsafe { DADisk::from_bsd_name(None, &session, name_ptr) }.ok_or_else(|| {
        Error::platform(
            ErrorCode::DeviceNotFound,
            format!("DADiskCreateFromBSDName returned null for {bsd_name}"),
        )
    })?;

    let (tx, rx) = mpsc::channel::<Result<(), DAReturn>>();

    unsafe {
        disk.unmount(
            kDADiskUnmountOptionWhole,
            Some(unmount_callback),
            &tx as *const _ as *mut c_void,
        );
    }

    let result = rx.recv().map_err(|_| {
        Error::platform(
            ErrorCode::Internal,
            "DiskArbitration unmount callback never fired",
        )
    })?;

    match result {
        Ok(()) => Ok(()),
        Err(status) if status == kDAReturnNotMounted => Ok(()),
        Err(status) => Err(Error::platform(
            ErrorCode::DeviceBusy,
            format!("DADiskUnmount failed with status 0x{status:08x}"),
        )),
    }
}
