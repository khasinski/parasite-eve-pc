//! Errors shown to the translator. `Type` marks a malformed tag whose message is not prefixed with
//! the line key by the callers that prefix ordinary validation errors.
use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorKind {
    Value,
    Type,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    pub kind: ErrorKind,
    pub message: String,
}

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    pub fn value(message: impl Into<String>) -> Self {
        Self { kind: ErrorKind::Value, message: message.into() }
    }

    pub fn type_error(message: impl Into<String>) -> Self {
        Self { kind: ErrorKind::Type, message: message.into() }
    }

    /// `"{key}: {message}"` for validation errors; other errors keep their own text.
    pub fn for_key(self, key: &str) -> Self {
        match self.kind {
            ErrorKind::Value => Self::value(format!("{key}: {}", self.message)),
            ErrorKind::Type => self,
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for Error {}

impl From<std::io::Error> for Error {
    fn from(error: std::io::Error) -> Self {
        Self::value(error.to_string())
    }
}

impl From<serde_json::Error> for Error {
    fn from(error: serde_json::Error) -> Self {
        Self::value(error.to_string())
    }
}

impl From<zip::result::ZipError> for Error {
    fn from(error: zip::result::ZipError) -> Self {
        Self::value(error.to_string())
    }
}
