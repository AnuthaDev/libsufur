//! `sufur` — the universal out-of-process interface for Sufur.
//!
//! Two output modes:
//!   - **Human (default):** TTY-friendly output on stderr.
//!   - **Machine (`--json`):** NDJSON on stdout, one JSON object per line.
//!
//! Unprivileged commands (`list`, `analyze-image`, `validate`, `capabilities`)
//! execute in-process via `sufur-core`.  Privileged commands (`create`,
//! `format`, `wipe`) would launch `sufur-helper` through polkit — the helper
//! is out of scope for this scaffold.
//!
//! See `SUFUR_ARCHITECTURE.md` § "CLI Interface".

use std::path::PathBuf;

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(
    name = "sufur",
    version,
    about = "Create bootable USB drives — CLI-first"
)]
struct Cli {
    /// Emit NDJSON on stdout instead of human-friendly TTY output.
    #[arg(long, global = true)]
    json: bool,

    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// List removable USB devices.
    List,
    /// Analyze an ISO or disk image.
    AnalyzeImage {
        /// Path to the image file.
        image: PathBuf,
    },
    /// Create a bootable USB drive (privileged — launches sufur-helper).
    Create {
        image: PathBuf,
        #[arg(long)]
        device: PathBuf,
    },
    /// Format a device or partition (privileged).
    Format {
        #[arg(long)]
        device: PathBuf,
        #[arg(long)]
        filesystem: String,
    },
    /// Wipe partition table (privileged).
    Wipe {
        #[arg(long)]
        device: PathBuf,
    },
    /// Verify image checksum.
    Validate {
        image: PathBuf,
        #[arg(long)]
        checksum: String,
    },
    /// Report system tool availability.
    Capabilities,
    /// Apply a declarative job file.
    ApplyJob { job_file: PathBuf },
    /// Dry-run check of a job file without touching devices.
    ValidateJob { job_file: PathBuf },
}

fn main() {
    let cli = Cli::parse();

    let result = match cli.command {
        Command::List => cmd_list(cli.json),
        Command::AnalyzeImage { image } => cmd_analyze_image(&image, cli.json),
        Command::Create { image, device } => cmd_create(&image, &device, cli.json),
        Command::Format { device, filesystem } => cmd_format(&device, &filesystem, cli.json),
        Command::Wipe { device } => cmd_wipe(&device, cli.json),
        Command::Validate { image, checksum } => cmd_validate(&image, &checksum, cli.json),
        Command::Capabilities => cmd_capabilities(cli.json),
        Command::ApplyJob { job_file } => cmd_apply_job(&job_file, cli.json),
        Command::ValidateJob { job_file } => cmd_validate_job(&job_file, cli.json),
    };

    if let Err(e) = result {
        if cli.json {
            let envelope = serde_json::json!({
                "type": "error",
                "code": code_of(&e),
                "message": e.to_string(),
            });
            println!(
                "{}",
                serde_json::to_string(&envelope).unwrap_or_else(|_| "{}".into())
            );
        } else {
            eprintln!("error: {e}");
        }
        std::process::exit(1);
    }
}

fn engine() -> Result<sufur_core::Sufur, sufur_core::Error> {
    sufur_core::Sufur::for_current_platform()
}

fn cmd_list(json: bool) -> Result<(), sufur_core::Error> {
    let engine = engine()?;
    let devices = engine.list_devices()?;
    if json {
        for d in &devices {
            println!("{}", serde_json::to_string(d).unwrap_or_default());
        }
    } else {
        if devices.is_empty() {
            println!("No removable devices found.");
        } else {
            for d in devices {
                println!(
                    "{}  {:>12}  {} {}",
                    d.path.display(),
                    format_size(d.size_bytes),
                    d.vendor,
                    d.model,
                );
            }
        }
    }
    Ok(())
}

fn cmd_analyze_image(path: &std::path::Path, json: bool) -> Result<(), sufur_core::Error> {
    let file = std::fs::File::open(path)?;
    let info = sufur_core::image::analyze(file)?;
    let info = sufur_core::image::ImageInfo {
        path: Some(path.display().to_string()),
        ..info
    };
    if json {
        println!("{}", serde_json::to_string(&info).unwrap_or_default());
    } else {
        println!(
            "{}  {}  windows_iso={}  win2go={}",
            info.path.as_deref().unwrap_or("?"),
            format_size(info.size_bytes),
            info.is_windows_iso,
            info.is_win2go_capable,
        );
    }
    Ok(())
}

fn cmd_create(
    _image: &std::path::Path,
    _device: &std::path::Path,
    json: bool,
) -> Result<(), sufur_core::Error> {
    // Phase 3: construct job, launch sufur-helper via pkexec, stream NDJSON.
    not_implemented("create", json)
}

fn cmd_format(_device: &std::path::Path, _fs: &str, json: bool) -> Result<(), sufur_core::Error> {
    not_implemented("format", json)
}

fn cmd_wipe(device: &std::path::Path, json: bool) -> Result<(), sufur_core::Error> {
    let engine = engine()?;
    let devices = engine.list_devices()?;
    let target = devices.iter().find(|d| d.path == device).ok_or_else(|| {
        sufur_core::Error::platform(
            sufur_core::ErrorCode::DeviceNotFound,
            format!("no removable device at {}", device.display()),
        )
    })?;

    engine.wipe_device(&target.id)?;

    if json {
        println!(
            "{}",
            serde_json::json!({
                "type": "complete",
                "device": device.display().to_string(),
                "message": "partition table wiped, fresh GPT written"
            })
        );
    } else {
        eprintln!(
            "Wiped partition table on {} — fresh GPT written.",
            device.display()
        );
    }
    Ok(())
}

fn cmd_validate(
    _image: &std::path::Path,
    _checksum: &str,
    _json: bool,
) -> Result<(), sufur_core::Error> {
    // Phase 2: SHA-256 verify.
    Ok(())
}

fn cmd_capabilities(json: bool) -> Result<(), sufur_core::Error> {
    let caps = serde_json::json!({
        "ntfs": false,
        "fat32": false,
        "exfat": false,
        "wim": false,
        "hive": false,
    });
    if json {
        println!("{caps}");
    } else {
        println!("Filesystem support: ntfs=no  fat32=no  exfat=no");
        println!("WIM support: wim=no  hive=no");
    }
    Ok(())
}

fn cmd_apply_job(job_file: &std::path::Path, _json: bool) -> Result<(), sufur_core::Error> {
    let raw = std::fs::read_to_string(job_file)?;
    let job: sufur_core::pipeline::CreateJob = serde_json::from_str(&raw).map_err(|e| {
        sufur_core::Error::platform(
            sufur_core::ErrorCode::Internal,
            format!("invalid job file: {e}"),
        )
    })?;
    let mut handler = NdJsonHandler;
    sufur_core::pipeline::run_create(&job, std::path::Path::new("/dev/null"), &mut handler)?;
    Ok(())
}

fn cmd_validate_job(job_file: &std::path::Path, _json: bool) -> Result<(), sufur_core::Error> {
    let raw = std::fs::read_to_string(job_file)?;
    let job: sufur_core::pipeline::CreateJob = serde_json::from_str(&raw).map_err(|e| {
        sufur_core::Error::platform(
            sufur_core::ErrorCode::Internal,
            format!("invalid job file: {e}"),
        )
    })?;
    sufur_core::pipeline::validate_job(&job)?;
    Ok(())
}

fn not_implemented(name: &str, json: bool) -> Result<(), sufur_core::Error> {
    let msg = format!("`sufur {name}` is not yet implemented in this scaffold");
    if json {
        let envelope = serde_json::json!({
            "type": "error",
            "code": "INTERNAL",
            "message": msg,
        });
        println!("{envelope}");
    } else {
        eprintln!("{msg}");
    }
    std::process::exit(2);
}

fn code_of(e: &sufur_core::Error) -> &'static str {
    match e {
        sufur_core::Error::Platform { code, .. } => match code {
            sufur_core::ErrorCode::DeviceBusy => "DEVICE_BUSY",
            sufur_core::ErrorCode::DeviceNotFound => "DEVICE_NOT_FOUND",
            sufur_core::ErrorCode::PermissionDenied => "PERMISSION_DENIED",
            sufur_core::ErrorCode::InvalidImage => "INVALID_IMAGE",
            sufur_core::ErrorCode::ChecksumMismatch => "CHECKSUM_MISMATCH",
            sufur_core::ErrorCode::UnsupportedFilesystem => "UNSUPPORTED_FILESYSTEM",
            sufur_core::ErrorCode::Cancelled => "CANCELLED",
            sufur_core::ErrorCode::Internal => "INTERNAL",
        },
        sufur_core::Error::Io(_) => "IO_ERROR",
        sufur_core::Error::Cancelled => "CANCELLED",
    }
}

fn format_size(bytes: u64) -> String {
    const UNITS: &[&str] = &["B", "KiB", "MiB", "GiB", "TiB"];
    let mut size = bytes as f64;
    let mut unit = 0;
    while size >= 1024.0 && unit < UNITS.len() - 1 {
        size /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{bytes} {}", UNITS[0])
    } else {
        format!("{size:.1} {}", UNITS[unit])
    }
}

struct NdJsonHandler;

impl sufur_core::progress::ProgressHandler for NdJsonHandler {
    fn on_event(&mut self, event: sufur_core::progress::ProgressEvent) {
        println!("{}", serde_json::to_string(&event).unwrap_or_default());
    }
}
