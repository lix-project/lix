use rootcause::{report, Report};
use toml::Value;

pub fn from_slice(data: &[u8]) -> Result<Value, Report> {
    toml::from_slice(data).map_err(|e| report!(e).into_dynamic())
}
