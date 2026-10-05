use std::sync::LazyLock;

use regex::Regex;

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum ObjectFormat {
    Sha1,
    Sha256,
}

/// Determine a remote's storage object format from `git ls-remote` output.
///
/// No `git ls-remote` option exposes the format: the `object-format` capability only exists in the
/// protocol v2 greeting, which the CLI discards. The format therefore has to be inferred from the
/// length of the object IDs in the advertisement.
///
/// Only the first field of a line (the OID, before the tab) should be inspected.
/// A SHA-1 repository can legitimately contain a ref *named* with 64 hex characters…
///
/// This doesn't take care of the possibly future repositories with mixed object formats…
///
/// In the event we are faced with mixed object formats, priority is given to SHA-256.
/// The reason for why is that it's more likely that Git will allow mixed formats using the sha256
/// parameter than the sha1 which is the default and will probably remain frozen in its behavior.
///
pub fn detect_object_format(output: &str) -> Option<ObjectFormat> {
    let mut saw_sha1 = false;

    for line in output.lines() {
        let Some((oid, _reference)) = line.split_once('\t') else {
            continue;
        };

        if oid.starts_with("ref:") || !oid.bytes().all(|b| b.is_ascii_hexdigit()) {
            continue;
        }

        match oid.len() {
            64 => return Some(ObjectFormat::Sha256),
            40 => saw_sha1 = true,
            _ => {}
        }
    }

    saw_sha1.then_some(ObjectFormat::Sha1)
}

#[derive(Debug, Eq, PartialEq)]
pub enum Kind {
    Symbolic,
    Object,
}

///
/// A line from the output of `git ls-remote --symref`
///
/// These can be of two kinds:
///
/// - Symbolic references of the form
///   ```
///   ref: {target}    {references}
///   ```
///   where {target} is itself a reference and {reference} is optional
///
/// - Object reference of the form
///   ```
///   {target}     {reference}
///   ```
///   where {target} is a commit id and {reference} is mandatory
#[derive(Debug)]
pub struct LsRemoteRefLine {
    pub kind: Kind,
    pub target: String,
    pub reference: Option<String>,
}

static LS_REMOTE_REF_REGEX: LazyLock<Regex> = LazyLock::new(|| {
    #[allow(clippy::unwrap_used)]
    Regex::new(r"^(ref: *)?([^\s]+)(?:\t+(.*))?$").unwrap()
});

impl LsRemoteRefLine {
    pub fn matches_ref_uri(&self, uri: &str) -> bool {
        let uri_regex = Regex::new(uri).expect("bad uri regex");
        match &self.reference {
            None => false,
            Some(r) => uri_regex.is_match(r.as_str()),
        }
    }
}

impl TryFrom<&str> for LsRemoteRefLine {
    type Error = String;

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        LS_REMOTE_REF_REGEX.captures(value).map(|m| {
            let kind = m
                .get(1)
                .filter(|ma| !ma.is_empty())
                .map_or(Kind::Object, |_| Kind::Symbolic);
            let re = m.get(3).map(|ma| ma.as_str()).map(|ma| {
                if ma.is_empty() {
                    None
                } else {
                    Some(ma.to_string())
                }
            });

            LsRemoteRefLine {
                kind,
                target: m
                    .get(2)
                    .expect("regex for matching remtoes has two capture groups defined, so this should not error")
                    .as_str()
                    .to_string(),
                reference: re.flatten(),
            }
        }).ok_or_else(|| "Invalid ref, did not match expected regex".into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_symref_line_with_reference() {
        let line = "ref: refs/head/main\tHEAD";
        let res = LsRemoteRefLine::try_from(line);
        assert!(res.is_ok());
        let res = res.unwrap();
        assert_eq!(res.kind, Kind::Symbolic);
        assert_eq!(res.target, "refs/head/main");
        assert_eq!(res.reference, Some("HEAD".to_string()));
    }

    #[test]
    fn parse_symref_line_with_no_reference() {
        let line = "ref: refs/head/main";
        let res = LsRemoteRefLine::try_from(line);
        assert!(res.is_ok());
        let res = res.unwrap();
        assert_eq!(res.kind, Kind::Symbolic);
        assert_eq!(res.target, "refs/head/main");
        assert_eq!(res.reference, None);
    }

    #[test]
    fn parse_object_ref_line() {
        let line = "abc123	refs/head/main";
        let res = LsRemoteRefLine::try_from(line);
        assert!(res.is_ok());
        let res = res.unwrap();
        assert_eq!(res.kind, Kind::Object);
        assert_eq!(res.target, "abc123");
        assert_eq!(res.reference, Some("refs/head/main".to_string()));
    }

    const SHA1_OID: &str = "39c25f63454572d62ac73ff642223d1dd20a824c";
    const SHA256_OID: &str = "62f076f86dd0b63e07ab0a5a45e59cbb5d7f72443bae28e19af4629ef3aa2aab";

    #[test]
    fn object_format_sha1() {
        let out = format!("{SHA1_OID}\tHEAD\n{SHA1_OID}\trefs/heads/main\n");
        assert_eq!(detect_object_format(&out), Some(ObjectFormat::Sha1));
    }

    #[test]
    fn object_format_sha256() {
        let out = format!("{SHA256_OID}\tHEAD\n{SHA256_OID}\trefs/heads/main\n");
        assert_eq!(detect_object_format(&out), Some(ObjectFormat::Sha256));
    }

    #[test]
    fn object_format_mixed() {
        let out = format!("{SHA256_OID}\tHEAD\n{SHA1_OID}\trefs/heads/main\n");
        assert_eq!(detect_object_format(&out), Some(ObjectFormat::Sha256));
    }

    #[test]
    fn object_format_ignores_symref_lines() {
        let out = format!("ref: refs/heads/main\tHEAD\n{SHA256_OID}\trefs/heads/main");
        assert_eq!(detect_object_format(&out), Some(ObjectFormat::Sha256));
    }

    #[test]
    fn object_format_ignores_hex_ref_names() {
        let out = format!("{SHA1_OID}\trefs/heads/{SHA256_OID}\n");
        assert_eq!(detect_object_format(&out), Some(ObjectFormat::Sha1));
    }

    #[test]
    fn object_format_annotated_tag_peels() {
        let out = format!("{SHA256_OID}\trefs/tags/v1\n{SHA256_OID}\trefs/tags/v1^{{}}\n");
        assert_eq!(detect_object_format(&out), Some(ObjectFormat::Sha256));
    }

    #[test]
    fn object_format_empty_advertisement() {
        assert_eq!(detect_object_format(""), None);
        assert_eq!(detect_object_format("ref: refs/heads/main\tHEAD\n"), None);
    }
}
