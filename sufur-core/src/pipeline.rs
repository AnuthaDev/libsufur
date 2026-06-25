//! Create pipeline — the multi-stage write workflow.
//!
//! Stages: `partition → format → copy → (optional verify)`.
//! Each stage emits [`ProgressEvent`]s through a [`ProgressHandler`].
//! On failure or cancellation the pipeline drives the `Rollback → Cleanup`
//! sequence described in the architecture.

use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::{
    progress::{ProgressEvent, ProgressHandler},
    Error,
};

/// Declarative job specification (YAML/JSON), consumed by `sufur apply-job`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CreateJob {
    pub version: u32,
    pub image: ImageSpec,
    pub device: DeviceSpec,
    pub layout: Layout,
    pub options: JobOptions,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ImageSpec {
    pub path: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub verify_checksum: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceSpec {
    /// Explicit device path, e.g. `/dev/sdb`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
    /// Selector expression, e.g. `vendor:SanDisk AND size_gb>=16`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub selector: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Layout {
    pub partition_scheme: PartitionScheme,
    pub target_system: TargetSystem,
    pub filesystem: crate::Filesystem,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub volume_label: Option<String>,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum PartitionScheme {
    Gpt,
    Mbr,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum TargetSystem {
    Uefi,
    Bios,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct JobOptions {
    pub quick_format: bool,
    pub bad_blocks_check: bool,
}

/// Validate a job spec *without* touching devices.  Used by `sufur
/// validate-job` and by frontends before launching the helper.
///
/// Scaffold: checks `version == 1` and that exactly one of `path`/`selector`
/// is set on the device spec.
pub fn validate_job(job: &CreateJob) -> Result<(), Error> {
    if job.version != 1 {
        return Err(Error::platform(
            crate::ErrorCode::Internal,
            format!("unsupported job version: {} (expected 1)", job.version),
        ));
    }
    match (&job.device.path, &job.device.selector) {
        (Some(_), Some(_)) => Err(Error::platform(
            crate::ErrorCode::Internal,
            "device.path and device.selector are mutually exclusive",
        )),
        (None, None) => Err(Error::platform(
            crate::ErrorCode::DeviceNotFound,
            "device.path or device.selector is required",
        )),
        _ => Ok(()),
    }
}

/// Execute the create pipeline against the platform, streaming progress.
///
/// Scaffold: emits a `Stage`/`Complete` skeleton.  Real partitioning,
/// formatting, and copy land in Phase 2.
pub fn run_create(
    job: &CreateJob,
    _device_path: &Path,
    progress: &mut dyn ProgressHandler,
) -> Result<(), Error> {
    validate_job(job)?;

    progress.on_event(ProgressEvent::Stage {
        name: "partition".into(),
        message: "Creating partition table".into(),
    });
    progress.on_event(ProgressEvent::Stage {
        name: "format".into(),
        message: "Formatting target partition".into(),
    });
    progress.on_event(ProgressEvent::Stage {
        name: "copy".into(),
        message: "Writing image data".into(),
    });
    progress.on_event(ProgressEvent::Complete { duration_ms: 0 });
    Ok(())
}
