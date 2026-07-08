use std::sync::LazyLock;

use regex::Regex;

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
///     ref: {target}    {references}
///     where {target} is itself a reference and {reference} is optional
///
/// - Object reference of the form
///     {target}     {reference}
///     where {target} is a commit id and {reference} is mandatory
pub struct LsRemoteRefLine {
    pub kind: Kind,
    pub target: String,
    pub reference: Option<String>,
}

static LS_REMOTE_REF_REGEX: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^(ref: *)?([^\s]+)(?:\t+(.*))?$").unwrap());

impl LsRemoteRefLine {
    pub fn matches_ref_uri(&self, uri: &str) -> bool {
        let uri_regex = Regex::new(uri).unwrap();
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
                kind: kind,
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
}
