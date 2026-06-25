//! File-backed [`BlockDevice`] implementation for macOS.
//!
//! Wraps a raw `/dev/rdiskN` file handle opened by [`crate::MacosPlatform::open_device`].
//! The platform layer handles unmounting via DiskArbitration before opening the
//! raw device — see `SUFUR_ARCHITECTURE.md` § "macOS: TCC and Full Disk Access".
//!
//! # Raw device alignment
//!
//! `/dev/rdiskN` is a raw character device that bypasses the unified buffer
//! cache.  The kernel storage driver rejects any `read`/`write` whose **offset**
//! is not a multiple of the device's logical block size, or whose **length** is
//! not a multiple of the block size, returning `EINVAL`.
//!
//! gptman issues unaligned writes (e.g. the protective MBR at byte 446, length
//! 66).  To bridge this, [`MacosBlockDevice`] performs read-modify-write (RMW)
//! for any I/O that does not fall on block boundaries:
//!
//!   1. Read the full block(s) that overlap the requested range.
//!   2. Overwrite the affected bytes in the block buffer.
//!   3. Write the full block(s) back.
//!
//! This is the same strategy used by Raspberry Pi Imager and other macOS disk
//! imaging tools.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::unix::io::AsRawFd;

use sufur_platform::{BlockDevice, Error};

/// `DKIOCGETBLOCKSIZE` — query the logical block size of a disk device.
///
/// Defined in `<sys/disk.h>` as `_IOR('d', 24, uint32_t)`.
/// Verified from macOS system headers: `0x40046418`.
const DKIOCGETBLOCKSIZE: libc::c_ulong = 0x40046418;

/// `F_NOCACHE` — disable the unified buffer cache for this fd.
///
/// Equivalent to `O_DIRECT` on Linux.  Defined in `<sys/fcntl.h>` as `48`.
const F_NOCACHE: libc::c_int = 48;

pub struct MacosBlockDevice {
    file: File,
    size: u64,
    block_size: u64,
}

impl MacosBlockDevice {
    pub fn new(file: File, size: u64) -> Self {
        let block_size = query_block_size(file.as_raw_fd()).unwrap_or(512);

        // Bypass the unified buffer cache — equivalent to O_DIRECT on Linux.
        // Best-effort: ignore failure (some devices may not support it).
        unsafe {
            let _ = libc::fcntl(file.as_raw_fd(), F_NOCACHE, 1);
        }

        Self {
            file,
            size,
            block_size,
        }
    }
}

/// Query the logical block size of a raw disk device via `DKIOCGETBLOCKSIZE`.
fn query_block_size(fd: libc::c_int) -> Option<u64> {
    let mut block_size: libc::c_uint = 0;
    let rc = unsafe {
        libc::ioctl(fd, DKIOCGETBLOCKSIZE, &mut block_size as *mut libc::c_uint)
    };
    if rc < 0 || block_size == 0 {
        None
    } else {
        Some(block_size as u64)
    }
}

impl BlockDevice for MacosBlockDevice {
    fn read(&mut self, buf: &mut [u8], offset: u64) -> Result<usize, Error> {
        let bs = self.block_size;

        // Fast path: aligned read.
        if offset % bs == 0 && buf.len() as u64 % bs == 0 {
            self.file.seek(SeekFrom::Start(offset))?;
            let mut total = 0;
            while total < buf.len() {
                let n = self.file.read(&mut buf[total..])?;
                if n == 0 {
                    break;
                }
                total += n;
            }
            return Ok(total);
        }

        // Slow path: read full blocks, extract relevant bytes.
        let start_block = offset / bs;
        let end_byte = offset + buf.len() as u64;
        let end_block = end_byte.div_ceil(bs);

        let mut block_buf = vec![0u8; bs as usize];
        let mut total = 0;

        for block_idx in start_block..end_block {
            let block_start = block_idx * bs;
            let data_start = block_start.max(offset);
            let data_end = (block_start + bs).min(end_byte);
            let data_len = (data_end - data_start) as usize;
            let off_in_block = (data_start - block_start) as usize;
            let buf_off = (data_start - offset) as usize;

            // Read the full block (zero-fill if read falls short at end of device).
            block_buf.fill(0);
            self.file.seek(SeekFrom::Start(block_start))?;
            let mut got = 0;
            while got < block_buf.len() {
                let n = self.file.read(&mut block_buf[got..])?;
                if n == 0 {
                    break;
                }
                got += n;
            }

            let copy_len = data_len.min(got.saturating_sub(off_in_block));
            if copy_len == 0 {
                break;
            }
            buf[buf_off..buf_off + copy_len]
                .copy_from_slice(&block_buf[off_in_block..off_in_block + copy_len]);
            total += copy_len;
        }

        Ok(total)
    }

    fn write(&mut self, buf: &[u8], offset: u64) -> Result<usize, Error> {
        let bs = self.block_size;

        // Fast path: aligned write.
        if offset % bs == 0 && buf.len() as u64 % bs == 0 {
            self.file.seek(SeekFrom::Start(offset))?;
            self.file.write_all(buf)?;
            return Ok(buf.len());
        }

        // Slow path: read-modify-write each affected block.
        let end_byte = offset + buf.len() as u64;
        let start_block = offset / bs;
        let end_block = end_byte.div_ceil(bs);

        let mut block_buf = vec![0u8; bs as usize];

        for block_idx in start_block..end_block {
            let block_start = block_idx * bs;
            let data_start = block_start.max(offset);
            let data_end = (block_start + bs).min(end_byte);
            let data_len = (data_end - data_start) as usize;
            let off_in_block = (data_start - block_start) as usize;
            let buf_off = (data_start - offset) as usize;

            // Read existing block only when we're not overwriting it entirely.
            // Zero-fill if the read falls short (e.g. past end of device).
            if data_len < bs as usize {
                block_buf.fill(0);
                self.file.seek(SeekFrom::Start(block_start))?;
                let mut got = 0;
                while got < block_buf.len() {
                    let n = self.file.read(&mut block_buf[got..])?;
                    if n == 0 {
                        break;
                    }
                    got += n;
                }
            }

            // Merge caller's data into the block buffer.
            block_buf[off_in_block..off_in_block + data_len]
                .copy_from_slice(&buf[buf_off..buf_off + data_len]);

            // Write the full block back.
            self.file.seek(SeekFrom::Start(block_start))?;
            self.file.write_all(&block_buf)?;
        }

        Ok(buf.len())
    }

    fn size(&self) -> u64 {
        self.size
    }

    fn sync(&mut self) -> Result<(), Error> {
        match self.file.sync_all() {
            Ok(()) => Ok(()),
            // fsync(2) on a raw character device returns ENOTTY — the device
            // doesn't support it.  With F_NOCACHE set, writes bypass the
            // buffer cache, so there's nothing for fsync to flush; the device
            // write cache drains on close().  Treat as success.
            Err(e) if e.raw_os_error() == Some(libc::ENOTTY) => Ok(()),
            Err(e) => Err(e.into()),
        }
    }
}
