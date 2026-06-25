# Sufur — High-Level Architecture

> **Sufur** is a clean-slate Rust port of Rufus for Linux (with macOS and Windows planned). It follows a CLI-first, library-core design: one source of truth in `sufur-core`, exposed via a CLI for scripts and agents, and consumed directly by native GUIs via Rust or `cxx`.

---

## Core Philosophy

- `sufur-core` is the single source of truth for all domain logic
- `sufur-core` is a reusable library that can execute inside different host processes. Analysis operations may execute inside a frontend process, while privileged write operations may execute inside a short-lived elevated helper process. The library itself is process-agnostic.
- All GUIs are thin clients — they render state and forward commands
- The CLI is the universal out-of-process interface (scripts, CI, web, agents)
- No `#ifdef` soup — platform differences live in separate crates behind a trait
- No hand-written C FFI — `cxx` generates type-safe bindings automatically

> Sufur follows a host-process execution model. `sufur-core` may execute inside an unprivileged frontend process for analysis and planning tasks, or inside a short-lived elevated helper process for destructive device operations. The same workflow engine is used in both cases. Privilege boundaries are implemented at the process level, not inside business logic.

---

## System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                          SUFUR ECOSYSTEM                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                   sufur/  (Rust workspace)                    │   │
│  │                                                              │   │
│  │   ┌─────────────┐   ┌──────────────┐   ┌─────────────┐      │   │
│  │   │ sufur-core  │   │sufur-platform│   │ sufur-linux │      │   │
│  │   │             │   │              │   │             │      │   │
│  │   │ • API types │◄──│ • traits     │◄──│ • udev      │      │   │
│  │   │ • pipeline  │   │ • Platform   │   │ • libfdisk  │      │   │
│  │   │ • progress  │   │ • BlockDevice│   │ • mount     │      │   │
│  │   │ • error     │   │ • Formatter  │   │             │      │   │
│  │   └──────┬──────┘   └──────────────┘   └─────────────┘      │   │
│  │          │                                                    │   │
│  │   ┌──────┴──────────────────────────────────────────────┐    │   │
│  │   │                                                       │    │   │
│  │   ▼           ▼           ▼            ▼                 │    │   │
│  │ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌────────┐           │    │   │
│  │ │sufur-cli│ │sufur-fs │ │sufur-wim│ │sufur-  │           │    │   │
│  │ │         │ │         │ │         │ │helper  │           │    │   │
│  │ │• cmd    │ │•ntfs-3g │ │•wimlib  │ │•pkexec │           │    │   │
│  │ │• JSON   │ │•dosfstls│ │•libhivex│ │•job    │           │    │   │
│  │ │• TTY    │ │•exfatpgs│ │•W2Go    │ │•stream │           │    │   │
│  │ └─────────┘ └─────────┘ └─────────┘ └────────┘           │    │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│          ┌───────────────────┼──────────────────┐                   │
│          │                   │                  │                   │
│          ▼                   ▼                  ▼                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │
│  │  sufur-qt/   │  │  sufur-gtk/  │  │  sufur-web/  │              │
│  │  (C++/Qt)    │  │  (Rust/GTK) │  │  (Browser)   │              │
│  │              │  │  sufur-tui/  │  │  sufur-tauri/│              │
│  │  Links core  │  │  (ratatui)   │  │  (Tauri app) │              │
│  │  via cxx     │  │  Link core   │  │  WASM + Tauri│              │
│  └──────────────┘  └──────────────┘  └──────────────┘              │
│                                                                      │
│  All GUI repos are separate from the core workspace.                │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Workspace Crate Map

| Crate | Type | Role |
|-------|------|------|
| `sufur-core` | lib | Domain logic, types, image analysis, create pipeline |
| `sufur-platform` | lib | Platform trait definitions (PAL) |
| `sufur-linux` | lib | Linux implementation: udev, libfdisk, mount |
| `sufur-macos` | lib | macOS implementation: IOKit, diskutil |
| `sufur-windows` | lib | Windows implementation: SetupAPI, VDS (optional) |
| `sufur-fs` | lib | Filesystem creation via statically linked C libs (ntfs-3g, dosfstools, exfatprogs) |
| `sufur-wim` | lib | WIM extraction and BCD hive editing via statically linked wimlib and libhivex; Windows To Go |
| `sufur-cli` | bin | CLI binary (`sufur`) with `--json` and TTY modes |
| `sufur-helper` | bin | Short-lived elevated process that hosts `sufur-core` for privileged operations |
| `sufur-wasm` | lib | `wasm-bindgen` exports for browser ISO analysis |

### Separate Repositories

| Repo | Stack | Integration |
|------|-------|-------------|
| `sufur-qt` | C++ / Qt6 / QML | Links `sufur-core` via `cxx` bridge |
| `sufur-gtk` | Rust / gtk-rs | Direct `sufur-core` crate dependency |
| `sufur-tui` | Rust / ratatui | Direct `sufur-core` crate dependency |
| `sufur-tauri` | Rust / Tauri + Web | Direct `sufur-core` crate dependency |
| `sufur-web` | TypeScript / React | WASM analysis via `sufur-wasm` |

---

## Vendored C Dependencies

Rather than copying C source code into the repository or requiring users to have system libraries installed, Sufur vendors upstream C libraries as **git submodules** and compiles them statically into the relevant crates via the `cc` crate in `build.rs`. This follows the same approach as Rufus. This means:

- `cargo build` is fully self-contained — no `apt install` or `brew install` required
- Upstream security fixes arrive via `git submodule update`
- The same build works identically on Linux and macOS
- Developers need a C toolchain (`cc`/`clang`) available, which is a safe assumption on both platforms

### config.h Strategy

These C libraries use Autotools, which generates a `config.h` encoding platform-specific feature detection (pthread availability, iconv location, header presence, etc.). The `cc` crate cannot run `./configure`.

**Solution (same as Rufus):** `config.h` files are generated manually by running `./configure` on a developer machine for each target platform, then **checked into git** under `vendor-config/`. They are not regenerated automatically during `cargo build`. This is a deliberate tradeoff: it ties the build to a specific generation environment, but keeps `cargo build` simple and reproducible for all users.

```
sufur/
├── vendor/
│   ├── ntfs-3g/               # git submodule → tuxera/ntfs-3g (pinned to release tag)
│   ├── dosfstools/             # git submodule → dosfstools/dosfstools (pinned)
│   ├── exfatprogs/             # git submodule → exfatprogs/exfatprogs (pinned)
│   ├── wimlib/                 # git submodule → ebiggers/wimlib (pinned)
│   └── libhivex/               # git submodule → libguestfs/libhivex (pinned)
│
├── sufur-fs/
│   └── vendor-config/
│       ├── ntfs3g-linux.h      # generated: cd vendor/ntfs-3g && ./configure && cp config.h ...
│       ├── ntfs3g-macos.h
│       ├── dosfstools-linux.h
│       ├── dosfstools-macos.h
│       ├── exfatprogs-linux.h
│       └── exfatprogs-macos.h
│
└── sufur-wim/
    └── vendor-config/
        ├── wimlib-linux.h      # generated: cd vendor/wimlib && ./configure && cp config.h ...
        ├── wimlib-macos.h
        ├── libhivex-linux.h
        └── libhivex-macos.h
```

The config files are generated on **Ubuntu 22.04 LTS** (Linux) and **macOS 14 Sonoma** (macOS). Distro packagers who need different configs for their target environment are expected to regenerate them — this is standard practice.

### What Each Submodule Provides

| Submodule | Used by | Replaces |
|-----------|---------|---------|
| `ntfs-3g` | `sufur-fs` | Copied `mkntfs` source / shelling out to `mkntfs` |
| `dosfstools` | `sufur-fs` | Copied `mkvfat` source / shelling out to `mkfs.fat` |
| `exfatprogs` | `sufur-fs` | Copied `mkexfat` source / shelling out to `mkfs.exfat` |
| `wimlib` | `sufur-wim` | System `libwim-dev` install requirement |
| `libhivex` | `sufur-wim` | System `libhivex-dev` install requirement |

### Build Pattern (`sufur-fs/build.rs`)

Each crate that wraps C libraries follows the same pattern in `build.rs`:

```rust
// sufur-fs/build.rs
fn main() {
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap();
    let cfg_suffix = match os.as_str() {
        "macos"  => "macos",
        "linux"  => "linux",
        other    => panic!("unsupported target OS: {other}"),
    };

    // NTFS (mkntfs logic from ntfs-3g)
    cc::Build::new()
        .files(glob("../../vendor/ntfs-3g/libntfs-3g/src/*.c"))
        .files(&["../../vendor/ntfs-3g/ntfsprogs/mkntfs.c"])
        .include("../../vendor/ntfs-3g/include")
        .include(format!("vendor-config/ntfs3g-{cfg_suffix}.h"))
        .define("HAVE_CONFIG_H", None)
        .compile("ntfs-format");

    // FAT (mkfs.fat logic from dosfstools)
    cc::Build::new()
        .files(glob("../../vendor/dosfstools/src/*.c"))
        .include("../../vendor/dosfstools/src")
        .include(format!("vendor-config/dosfstools-{cfg_suffix}.h"))
        .define("HAVE_CONFIG_H", None)
        .compile("fat-format");

    // exFAT (mkfs.exfat logic from exfatprogs)
    cc::Build::new()
        .files(glob("../../vendor/exfatprogs/mkfs/*.c"))
        .files(glob("../../vendor/exfatprogs/lib/*.c"))
        .include("../../vendor/exfatprogs/include")
        .include(format!("vendor-config/exfatprogs-{cfg_suffix}.h"))
        .define("HAVE_CONFIG_H", None)
        .compile("exfat-format");

    println!("cargo:rerun-if-changed=../../vendor/");
    println!("cargo:rerun-if-changed=vendor-config/");
}
```

```rust
// sufur-wim/build.rs
fn main() {
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap();
    let cfg_suffix = match os.as_str() {
        "macos" => "macos",
        "linux" => "linux",
        other   => panic!("unsupported target OS: {other}"),
    };

    // wimlib — WIM extraction and creation
    cc::Build::new()
        .files(glob("../../vendor/wimlib/src/*.c"))
        .include("../../vendor/wimlib/include")
        .include(format!("vendor-config/wimlib-{cfg_suffix}.h"))
        .define("HAVE_CONFIG_H", None)
        .compile("wim");

    // libhivex — Windows registry hive (BCD) editing
    cc::Build::new()
        .files(glob("../../vendor/libhivex/lib/*.c"))
        .include("../../vendor/libhivex/lib")
        .include(format!("vendor-config/libhivex-{cfg_suffix}.h"))
        .define("HAVE_CONFIG_H", None)
        .compile("hivex");

    println!("cargo:rerun-if-changed=../../vendor/");
    println!("cargo:rerun-if-changed=vendor-config/");
}
```

### Rust FFI Layer

The C entry points are wrapped in thin, safe Rust abstractions. The public API of `sufur-fs` and `sufur-wim` never exposes raw C types.

```rust
// sufur-fs/src/ntfs.rs
mod ffi {
    extern "C" {
        // Entry point exposed by the ntfs-3g mkntfs compilation unit
        fn mkntfs_main(argc: c_int, argv: *const *const c_char) -> c_int;
    }
}

pub struct NtfsFormatter;

impl NtfsFormatter {
    pub fn format(&self, device: &Path, label: Option<&str>, quick: bool) -> Result<(), Error> {
        // Build argv, call mkntfs_main, map return code to typed Error
        ...
    }
}
```

```rust
// sufur-wim/src/extract.rs
mod ffi {
    extern "C" {
        fn wimlib_open_wim(path: *const c_char, flags: u32, wim: *mut *mut WimStruct) -> c_int;
        fn wimlib_extract_image(wim: *mut WimStruct, image: c_int, target: *const c_char, flags: u32) -> c_int;
        fn wimlib_free(wim: *mut WimStruct);
        // ...
    }
}
```

### Developer Prerequisites

| Platform | Requirement |
|----------|------------|
| Linux | `gcc` or `clang`, `make` (standard on any dev machine) |
| macOS | Xcode Command Line Tools (`xcode-select --install`) |
| Windows | MSVC or MinGW (only if Windows port is pursued) |

No `apt install libwimlib-dev`, no `brew install ntfs-3g`. A plain `cargo build` is sufficient.

### Upgrading a Submodule

```bash
# 1. Pull the new release tag
cd vendor/wimlib
git fetch --tags
git checkout v1.14.4   # pin to a release tag, never track main
cd ../..

# 2. Regenerate config.h on each supported platform and check them in
#    (must be done on a Linux machine AND a macOS machine)
cd vendor/wimlib && ./configure && cp config.h ../../sufur-wim/vendor-config/wimlib-linux.h
cd vendor/wimlib && ./configure && cp config.h ../../sufur-wim/vendor-config/wimlib-macos.h

# 3. Commit everything together
git add vendor/wimlib sufur-wim/vendor-config/
git commit -m "chore: bump wimlib to v1.14.4"
```

Submodules are always pinned to a **release tag**, never a branch, to ensure reproducible builds. config.h regeneration is **mandatory** when bumping a submodule — skipping it risks silent feature-detection mismatches.

---

## Platform Abstraction Layer

Platform differences are encoded in traits, not `cfg` blocks in business logic.

```rust
// sufur-platform/src/lib.rs
pub trait Platform: Send + Sync {
    fn list_devices(&self) -> Result<Vec<Device>, Error>;
    fn open_device(&self, id: &DeviceId) -> Result<Box<dyn BlockDevice>, Error>;
    fn format_partition(&self, p: &Partition, fs: Filesystem, opts: FormatOptions) -> Result<(), Error>;
    fn mount(&self, src: &Path, tgt: &Path, fs: Option<&str>) -> Result<MountHandle, Error>;
    fn watch_devices(&self) -> impl Stream<Item = DeviceEvent>;  // hotplug
}
```

`sufur-core` is constructed with a `Platform` implementation injected:

```rust
pub struct Sufur {
    platform: Arc<dyn Platform>,
}

impl Sufur {
    // Primary constructor — platform injected (testable)
    pub fn new(platform: impl Platform + 'static) -> Self;

    // Convenience — selects the correct platform for the current OS
    pub fn for_current_platform() -> Result<Self, Error>;
}
```

---

## GUI Integration

### Qt (Primary GUI) — Hybrid Process Model

The Qt GUI uses a **hybrid process model**. Unprivileged operations run in-process via `cxx`; privileged operations are delegated to a short-lived `sufur-helper` process launched through polkit. The GUI never runs as root, and the Qt event loop must never execute with elevated privileges.

```
Qt GUI (user)
    │
    ├── cxx ──► sufur-core (analysis/planning)
    │
    └── launch helper
             │
             ▼
      sufur-helper (root)
             │
             ▼
         sufur-core
```

#### Unprivileged Operations (In-Process)

The following execute directly inside the Qt process via the `cxx` bridge:

- device enumeration
- image analysis
- job creation and validation
- capability detection

**`cxx` bridge:**

```rust
// sufur-qt/rust/src/lib.rs
#[cxx::bridge]
mod ffi {
    struct Device {
        id: String, path: String, vendor: String,
        model: String, size_bytes: u64, removable: bool,
    }
    struct ImageInfo {
        path: String, size_bytes: u64,
        is_windows_iso: bool, is_win2go_capable: bool,
    }

    extern "Rust" {
        type SufurEngine;
        fn sufur_engine_new() -> Result<Box<SufurEngine>>;
        fn list_devices(engine: &SufurEngine) -> Result<Vec<Device>>;
        fn analyze_image(engine: &SufurEngine, path: &str) -> Result<ImageInfo>;
    }
}
```

**Threading (Qt):** Analysis and enumeration calls via the `cxx` bridge run on a dedicated `QThread` (`SufurWorker`). The Rust progress callback marshals events to the Qt main thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` — never emit signals directly from the Rust callback, as it runs on the worker thread.

**Cancellation (in-process):** Shared `Arc<AtomicBool>` between `SufurWorker` and the Rust engine for long-running analysis operations. The Qt "Cancel" button sets the flag; Rust checks it at safe points and returns early with a `Cancelled` error.

#### Privileged Operations (Helper Process)

When the user starts a write operation:

1. Qt constructs a validated job specification.
2. Qt launches `sufur-helper` through polkit.
3. Helper executes the workflow using `sufur-core`.
4. Progress is streamed back to Qt as NDJSON events.
5. Helper exits when finished.

The GUI never runs as root. The Qt event loop must never execute with elevated privileges.

Progress events from the helper use the same NDJSON event stream already defined for the CLI. Qt parses the stream and renders progress in the UI. See the [Privilege Model](#privilege-model) section for details on the helper protocol and cancellation semantics.

### Future Frontends (Separate Repos)

All future native frontends follow the same hybrid model: direct-link to `sufur-core` for unprivileged operations, launch `sufur-helper` for privileged operations:

```
sufur-gtk (Rust/gtk-rs) ─────► sufur-core (analysis)     ──► sufur-helper (write)
sufur-tui (Rust/ratatui) ────► sufur-core (analysis)     ──► sufur-helper (write)
sufur-tauri (Rust + Web) ────► sufur-core (analysis)     ──► sufur-helper (write)
```

### CLI and Scripts — Out-of-Process

The CLI is the stable out-of-process interface for scripts, CI, and agents. Unprivileged commands (`list`, `analyze-image`, `validate`, `capabilities`) execute directly in-process via `sufur-core`. Privileged commands (`create`, `format`, `wipe`) launch `sufur-helper` through polkit — the same helper used by all GUIs.

```
Shell scripts ────────spawn──────► sufur CLI
                                    │
                                    ├── analyze / list / etc
                                    │       ↓
                                    │   sufur-core
                                    │
                                    └── create / format / wipe
                                            ↓
                                      launch sufur-helper
                                            ↓
                                        sufur-core

AI agents ────────────spawn──────► sufur CLI ──────► (same flow as above)
```

---

## CLI Interface

The `sufur` binary is the stable out-of-process API surface.

### Commands

```
sufur list               List removable USB devices
sufur analyze-image      Analyze an ISO or disk image
sufur create             Create a bootable USB drive
sufur format             Format a device or partition
sufur wipe               Wipe partition table
sufur validate           Verify image checksum
sufur capabilities       Report system tool availability
sufur apply-job          Apply a declarative job file
```

### Output Modes

**Human (default):** TTY-friendly spinners and progress bars on stderr.

**Machine (`--json`):** NDJSON on stdout, one JSON object per line.

```bash
$ sufur create debian.iso --device /dev/sdb --json
{"type":"started","operation_id":"op_7f8a9b2c"}
{"type":"stage","stage":"partition","message":"Creating GPT partition table"}
{"type":"progress","stage":"copy","percent":45,"bytes_done":483183820,"bytes_total":1073741824,"eta_seconds":92}
{"type":"complete","success":true,"duration_ms":45231}
```

Errors include machine-actionable remediation:

```json
{
  "type": "error",
  "code": "DEVICE_BUSY",
  "message": "Device /dev/sdb is mounted at /media/user/USB_DRIVE",
  "remediation": { "action": "unmount", "command": "umount /dev/sdb1", "auto_fixable": true }
}
```

---

## Progress & Cancellation

### Progress Events

```rust
pub enum ProgressEvent {
    /// A named pipeline stage has started (e.g. "partition", "format", "copy")
    Stage    { name: String, message: String },

    /// Incremental progress within the current stage
    Progress { percent: u8, bytes_done: u64, bytes_total: u64, eta_seconds: Option<u64> },

    /// Non-fatal warning; operation continues
    Warning  { message: String },

    /// Informational message; no action required
    Info     { message: String },

    /// Operation completed successfully
    Complete { duration_ms: u64 },

    /// Operation failed; cleanup/rollback is now starting
    /// Consumers should display `reason` and wait for Cleanup before allowing retry
    Rollback { reason: String },

    /// Cleanup after a failure has finished
    /// `success: true` means the device is back in a known-clean state (e.g. partition table wiped)
    /// `success: false` means cleanup itself failed; device state is unknown — warn the user
    Cleanup  { success: bool, message: String },

    /// Operation was cancelled by the user; cleanup is starting (emits Rollback + Cleanup sequence)
    Cancelled,

    /// Unrecoverable error with optional remediation hint
    Error    { code: ErrorCode, message: String, suggestion: Option<String> },
}

pub trait ProgressHandler: Send {
    fn on_event(&mut self, event: ProgressEvent);
}
```

**Event sequencing on failure:**

```
Stage("partition") → Progress(...) → Stage("format") → Error(...)
  → Rollback { reason: "format failed: ..." }
  → Cleanup  { success: true, message: "partition table wiped" }
```

**Event sequencing on cancellation:**

```
Stage("copy") → Progress(45%) → [user cancels]
  → Cancelled
  → Rollback { reason: "cancelled by user" }
  → Cleanup  { success: true, message: "partition table wiped" }
```

Consumers (GUI and CLI) must not allow retrying or starting a new operation until a `Cleanup` event is received.

### Cancellation

| Consumer | Mechanism |
|----------|-----------|
| Qt GUI (in-process) | `Arc<AtomicBool>` flag set by "Cancel" button; Rust checks at safe points in analysis loop |
| Qt GUI (helper) | Frontend sends cancel signal to `sufur-helper`; helper sets internal cancellation flag; `sufur-core` performs rollback/cleanup; helper exits |
| CLI (interactive, in-process) | `SIGTERM` handler sets `AtomicBool`; Rust unwinds cleanly through the cancel path |
| CLI (interactive, helper) | `SIGTERM` to CLI process forwards cancel signal to `sufur-helper`; helper performs rollback/cleanup and exits |
| CLI (scripted) | Send `SIGTERM` to the `sufur` process; SIGKILL only as a last resort (leaves device in unknown state) |

---

## Declarative Job Format

Agents and scripts can describe an operation as a YAML/JSON file instead of constructing CLI flags:

```yaml
# sufur-job.yaml
image:
  path: ./debian-12.iso
  verify_checksum: sha256:abc123...

device:
  path: /dev/sdb            # explicit
  # OR: selector: "vendor:SanDisk AND size_gb>=16"

layout:
  partition_scheme: gpt
  target_system: uefi
  filesystem: fat32
  volume_label: DEBIAN_12

options:
  quick_format: true
  bad_blocks_check: false
```

```bash
sufur validate-job sufur-job.yaml --json   # dry-run check
sufur apply-job    sufur-job.yaml --json   # execute
```

---

## WebUSB & Web Frontend

Browsers cannot write raw block devices. Two modes are supported:

| Mode | Capability | Use case |
|------|-----------|----------|
| Pure browser (WebUSB only) | Enumerate devices, analyze ISO via WASM, no write | Pre-flight check, docs site |
| Tauri app | Full write via embedded `sufur-core` (analysis) + `sufur-helper` (writes) | Desktop install, offline |

The `sufur-wasm` crate compiles `sufur-core`'s image analysis to WebAssembly. **Note:** the image analysis API must accept `Read + Seek`, not `&Path`, for WASM compatibility.

---

## Privilege Model

Raw block device access requires root on all platforms. Sufur follows a host-process execution model: `sufur-core` may execute inside an unprivileged frontend process for analysis and planning tasks, or inside a short-lived elevated helper process for destructive device operations. The same workflow engine is used in both cases. Privilege boundaries are implemented at the process level, not inside business logic.

### Principle

Most Sufur functionality does not require elevated privileges:

- device enumeration
- hotplug monitoring
- ISO analysis
- checksum verification
- job planning
- capability detection

Only destructive operations require elevation:

- partitioning
- formatting
- raw block device writes
- privileged mount/unmount operations

Therefore:

- Frontends remain unprivileged.
- Privileged execution occurs in a short-lived helper process.
- The helper hosts the same `sufur-core` workflow engine used elsewhere.
- The helper exits when the operation completes.

### Linux Privilege Model

#### Execution Flow

```text
Qt / GTK / Tauri / CLI
    │
    ├─ Analyze image
    ├─ Enumerate devices
    ├─ Build validated CreateJob
    │
    ▼
pkexec sufur-helper execute-job job.json
    │
    ▼
sufur-core
    │
    ▼
Linux platform implementation
```

#### Helper Responsibilities

The helper is intentionally thin. Its responsibilities are:

1. Acquire privileges through polkit/pkexec.
2. Deserialize a validated job description.
3. Construct `Sufur`.
4. Execute the workflow using `sufur-core`.
5. Stream progress events.
6. Exit.

Business logic must remain in `sufur-core`, not in the helper.

#### Progress Transport

The helper emits the same NDJSON event stream already defined for the CLI.

```json
{"type":"started","operation_id":"op_123"}
{"type":"stage","stage":"format"}
{"type":"progress","percent":45}
{"type":"complete"}
```

Frontends consume this stream and render progress. This ensures a single event model across:

- CLI
- Qt
- GTK
- Tauri
- future web integrations

#### Cancellation

Cancellation is implemented through process communication with the helper. The helper owns the workflow execution.

When cancellation is requested:

1. Frontend sends a cancel signal to the helper.
2. Helper sets the internal cancellation flag.
3. `sufur-core` performs normal rollback/cleanup handling.
4. Helper exits after cleanup.

Cancellation semantics remain owned by `sufur-core`.

### Operation Privilege Summary

| Operation | Privilege needed | Mechanism |
|-----------|-----------------|-----------|
| List devices | User | udev sysfs read (Linux), IOKit (macOS), SetupAPI (Windows) |
| Analyze ISO | User | Regular file read |
| Watch hotplug events | User | udev subscription (Linux), IOKit notifications (macOS) |
| Partition / format / write | Root | Linux: `sufur-helper` via polkit/pkexec; CLI and GUI both use the same helper |
| macOS device write | Root + FDA | Full Disk Access required; `diskutil unmountDisk` before open |
| Windows device write | Elevated token | UAC prompt via `ShellExecute("runas")`; progress via named pipe |

### CLI Privilege Model

The CLI always runs unprivileged. Unprivileged commands (`list`, `analyze-image`, `validate`, `capabilities`) execute directly in-process via `sufur-core`. Privileged commands (`create`, `format`, `wipe`) launch `sufur-helper` through polkit — the same helper binary used by all GUIs. The helper writes NDJSON progress to stdout, which the CLI forwards to the terminal or consuming process.

This avoids duplicating privilege logic between GUI and CLI. The helper becomes the single implementation of privileged execution.

```text
sufur CLI
    │
    ├── analyze/list/etc
    │       ↓
    │   sufur-core
    │
    └── create/format/wipe
            ↓
      launch sufur-helper
            ↓
        sufur-core
```

### Qt GUI Privilege Model

The Qt GUI remains unprivileged at all times. Unprivileged operations (device enumeration, image analysis, job creation, validation, capability detection) execute in-process via the `cxx` bridge. Privileged operations (partitioning, formatting, raw writes) are delegated to `sufur-helper`, launched through polkit.

The GUI never runs as root. The Qt event loop must never execute with elevated privileges. This is a decided architecture — not an open question.

---

## Testing Strategy

| Layer | Approach |
|-------|----------|
| `sufur-core` unit tests | `Sufur::new(mock_platform)` — no real devices needed |
| Image analysis | Fixture ISO files in `tests/fixtures/` |
| Integration (device write) | Loop devices (`losetup`), requires root, marked `#[ignore]` |
| CLI contract | Spawn `sufur` binary, assert JSON schema of stdout |
| Platform implementations | Real devices or loop devices; skipped if unavailable |
| Qt GUI | Qt test framework; mock `SufurEngine` via test crate |

### Fuzz Targets

ISO 9660, WIM, and NTFS parsing are attack surfaces — especially in automated pipelines where images may come from untrusted sources. The following fuzz targets are maintained using `cargo-fuzz` (libFuzzer):

```
fuzz/
├── fuzz_targets/
│   ├── analyze_iso.rs       # Feed arbitrary bytes as ISO 9660 image
│   ├── analyze_wim.rs       # Feed arbitrary bytes as WIM header
│   ├── parse_ntfs_boot.rs   # Feed arbitrary bytes as NTFS boot sector
│   └── parse_job_yaml.rs    # Feed arbitrary bytes as job YAML file
```

```rust
// fuzz/fuzz_targets/analyze_iso.rs
#![no_main]
use libfuzzer_sys::fuzz_target;
use sufur_core::image::analyze;
use std::io::Cursor;

fuzz_target!(|data: &[u8]| {
    // Must not panic — only return Ok or Err
    let _ = analyze(Cursor::new(data));
});
```

Running the fuzz suite:

```bash
cargo +nightly fuzz run analyze_iso
cargo +nightly fuzz run analyze_wim
```

**Requirements:**
- `analyze_image` and all format parsers must accept `Read + Seek` (not `&Path`) — this is also required for WASM compatibility
- Parsers must never panic on malformed input; all error paths must return `Result::Err`
- Fuzz corpus is stored in `fuzz/corpus/` and checked into git; CI runs fuzz targets for a fixed duration (e.g. 60 seconds) on each PR

---

## Migration Roadmap

### Phase 1 — Scaffolding (1 week)
- Rust workspace with all crate skeletons
- `sufur-platform` trait definitions
- `sufur-cli` with `clap` and `--json` skeleton

### Phase 2 — Linux Core (3 weeks)
- Add git submodules: `ntfs-3g`, `dosfstools`, `exfatprogs`, `wimlib`, `libhivex` under `vendor/`
- Pin each submodule to a release tag; document minimum versions
- `sufur-linux`: udev enumeration, libfdisk partitioning, mount
- `sufur-fs`: `build.rs` compiling ntfs-3g, dosfstools, exfatprogs via `cc` crate; safe Rust wrappers for each formatter
- `sufur-core`: image analysis, create pipeline, progress streaming
- `sufur-wim`: `build.rs` compiling wimlib and libhivex via `cc` crate; safe Rust wrappers for WIM extraction and BCD hive editing
- CLI commands: `list`, `analyze-image`, `create`
- Unit tests for all core logic

### Phase 3 — CLI Polish (1 week)
- Human-friendly TTY output (spinners, progress bars via `indicatif`)
- Comprehensive `--json` output for all commands and error cases
- `sufur-helper` binary: polkit/pkexec integration, job deserialization, NDJSON progress streaming
- `sufur capabilities` command
- Man page and `--help`

### Phase 4 — Qt GUI (2 weeks)
- `sufur-qt` repository with `cxx` bridge crate (`rust/`)
- CMake + Corrosion build integrating the Rust bridge as a static lib
- `cxx::bridge` in `sufur-qt/rust/src/lib.rs` exporting `sufur-core` types
- `SufurEngine` C++ wrapper; `SufurWorker` `QThread` for async operations
- Progress marshalled to Qt main thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
- Cancellation via `Arc<AtomicBool>` for in-process analysis; cancel signal to `sufur-helper` for privileged operations
- QML UI ported: `Driver` → `SufurEngine` + `SufurWorker`
- Integrate `sufur-helper` for privileged operations: launch via polkit, stream NDJSON progress, handle cancellation
- End-to-end write test on Linux

### Phase 5 — macOS (2 weeks)
- `sufur-macos`: IOKit enumeration, diskutil partition/format, mount
- Gate Windows-specific features behind `#[cfg(target_os = "linux")]`
- Linux ISO creation tested on macOS
- macOS limitations documented

### Phase 6 — Web & WASM (2 weeks)
- `sufur-wasm`: refactor image analysis to `Read + Seek`; compile to WASM
- `sufur-web`: React prototype with WebUSB enumeration and WASM-based ISO analysis

### Phase 7 — Advanced & Frontends (ongoing)
- Persistence, bad blocks, compressed image support (gz, xz, zip)
- `sufur-tauri` desktop wrapper (shared UI with `sufur-web`)
- `sufur-gtk` / `sufur-tui` alternative frontends
- Windows User Experience (`unattend.xml` generation)
- Publish `sufur-wasm` to npm

---

## Known Constraints & Open Decisions

| Item | Status | Notes |
|------|--------|-------|
| `libhivex` FFI (BCD editing) | Risk | Poorly documented; consider `chntpw` CLI as fallback if FFI proves intractable |
| `sufur-wasm` image analysis | Blocked | Requires `Read + Seek` refactor of all format parsers before WASM or fuzzing works |
| udev hotplug events | Not yet designed | `Platform::watch_devices()` needs a concrete `Stream` implementation |
| Linux helper transport | Open | Evaluate stdout NDJSON, Unix domain sockets, or pipes for progress streaming |
| Linux helper packaging | Open | Determine installation and polkit integration strategy |
| Qt privilege model | Decided | Privileged execution occurs in `sufur-helper`; GUI remains unprivileged |
| config.h generation environment | Decided | Generated on Ubuntu 22.04 LTS (Linux) and macOS 14 Sonoma; must be regenerated on submodule bumps |
| Job format versioning | Decided | `version: 1` field required; parser rejects unknown versions with a clear error |

### macOS: TCC and Full Disk Access

macOS enforces Transparency, Consent, and Control (TCC) for raw block device access. Even `root` cannot open `/dev/rdisk*` without the process holding **Full Disk Access** (FDA) permission.

**Decision: FDA is the required path. `authopen` is not sufficient.**

`authopen` can open files with elevated privileges but does not grant raw block device access for writing partition tables or filesystem structures — it is designed for regular file I/O, not device I/O. Any implementation relying on `authopen` for write operations will fail silently or with cryptic permission errors.

The required approach for macOS:

1. The app bundle must declare the `com.apple.security.device.usb` entitlement and request FDA via the `NSDesktopFolderUsageDescription` / `NSRemovableVolumesUsageDescription` keys in `Info.plist`
2. On first launch, the app prompts the user to grant Full Disk Access in System Settings → Privacy & Security → Full Disk Access
3. If FDA is not granted, all write operations must fail immediately with a clear error message and a deep link to the relevant System Settings pane: `x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles`
4. `diskutil unmountDisk /dev/diskN` must be called before opening the raw device, even with FDA

This is mandatory from day one of macOS support (Phase 5+). The `PrivilegedPlatform` trait for macOS must check for FDA at startup and surface a typed `Error::PermissionDenied { reason: "Full Disk Access required", deep_link: "..." }` before attempting any device I/O.

### Windows: Progress Channel for Elevated Process

On Windows, UAC elevation via `ShellExecute(..., "runas", ...)` spawns a new process with a different security token. Unlike Unix `setuid`, an elevated child process **cannot inherit pipes** from its unprivileged parent — the pipe handles are not valid across the privilege boundary.

The correct mechanism for passing progress events from `sufur-helper` back to the unprivileged GUI is a **named pipe created by the elevated child**:

```
[unprivileged Qt GUI]                    [elevated sufur-helper]
        │                                         │
        │  ShellExecute("runas", "sufur-helper    │
        │    execute-job --pipe \\\\.\\pipe\\sufur-{pid}")│
        ├────────────────────────────────────────►│
        │                                         │ CreateNamedPipe(
        │                                         │   "\\\\.\\pipe\\sufur-{pid}",
        │                                         │   PIPE_ACCESS_OUTBOUND)
        │◄── ConnectNamedPipe() ──────────────────┤
        │                                         │
        │◄═══ NDJSON progress events (ReadFile) ══╡ (write loop)
        │                                         │
        │◄── pipe closed (EOF = operation done) ──┤
```

**Protocol:**
- `sufur-helper` creates the named pipe on startup, using the parent's PID in the pipe name to avoid collisions: `\\.\pipe\sufur-progress-{parent_pid}`
- The GUI connects to the pipe after launching the helper, with a timeout (e.g. 5 seconds)
- Progress is streamed as NDJSON over the pipe, identical to the `--json` stdout format
- When the pipe closes (EOF), the operation is complete; the GUI reads the final `{"type":"complete"}` or `{"type":"error"}` event before disconnecting
- Cancellation: the GUI closes its end of the pipe; `sufur-helper` detects the broken pipe on the next write and begins the `Rollback` → `Cleanup` sequence

**Implementation note:** The named pipe approach is used by Rufus itself for its elevated helper. It is non-trivial but well-understood on Windows. This is scoped to Phase 6+.