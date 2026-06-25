//! GPT partition table wipe and creation.
//!
//! Uses `gptman` to wipe the existing partition table on a block device and
//! write a fresh GPT (GUID Partition Table) with a protective MBR.  The
//! partition logic is platform-agnostic — it operates through the
//! [`BlockDevice`] trait via a [`Read`] + [`Write`] + [`Seek`] adapter.
//!
//! See `SUFUR_ARCHITECTURE.md` § "Create Pipeline" for the partition stage.

use std::io::{self, Read, Seek, SeekFrom, Write};

use gptman::GPT;

use crate::{BlockDevice, Error, ErrorCode};

/// Extract a human-readable detail string from a `gptman::Error`.
///
/// gptman's `Error::Io` Display is the unhelpful `"generic I/O error"` — it
/// discards the inner `io::Error` that carries the actual OS message (e.g.
/// `"Invalid argument (os error 22)"`).  This helper unwraps the inner error
/// so callers see the real cause.
fn gptman_detail(e: &gptman::Error) -> String {
    match e {
        gptman::Error::Io(io_err) => format!("{io_err}"),
        other => format!("{other}"),
    }
}

/// Adapts a [`BlockDevice`] (offset-based read/write) to `Read + Write + Seek`
/// for use with `gptman`, which requires standard `std::io` traits.
struct BlockDeviceIo<'a> {
    dev: &'a mut dyn BlockDevice,
    pos: u64,
}

impl<'a> BlockDeviceIo<'a> {
    fn new(dev: &'a mut dyn BlockDevice) -> Self {
        Self { dev, pos: 0 }
    }

    fn sync(&mut self) -> Result<(), Error> {
        self.dev.sync()
    }
}

impl Read for BlockDeviceIo<'_> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let n = self
            .dev
            .read(buf, self.pos)
            .map_err(|e| io::Error::other(e.to_string()))?;
        self.pos += n as u64;
        Ok(n)
    }
}

impl Write for BlockDeviceIo<'_> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let n = self
            .dev
            .write(buf, self.pos)
            .map_err(|e| io::Error::other(e.to_string()))?;
        self.pos += n as u64;
        Ok(n)
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

impl Seek for BlockDeviceIo<'_> {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        match pos {
            SeekFrom::Start(p) => {
                self.pos = p;
                Ok(p)
            }
            SeekFrom::End(offset) => {
                let size = self.dev.size();
                self.pos = if offset >= 0 {
                    size.saturating_add(offset as u64)
                } else {
                    size.saturating_sub((-offset) as u64)
                };
                Ok(self.pos)
            }
            SeekFrom::Current(offset) => {
                self.pos = if offset >= 0 {
                    self.pos.saturating_add(offset as u64)
                } else {
                    self.pos.saturating_sub((-offset) as u64)
                };
                Ok(self.pos)
            }
        }
    }
}

/// Default sector size for USB mass storage devices.
///
/// Virtually all USB flash drives and SD card readers present 512-byte logical
/// sectors, even when the physical media uses 4K or larger pages.  This is the
/// correct default for the GPT header layout.
const DEFAULT_SECTOR_SIZE: u64 = 512;

/// Wipe the existing partition table on `dev` and write a fresh empty GPT
/// with a protective MBR.
///
/// The disk GUID is randomly generated (UUID v4).  No partitions are created —
/// callers that need partitions should use [`create_gpt_with_partition`].
pub fn wipe_and_create_gpt(dev: &mut dyn BlockDevice) -> Result<(), Error> {
    let mut io = BlockDeviceIo::new(dev);
    let disk_guid = *uuid::Uuid::new_v4().as_bytes();

    let mut gpt = GPT::new_from(&mut io, DEFAULT_SECTOR_SIZE, disk_guid)
        .map_err(|e| Error::platform(ErrorCode::Internal, format!("GPT creation failed: {}", gptman_detail(&e))))?;

    GPT::write_protective_mbr_into(&mut io, DEFAULT_SECTOR_SIZE)
        .map_err(|e| Error::platform(ErrorCode::Internal, format!("protective MBR failed: {}", gptman_detail(&e))))?;

    gpt.write_into(&mut io)
        .map_err(|e| Error::platform(ErrorCode::Internal, format!("GPT write failed: {}", gptman_detail(&e))))?;

    io.sync()?;
    Ok(())
}

/// Wipe the existing partition table, create a fresh GPT, and add a single
/// partition spanning the entire usable area.
///
/// `partition_type_guid` is the GPT partition type GUID (e.g. Microsoft Basic
/// Data `ebd0a0a2-b9e5-4433-87c0-68b6b72699c7`).  `name` sets the partition
/// label.
pub fn create_gpt_with_partition(
    dev: &mut dyn BlockDevice,
    partition_type_guid: [u8; 16],
    name: &str,
) -> Result<(), Error> {
    let mut io = BlockDeviceIo::new(dev);
    let disk_guid = *uuid::Uuid::new_v4().as_bytes();

    let mut gpt = GPT::new_from(&mut io, DEFAULT_SECTOR_SIZE, disk_guid)
        .map_err(|e| Error::platform(ErrorCode::Internal, format!("GPT creation failed: {}", gptman_detail(&e))))?;

    GPT::write_protective_mbr_into(&mut io, DEFAULT_SECTOR_SIZE)
        .map_err(|e| Error::platform(ErrorCode::Internal, format!("protective MBR failed: {}", gptman_detail(&e))))?;

    gpt[1] = gptman::GPTPartitionEntry {
        partition_type_guid,
        unique_partition_guid: *uuid::Uuid::new_v4().as_bytes(),
        starting_lba: gpt.header.first_usable_lba,
        ending_lba: gpt.header.last_usable_lba,
        attribute_bits: 0,
        partition_name: name.into(),
    };

    gpt.write_into(&mut io)
        .map_err(|e| Error::platform(ErrorCode::Internal, format!("GPT write failed: {}", gptman_detail(&e))))?;

    io.sync()?;
    Ok(())
}

/// Microsoft Basic Data partition type GUID.
///
/// Used for FAT32, exFAT, and NTFS partitions on GPT disks.
pub const PARTITION_TYPE_MICROSOFT_BASIC_DATA: [u8; 16] = [
    0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44, 0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7,
];

/// EFI System Partition type GUID.
pub const PARTITION_TYPE_EFI_SYSTEM: [u8; 16] = [
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b,
];

#[cfg(test)]
mod tests {
    use super::*;

    struct MockBlockDevice {
        data: Vec<u8>,
    }

    impl MockBlockDevice {
        fn new(size: u64) -> Self {
            Self {
                data: vec![0u8; size as usize],
            }
        }
    }

    impl BlockDevice for MockBlockDevice {
        fn read(&mut self, buf: &mut [u8], offset: u64) -> Result<usize, Error> {
            let start = offset as usize;
            if start >= self.data.len() {
                return Ok(0);
            }
            let end = (start + buf.len()).min(self.data.len());
            let n = end - start;
            buf[..n].copy_from_slice(&self.data[start..end]);
            Ok(n)
        }

        fn write(&mut self, buf: &[u8], offset: u64) -> Result<usize, Error> {
            let start = offset as usize;
            let end = start + buf.len();
            if end > self.data.len() {
                self.data.resize(end, 0);
            }
            self.data[start..end].copy_from_slice(buf);
            Ok(buf.len())
        }

        fn size(&self) -> u64 {
            self.data.len() as u64
        }

        fn sync(&mut self) -> Result<(), Error> {
            Ok(())
        }
    }

    #[test]
    fn wipe_creates_valid_gpt() {
        let mut dev = MockBlockDevice::new(32 * 1024 * 1024);
        wipe_and_create_gpt(&mut dev).expect("wipe should succeed");

        let mut io = BlockDeviceIo::new(&mut dev);
        let gpt = GPT::read_from(&mut io, DEFAULT_SECTOR_SIZE).expect("GPT should be readable");
        assert_eq!(gpt.sector_size, DEFAULT_SECTOR_SIZE);
        assert_eq!(gpt.header.signature, *b"EFI PART");
        assert!(gpt.header.primary_lba == 1);
    }

    #[test]
    fn wipe_writes_protective_mbr() {
        let mut dev = MockBlockDevice::new(32 * 1024 * 1024);
        wipe_and_create_gpt(&mut dev).expect("wipe should succeed");

        let mut io = BlockDeviceIo::new(&mut dev);
        let mut buf = [0u8; 512];
        io.read_exact(&mut buf).expect("read sector 0");

        assert_eq!(&buf[510..512], &[0x55, 0xAA]);
    }

    #[test]
    fn create_with_partition_adds_one_partition() {
        let mut dev = MockBlockDevice::new(64 * 1024 * 1024);
        create_gpt_with_partition(&mut dev, PARTITION_TYPE_MICROSOFT_BASIC_DATA, "USBSTORE")
            .expect("create should succeed");

        let mut io = BlockDeviceIo::new(&mut dev);
        let gpt = GPT::read_from(&mut io, DEFAULT_SECTOR_SIZE).expect("GPT should be readable");

        assert!(gpt[1].is_used());
        assert_eq!(
            gpt[1].partition_type_guid,
            PARTITION_TYPE_MICROSOFT_BASIC_DATA
        );
        assert_eq!(gpt[1].partition_name.as_str(), "USBSTORE");
        assert!(!gpt[2].is_used());
    }

    #[test]
    fn adapter_seek_end_uses_device_size() {
        let mut dev = MockBlockDevice::new(4096);
        let mut io = BlockDeviceIo::new(&mut dev);

        let pos = io.seek(SeekFrom::End(0)).expect("seek end");
        assert_eq!(pos, 4096);

        let pos = io.seek(SeekFrom::End(-512)).expect("seek end -512");
        assert_eq!(pos, 3584);
    }

    #[test]
    fn adapter_read_write_roundtrip() {
        let mut dev = MockBlockDevice::new(4096);
        let mut io = BlockDeviceIo::new(&mut dev);

        io.seek(SeekFrom::Start(100)).unwrap();
        io.write_all(b"hello").unwrap();

        io.seek(SeekFrom::Start(100)).unwrap();
        let mut buf = [0u8; 5];
        io.read_exact(&mut buf).unwrap();
        assert_eq!(&buf, b"hello");
    }
}
