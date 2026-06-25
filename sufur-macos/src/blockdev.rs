//! File-backed [`BlockDevice`] implementation for macOS.
//!
//! Wraps a raw `/dev/rdiskN` file handle opened by [`crate::MacosPlatform::open_device`].
//! The platform layer handles unmounting via `diskutil unmountDisk` before
//! opening the raw device — see `SUFUR_ARCHITECTURE.md` § "macOS: TCC and
//! Full Disk Access".

use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};

use sufur_platform::{BlockDevice, Error};

pub struct MacosBlockDevice {
    file: File,
    size: u64,
}

impl MacosBlockDevice {
    pub fn new(file: File, size: u64) -> Self {
        Self { file, size }
    }
}

impl BlockDevice for MacosBlockDevice {
    fn read(&mut self, buf: &mut [u8], offset: u64) -> Result<usize, Error> {
        self.file.seek(SeekFrom::Start(offset))?;
        let mut total = 0;
        while total < buf.len() {
            let n = self.file.read(&mut buf[total..])?;
            if n == 0 {
                break;
            }
            total += n;
        }
        Ok(total)
    }

    fn write(&mut self, buf: &[u8], offset: u64) -> Result<usize, Error> {
        self.file.seek(SeekFrom::Start(offset))?;
        self.file.write_all(buf)?;
        Ok(buf.len())
    }

    fn size(&self) -> u64 {
        self.size
    }

    fn sync(&mut self) -> Result<(), Error> {
        self.file.sync_all()?;
        Ok(())
    }
}
