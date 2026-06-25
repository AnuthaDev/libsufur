//! Progress event model shared by the CLI, all GUIs, and `sufur-helper`.
//!
//! Every consumer renders the same NDJSON event stream — there is exactly one
//! progress representation in the entire system (architecture § "Progress &
//! Cancellation" and § "Privilege Model → Progress Transport").

use serde::{Serialize, Deserialize};

/// A single progress event in the pipeline event stream.
///
/// Event ordering on failure:
///   `Stage → Progress → … → Error → Rollback → Cleanup`
///
/// Event ordering on cancellation:
///   `Stage → Progress → Cancelled → Rollback → Cleanup`
///
/// Consumers must not allow retrying or starting a new operation until a
/// `Cleanup` event is received.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "lowercase")]
pub enum ProgressEvent {
    /// A named pipeline stage has started (e.g. "partition", "format", "copy").
    Stage { name: String, message: String },

    /// Incremental progress within the current stage.
    Progress {
        percent: u8,
        bytes_done: u64,
        bytes_total: u64,
        #[serde(skip_serializing_if = "Option::is_none")]
        eta_seconds: Option<u64>,
    },

    /// Non-fatal warning; operation continues.
    Warning { message: String },

    /// Informational message; no action required.
    Info { message: String },

    /// Operation completed successfully.
    Complete { duration_ms: u64 },

    /// Operation failed; cleanup/rollback is now starting.
    Rollback { reason: String },

    /// Cleanup after a failure has finished.
    /// `success: true` → device is back in a known-clean state.
    /// `success: false` → cleanup itself failed; device state is unknown.
    Cleanup { success: bool, message: String },

    /// Operation was cancelled by the user; rollback is starting
    /// (emits `Rollback` + `Cleanup` next).
    Cancelled,

    /// Unrecoverable error with optional remediation hint.
    Error {
        code: crate::ErrorCode,
        message: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        suggestion: Option<String>,
    },
}

/// Sink for progress events.  Implementations: CLI (TTY spinner or NDJSON
/// stdout), Qt (marshals to main thread via queued connection), helper
/// (NDJSON stdout).
pub trait ProgressHandler: Send {
    fn on_event(&mut self, event: ProgressEvent);
}
