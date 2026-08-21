//! base64 encoding, and Nix-specific (unreasonably) lenient base64 decoding.

use base64::{
    alphabet::STANDARD,
    engine::{
        general_purpose::{PAD_INDIFFERENT, STANDARD_PAD_INDIFFERENT},
        GeneralPurpose,
    },
    Engine,
};
use rootcause::{prelude::ResultExt, Report};

/// encode as base64 with the [RFC 4648] standard alphabet.
///
/// [RFC 4648]: https://datatracker.ietf.org/doc/html/rfc4648#section-4
pub fn encode(data: &[u8]) -> String {
    STANDARD_PAD_INDIFFERENT.encode(data)
}

/// decode a base64 string (passed as `&[u8]` for ease of use from c++) using a
/// few inherited bad choices.
///
/// newline characters are ignored (which is mostly fine), padding isn't checked
/// (less than fine) and everything after the first padding character is ignored
/// entirely (very much *not* fine). being any stricter than this breaks nixpkgs
/// across the ages: any nixpkgs commit of nixpkgs is near guaranteed to contain
/// at least *one* malformed base64 string that needs to be decoded anyway since
/// it worked fine with the old decoders. this is why we can't have nice things.
pub fn decode(data: &[u8]) -> Result<Vec<u8>, Report> {
    // first try to decode with reasonably strict settings. if this works everything is well.
    if let Ok(r) = STANDARD_PAD_INDIFFERENT.decode(data) {
        return Ok(r);
    }

    // if that didn't work, filter all newlines (historically ignored) and strip everything
    // starting with the first padding character (historically used to terminate decoding).
    // TODO print very loud warnings if we get here and eventually *refuse* invalid inputs.
    let cleaned_data: Vec<_> = data
        .iter()
        .copied()
        .filter(|c| *c != b'\n')
        .take_while(|c| *c != b'=')
        .collect();
    Ok(
        GeneralPurpose::new(&STANDARD, PAD_INDIFFERENT.with_decode_allow_trailing_bits(true))
            .decode(cleaned_data)
            .attach_with(|| format!("input: {}", String::from_utf8_lossy(data)))?,
    )
}
