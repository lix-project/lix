//! Nix-specific base32 encoding.
//!
//! this encoding is *unbelievably* broken. input is treated as if it were a
//! single little-endian integer, and then encoded into a *big* endian form.
//! this makes streaming impossible because byte order is obviously swapped,
//! and bit order also seems random. (this is why we can't have nice things)
//!
//! ***DO NOT*** use this encoding for anything new if it can be avoided. if
//! generic base32 encoding is needed for some reason, use the base32 crate.

use rootcause::{report, Report};

// omitted: E O U T
const ALPHABET: &[u8] = b"0123456789abcdfghijklmnpqrsvwxyz";
const B32_BITS_PER_DIGIT: usize = 5;

#[deprecated(note = "Nix base32 encoding is pretty broken, use the base32 crate if possible")]
pub fn encode(data: &[u8]) -> String {
    if data.is_empty() {
        return String::default();
    }

    let len = (data.len() * 8 - 1) / B32_BITS_PER_DIGIT + 1;

    let mut res = Vec::with_capacity(len);

    for n in (0..len).rev() {
        let b = n * B32_BITS_PER_DIGIT;
        let i = b / 8;
        let j = b % 8;

        let cur_char = data[i];
        let second = if i >= data.len() - 1 {
            0
        } else {
            (data[i + 1] as u32) << (8 - j)
        };
        let c = ((cur_char as u32) >> j) | second;

        res.push(ALPHABET[(c & 0x1f) as usize]);
    }

    #[allow(clippy::unwrap_used, reason = "`res` contains only ascii bytes")]
    String::from_utf8(res).unwrap()
}

#[deprecated(note = "Nix base32 encoding is pretty broken, use the base32 crate if possible")]
pub fn decode(data: &[u8]) -> Result<Vec<u8>, Report> {
    if data.is_empty() {
        return Ok(Vec::default());
    }

    let len = ((data.len() - 1) * 5) / 8 + 1;
    let mut res: Vec<_> = std::iter::repeat_n(0, len).collect();

    for n in 0..data.len() {
        let c = data[data.len() - n - 1];
        let mut digit: u8 = 0;
        while (digit as usize) < ALPHABET.len() {
            if ALPHABET[digit as usize] == c {
                break;
            }
            digit += 1;
        }
        if digit >= 32 {
            return Err(report!("invalid base-32 string").attach(String::from_utf8_lossy(data).to_string()));
        }
        let b = n * 5;
        let i = b / 8;
        let j = b % 8;
        res[i] |= digit << j;

        if i < res.len() - 1 {
            if j > 0 {
                res[i + 1] |= digit >> (8 - j);
            }
        } else {
            if j > 0 && digit >> (8 - j) != 0 {
                return Err(
                    report!("invalid base-32 string").attach(String::from_utf8_lossy(data).to_string())
                );
            }
        }
    }

    Ok(res)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn empty_string() {
        assert_eq!(encode(&[]), "");
        assert_matches!(decode(b"").as_deref(), Ok([]));
    }

    #[test]
    fn encode_string() {
        assert_eq!(
            encode(b"quod erat demonstrandum"),
            "6sxb4drhp4x3kdrpnsrb441s62wk541j6yxbi",
        );
    }

    #[test]
    fn decode_string() {
        assert_eq!(
            decode(b"6sxb4drhp4x3kdrpnsrb441s62wk541j6yxbi").unwrap(),
            b"quod erat demonstrandum"
        );
    }

    #[test]
    fn encode_and_decode() {
        let s = b"quod erat demonstrandum";
        let encoded = encode(s);
        let decoded = decode(encoded.as_bytes()).unwrap();

        assert_eq!(decoded, s);
    }

    #[test]
    fn encode_and_decode_non_printable() {
        let s = (0..=257).map(|i| i as u8).collect::<Vec<_>>();
        let encoded = encode(&s);
        let decoded = decode(encoded.as_bytes()).unwrap();

        assert_eq!(decoded, s);
    }

    #[test]
    fn encode_handles_nul() {
        // Just throw a NUL in there somewhere.
        let s = b"cat g\0rls say meow even with NULs";

        let encoded = encode(s);
        let decoded = decode(encoded.as_bytes()).unwrap();

        assert_eq!(decoded, s);
    }

    #[test]
    fn decode_handles_invalid_chars() {
        assert_matches!(
            decode(b"6sxb4drhp4x3kdrpnsrb441s62wk541j6yxbe"),
            Err(e) if e.to_string().contains("invalid base-32 string")
        );
    }
}
