use base64::prelude::*;
use rootcause::{
    prelude::{IteratorExt, ResultExt},
    report,
    report_collection::ReportCollection,
    Report,
};
use serde::Deserialize;
use std::collections::{HashMap, HashSet};

use crate::generated::parseBuilderLines;

const MIN_VERSION: u16 = 1;
const LATEST_VERSION: u16 = 1;

/// Machine, as used in code-base, with all fields filled
#[derive(Deserialize, Clone, Debug, PartialEq)]
pub struct Machine {
    pub name: String,
    pub uri: String,
    pub system_types: HashSet<String>,
    pub ssh_key: String,
    pub max_jobs: u32,
    pub speed_factor: f32,
    pub supported_features: HashSet<String>,
    pub mandatory_features: HashSet<String>,
    pub ssh_public_host_key: String,
    pub enabled: bool,
}

///
/// Machine, as found in file
#[derive(Deserialize)]
#[serde(rename_all = "kebab-case", deny_unknown_fields)]
struct FileMachine {
    pub uri: Option<String>,
    pub system_types: Option<HashSet<String>>,
    pub ssh_key: Option<String>,
    #[serde(rename = "jobs")]
    pub max_jobs: Option<u32>,
    pub speed_factor: Option<f32>,
    pub supported_features: Option<HashSet<String>>,
    pub mandatory_features: Option<HashSet<String>>,
    pub ssh_public_host_key: Option<String>,
    #[serde(alias = "enable")]
    pub enabled: Option<bool>,
}

#[derive(Deserialize)]
struct MachinesFile {
    version: Option<u16>,
    machines: HashMap<String, FileMachine>,
}

impl MachinesFile {
    fn to_machines(self, current_system: &str) -> Result<Vec<Machine>, ReportCollection> {
        let version = self.version.unwrap_or(LATEST_VERSION);
        if version < MIN_VERSION || version > LATEST_VERSION {
            let mut rc = ReportCollection::new();
            rc.push(report!("Unable to parse Machines of version {}, only versions between {} and {} are supported.", version, MIN_VERSION, LATEST_VERSION).into_cloneable());
            return Err(rc);
        };

        self.machines
            .into_iter()
            .map(|(name, m)| {
                Machine {
                    name,
                    uri: m.uri.unwrap_or("".to_string()),
                    system_types: m
                        .system_types
                        .unwrap_or_else(|| HashSet::from([current_system.to_string()])),
                    ssh_key: m.ssh_key.unwrap_or_else(|| "".to_string()),
                    max_jobs: m.max_jobs.unwrap_or(1),
                    speed_factor: m.speed_factor.unwrap_or(1.0),
                    supported_features: m.supported_features.unwrap_or_else(|| HashSet::new()),
                    mandatory_features: m.mandatory_features.unwrap_or_else(|| HashSet::new()),
                    ssh_public_host_key: BASE64_STANDARD
                        .encode(m.ssh_public_host_key.unwrap_or_else(|| "".to_string())),
                    enabled: m.enabled.unwrap_or(true),
                }
                .validate()
            })
            .filter(|r| match r {
                Ok(machine) => machine.enabled,
                Err(_) => true,
            })
            .collect_reports::<Vec<Machine>, _>()
    }
}

impl Machine {
    fn validate(self) -> Result<Machine, Report> {
        let mut errs = ReportCollection::new();
        if self.uri.is_empty() {
            errs.push(report!("uri must be present and non-empty").into_cloneable());
        }
        if self.speed_factor < 0.0 {
            errs.push(report!("speed-factor must be >= 0").into_cloneable());
        }
        if !errs.is_empty() {
            return Err(errs
                .context(format!("while validating machine {}", self.name))
                .into_dynamic());
        }
        Ok(self)
    }
    /// Whether `system` is either `"builtin"` or in
    /// `systemTypes`.
    pub fn system_supported(&self, system: &String) -> bool {
        system == "builtin" || self.system_types.contains(system)
    }

    /// Whether `features` is a subset of the union of `supported_features` and `mandatory_features`
    pub fn all_supported(&self, features: &HashSet<String>) -> bool {
        features
            .iter()
            .all(|f| self.supported_features.contains(f) || self.mandatory_features.contains(f))
    }

    /// Whether `mandatory_features` is a subset of `features`
    pub fn mandatory_met(&self, features: &HashSet<String>) -> bool {
        self.mandatory_features.is_subset(features)
    }

    pub fn disable(&mut self) {
        self.enabled = false;
    }

    pub fn format_for_error(&self) -> String {
        format!(
            "{:?}, {}, {:?}, {:?}",
            self.system_types, self.max_jobs, self.supported_features, self.mandatory_features
        )
    }

    pub fn is_eligible(&self, needed_system: &String, required_features: &HashSet<String>) -> bool {
        self.enabled
            && self.system_supported(needed_system)
            && self.all_supported(required_features)
            && self.mandatory_met(required_features)
    }

    pub fn new(
        name: String,
        uri: String,
        system_types: HashSet<String>,
        ssh_key: String,
        max_jobs: u32,
        speed_factor: f32,
        supported_features: HashSet<String>,
        mandatory_features: HashSet<String>,
        ssh_public_host_key: String,
        enabled: bool,
    ) -> Self {
        Self {
            name,
            uri,
            system_types,
            ssh_key,
            max_jobs,
            speed_factor,
            supported_features,
            mandatory_features,
            ssh_public_host_key,
            enabled,
        }
    }
}

enum TomlParsingError {
    YouIntendedTomlError(Report),
    UncertainErr(Report),
}

fn get_toml_machines(
    setting: &String,
    current_system: &String,
) -> Result<Vec<Machine>, TomlParsingError> {
    let content = if let Some(file_path) = setting.strip_prefix("@") {
        std::fs::read_to_string(file_path)
            .context("while reading external machines file")
            .map_err(|e| {
                let e = e.into_dynamic();
                if file_path.ends_with("toml") {
                    TomlParsingError::YouIntendedTomlError(e)
                } else {
                    TomlParsingError::UncertainErr(e)
                }
            })?
    } else {
        setting.to_string()
    };
    if content.is_empty() {
        return Ok(vec![]);
    }
    Ok(toml::from_str::<MachinesFile>(content.as_str())
        .map_err(|e| TomlParsingError::UncertainErr(report!(e).into_dynamic()))?
        .to_machines(current_system.as_str())
        .context("while validating machines")
        .map_err(|e| TomlParsingError::YouIntendedTomlError(e.into_dynamic()))?)
}

pub fn get_machines(setting: String, current_system: String) -> Result<Vec<Machine>, Report> {
    match get_toml_machines(&setting, &current_system) {
        Ok(machines) => Ok(machines),
        Err(TomlParsingError::YouIntendedTomlError(e)) => Err(e),
        Err(TomlParsingError::UncertainErr(toml_err)) => {
            print_debug!("Trying again with legacy format");
            match parseBuilderLines(setting.as_str(), current_system.as_str()) {
                Ok(machines) => {
                    if machines.iter().any(|m| m.system_types.contains("=")) {
                        Err(toml_err)
                    } else {
                        Ok(machines)
                    }
                }
                Err(e) => Err(report!(e)
                    .into_dynamic()
                    .attach(toml_err.to_string().trim_start().to_string())),
            }
        }
    }
    .attach(format!("builders setting: {}", setting))
}

#[cfg(test)]
mod tests {
    mod integration {
        use crate::machines::*;

        fn gm(machines: &str) -> Result<Vec<Machine>, Report> {
            get_machines(machines.to_string(), "TEST_ARCH-TEST_OS".to_string())
        }
        #[test]
        fn toml_single_valid() {
            let machines = gm(r#"
                [machines.andesite]
                uri = "ssh://lix@andesite.lix.systems"
                "#)
            .unwrap();
            assert_eq!(machines.len(), 1);
            assert_eq!(
                machines.get(0).unwrap().uri.as_str(),
                "ssh://lix@andesite.lix.systems"
            );
        }

        #[test]
        fn legacy_single_valid() {
            let machines = gm("ssh://lix@diorite.lix.systems").unwrap();
            assert_eq!(machines.len(), 1);
            assert_eq!(
                machines.get(0).unwrap().uri.as_str(),
                "ssh://lix@diorite.lix.systems"
            );
        }

        #[test]
        fn missing_toml_uri_doesnt_retry() {
            let err = gm("[machines.missing-uri]").unwrap_err().to_string();
            assert!(err.contains("uri must be present"));
        }

        #[test]
        fn both_invalid_shows_both_errors() {
            let err = gm(r#"
                    hello world this is a very much invalid file
                    for both amazingly stupid formats
                    and i am having a buuuuuunch of fun writing this test file
                "#)
            .unwrap_err()
            .to_string();
            assert!(err.contains("bad machine specification: failed to convert column"));
            assert!(err.contains("TOML parse error at line 2, column 27"));
        }

        #[test]
        fn bad_toml_file_doesnt_retry_legacy() {
            // Non-existing files do not throw errors with the legacy format
            // but will just return an empty list of builders
            let err = gm("@/this/file/does/not/exist.toml")
                .unwrap_err()
                .to_string();
            assert!(err.contains("No such file"));
        }

        #[test]
        fn bad_toml_deser_retrys() {
            let machines = gm("a\nb").unwrap();
            assert!(machines.iter().any(|m| m.uri.ends_with("a")));
            assert!(machines.iter().any(|m| m.uri.ends_with("b")));
        }

        #[test]
        fn missing_quotes_doesnt_retry() {
            let err = gm(r#"
[machines.andesite]
uri = ssh://whooooops.forgotten.quotes
speed-factor = 3
"#)
            .unwrap_err()
            .to_string();
            assert!(err.contains("string values must be quoted"));
            assert!(!err.contains("bad machine specification"));
        }
    }
    mod toml {
        use crate::machines::*;

        fn gm(machines: &str) -> Result<Vec<Machine>, Report> {
            crate::machines::get_toml_machines(
                &machines.to_string(),
                &"TEST_ARCH-TEST_OS".to_string(),
            )
            .map_err(|e| match e {
                TomlParsingError::YouIntendedTomlError(e) => e,
                TomlParsingError::UncertainErr(e) => e,
            })
        }

        #[test]
        fn empty_builders() {
            let machines = gm("");
            assert!(machines.is_ok());
            assert_eq!(machines.unwrap(), vec![]);
        }

        #[test]
        fn get_uri_only() {
            let machines = gm(r#"
                [machines.scratchy]
                uri = "ssh://nix@scratchy.labs.cs.uu.nl"
            "#);
            let machines = machines.unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.uri, "ssh://nix@scratchy.labs.cs.uu.nl".to_string());
            assert_eq!(
                m.system_types,
                HashSet::from(["TEST_ARCH-TEST_OS".to_string()])
            );
            assert_eq!(m.ssh_key, "".to_string());
            assert_eq!(m.max_jobs, 1);
            assert_eq!(m.speed_factor, 1.0);
            assert_eq!(m.supported_features.len(), 0);
            assert_eq!(m.mandatory_features.len(), 0);
            assert_eq!(m.ssh_public_host_key, "".to_string());
        }

        #[test]
        fn multiple_machines() {
            let machines = gm(r#"
                [machines.scratchy]
                uri = "nix@scratchy/labs.cs.uu.nl"
                [machines.itchy]
                uri = "nix@itchy.labs.cs.uu.nl"
            "#);
            let machines = machines.unwrap();
            assert_eq!(machines.len(), 2);
            let names: Vec<&str> = machines.iter().map(|m| m.name.as_str()).collect();
            assert!(names.contains(&"scratchy"));
            assert!(names.contains(&"itchy"));
        }

        #[test]
        fn commplete_single_builder() {
            let machines = gm(r#"
                [machines.scratchy]
                uri = "nix@scratchy.labs.cs.uu.nl"
                system-types = ["i686-linux"]
                ssh-key = "/home/nix/.ssh/id_scratchy_auto"
                jobs = 8
                speed-factor = 3.0
                supported-features = ["kvm"]
                mandatory-features = ["benchmark"]
                ssh-public-host-key = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIJYfqESaiQlOrL3Wm1Q9s9q8b4mjj2nIuyqCZub5aGPi nix@scratchy"
            "#);
            let machines = machines.unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.uri, "nix@scratchy.labs.cs.uu.nl");
            assert_eq!(m.system_types, HashSet::from(["i686-linux".to_string()]));
            assert_eq!(m.ssh_key, "/home/nix/.ssh/id_scratchy_auto".to_string());
            assert_eq!(m.max_jobs, 8);
            assert_eq!(m.speed_factor, 3.0);
            assert_eq!(m.supported_features, HashSet::from(["kvm".to_string()]));
            assert_eq!(
                m.mandatory_features,
                HashSet::from(["benchmark".to_string()])
            );
            assert_eq!(m.ssh_public_host_key, "c3NoLWVkMjU1MTkgQUFBQUMzTnphQzFsWkRJMU5URTVBQUFBSUpZZnFFU2FpUWxPckwzV20xUTlzOXE4YjRtamoybkl1eXFDWnViNWFHUGkgbml4QHNjcmF0Y2h5".to_string());
        }

        #[test]
        fn both_float_formats() {
            let machines = gm(r#"
                [machines.andesite]
                uri = "ssh://lix@andesite.lix.systems"
                speed-factor = 3
            "#)
            .unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.speed_factor, 3.0);

            let machines = gm(r#"
                [machines.diorite]
                uri = "ssh://lix@diorite.lix.systems"
                speed-factor = 3.1
            "#)
            .unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.speed_factor, 3.1);
        }

        #[test]
        fn multi_options() {
            let machines = gm(r#"
                [machines.scratchy]
                uri = "nix@scratchy.labs.cs.uu.nl"
                system-types = ["Arch1", "Arch2"]
                supported-features = ["SF1", "SF2"]
                mandatory-features = ["MF1", "MF2"]
            "#)
            .unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(
                m.system_types,
                HashSet::from(["Arch1".to_string(), "Arch2".to_string()])
            );
            assert_eq!(
                m.supported_features,
                HashSet::from(["SF1".to_string(), "SF2".to_string()])
            );
            assert_eq!(
                m.mandatory_features,
                HashSet::from(["MF1".to_string(), "MF2".to_string()])
            );
        }

        #[test]
        fn extra_keys() {
            let err = gm(r#"
                [machines.andesite]
                uri = "ssh://lix@andesite.lix.systems"
                extra-key = 3
            "#)
            .unwrap_err()
            .to_string();
            assert!(err.contains("unknown field `extra-key`"))
        }

        #[test]
        fn no_quotation_uri() {
            let err = gm(r#"
                [machines.invalid_syntax]
                uri = ssh://lix@andesite.lix.systems
                max-jobs = -3
            "#)
            .unwrap_err()
            .to_string();
            assert!(err.contains("string values must be quoted, expected literal string"));
        }

        #[test]
        fn missing_uri() {
            let err = gm("[machines.a]").unwrap_err().to_string();
            assert!(err.contains("uri must be present and non-empty"));
        }

        #[test]
        fn incorrect_typing() {
            let err = gm(r#"
                [machines.scratchy]
                uri = "nix@scratchy.labs.cs.uu.nl"
                jobs = -3
            "#)
            .unwrap_err()
            .to_string();
            assert!(err.contains("invalid value: integer `-3`, expected u32"));

            let err = gm(r#"
                [machines.scratchy]
                uri = "nix@scratchy.labs.cs.uu.nl"
                jobs = "three"
            "#)
            .unwrap_err()
            .to_string();
            assert!(err.contains("invalid type: string"));

            let err = gm(r#"
                [[machines]]
                uri = "lix@andesite.lix.systems"
                [[machines]]
                uri = "lix@diorite.lix.systems"
            "#)
            .unwrap_err()
            .to_string();
            assert!(err.contains("invalid type: sequence, expected a map"));

            let err = gm(r#"
            version = 1
                [machines.legacy]
                uri = "nix@scratchy.labs.cs.uu.nl"
                enable = 0
            "#)
            .unwrap_err()
            .to_string();
            assert!(err.contains("invalid type: integer `0`, expected a boolean"));
        }

        #[test]
        fn bad_version() {
            let err = gm(r#"
                version = "hello"
                machines = {}
            "#)
            .unwrap_err()
            .to_string();

            assert!(err.contains("invalid type: string \"hello\", expected u16"))
        }

        #[test]
        fn too_high_version() {
            let err = gm(r#"
                version = 42
                machines = {}
            "#)
            .unwrap_err()
            .to_string();

            assert!(err.contains(
            "Unable to parse Machines of version 42, only versions between 1 and 1 are supported."
        ));
        }

        #[test]
        fn too_low_version() {
            let err = gm(r#"
                version = 0
                machines = {}
            "#)
            .unwrap_err()
            .to_string();

            assert!(err.contains(
            "Unable to parse Machines of version 0, only versions between 1 and 1 are supported."
        ));
        }

        #[test]
        fn one_disabled() {
            let machines = gm(r#"
                version = 1
                [machines.a]
                uri = "ssh://test"
                enable = false
                [machines.b]
                uri = "ssh://test2"
            "#)
            .unwrap();

            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.uri, "ssh://test2".to_string());
        }

        #[test]
        fn bad_file() {
            let err = gm("@/this/file/does/not/exist").unwrap_err().to_string();
            assert!(err.contains("while reading external machines file"));
            assert!(err.contains("No such file or directory"));
        }
    }
    mod legacy {
        use crate::generated::parseBuilderLines;
        use crate::machines::Machine;
        use std::collections::HashSet;

        fn parse(line: &str) -> Result<Vec<Machine>, String> {
            parseBuilderLines(line, "TEST_ARCH-TEST_OS")
        }

        #[test]
        fn empty_builders() {
            let machines = parse("").unwrap();
            assert_eq!(machines, vec![]);
        }

        #[test]
        fn uri_only() {
            let machines = parse("nix@scratchy.labs.cs.uu.nl").unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.uri, "ssh://nix@scratchy.labs.cs.uu.nl".to_string());
            assert_eq!(
                m.system_types,
                HashSet::from(["TEST_ARCH-TEST_OS".to_string()])
            );
            assert_eq!(m.ssh_key, "".to_string());
            assert_eq!(m.max_jobs, 1);
            assert_eq!(m.speed_factor, 1.0);
            assert_eq!(m.supported_features.len(), 0);
            assert_eq!(m.mandatory_features.len(), 0);
            assert_eq!(m.ssh_public_host_key, "".to_string());
        }

        #[test]
        fn defaults() {
            let machines = parse("nix@scratchy.labs.cs.uu.nl - - - - - - -").unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.uri, "ssh://nix@scratchy.labs.cs.uu.nl".to_string());
            assert_eq!(
                m.system_types,
                HashSet::from(["TEST_ARCH-TEST_OS".to_string()])
            );
            assert_eq!(m.ssh_key, "".to_string());
            assert_eq!(m.max_jobs, 1);
            assert_eq!(m.speed_factor, 1.0);
            assert_eq!(m.supported_features.len(), 0);
            assert_eq!(m.mandatory_features.len(), 0);
            assert_eq!(m.ssh_public_host_key, "".to_string());
        }

        #[test]
        fn newline_sep() {
            let machines = parse("nix@scratchy.labs.cs.uu.nl\nnix@itchy.labs.cs.uu.nl").unwrap();
            assert_eq!(machines.len(), 2);
            let uris = machines.iter().map(|m| m.uri.as_str()).collect::<Vec<_>>();
            assert!(uris.contains(&"ssh://nix@scratchy.labs.cs.uu.nl"));
            assert!(uris.contains(&"ssh://nix@itchy.labs.cs.uu.nl"));
        }

        #[test]
        fn semicolon_sep() {
            let machines = parse("nix@scratchy.labs.cs.uu.nl ; nix@itchy.labs.cs.uu.nl").unwrap();
            assert_eq!(machines.len(), 2);
            let uris = machines.iter().map(|m| m.uri.as_str()).collect::<Vec<_>>();
            assert!(uris.contains(&"ssh://nix@scratchy.labs.cs.uu.nl"));
            assert!(uris.contains(&"ssh://nix@itchy.labs.cs.uu.nl"));
        }

        #[test]
        fn complete_builder() {
            let machines = parse("nix@scratchy.labs.cs.uu.nl     i686-linux      /home/nix/.ssh/id_scratchy_auto        8 3 kvm benchmark SSH+HOST+PUBLIC+KEY+BASE64+ENCODED==").unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.uri.as_str(), "ssh://nix@scratchy.labs.cs.uu.nl");
            assert_eq!(m.system_types, HashSet::from(["i686-linux".to_string()]));
            assert_eq!(m.ssh_key, "/home/nix/.ssh/id_scratchy_auto".to_string());
            assert_eq!(m.max_jobs, 8);
            assert_eq!(m.speed_factor, 3.0);
            assert_eq!(m.supported_features, HashSet::from(["kvm".to_string()]));
            assert_eq!(
                m.mandatory_features,
                HashSet::from(["benchmark".to_string()])
            );
            assert_eq!(
                m.ssh_public_host_key.as_str(),
                "SSH+HOST+PUBLIC+KEY+BASE64+ENCODED=="
            );
        }

        #[test]
        fn complete_builder_tab_column() {
            let machines = parse("nix@scratchy.labs.cs.uu.nl\ti686-linux\t/home/nix/.ssh/id_scratchy_auto\t8\t3\tkvm\tbenchmark\tSSH+HOST+PUBLIC+KEY+BASE64+ENCODED==").unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.uri.as_str(), "ssh://nix@scratchy.labs.cs.uu.nl");
            assert_eq!(m.system_types, HashSet::from(["i686-linux".to_string()]));
            assert_eq!(m.ssh_key, "/home/nix/.ssh/id_scratchy_auto".to_string());
            assert_eq!(m.max_jobs, 8);
            assert_eq!(m.speed_factor, 3.0);
            assert_eq!(m.supported_features, HashSet::from(["kvm".to_string()]));
            assert_eq!(
                m.mandatory_features,
                HashSet::from(["benchmark".to_string()])
            );
            assert_eq!(
                m.ssh_public_host_key.as_str(),
                "SSH+HOST+PUBLIC+KEY+BASE64+ENCODED=="
            );
        }

        #[test]
        fn multi_options() {
            let machines =
                parse("nix@scratchy.labs.cs.uu.nl Arch1,Arch2 - - - SF1,SF2 MF1,MF2").unwrap();
            assert_eq!(machines.len(), 1);
            let m = machines.get(0).unwrap();
            assert_eq!(m.uri.as_str(), "ssh://nix@scratchy.labs.cs.uu.nl");
            assert_eq!(
                m.system_types,
                HashSet::from(["Arch1".to_string(), "Arch2".to_string()])
            );
            assert_eq!(
                m.supported_features,
                HashSet::from(["SF1".to_string(), "SF2".to_string()])
            );
            assert_eq!(
                m.mandatory_features,
                HashSet::from(["MF1".to_string(), "MF2".to_string()])
            );
        }

        #[test]
        fn incorrect_formatting() {
            let err = parse("nix@scratchy.labs.cs.uu.nl - - eight").unwrap_err();
            assert!(err.contains("FormatError"));
            assert!(err.contains("failed to convert column"));
            assert!(err.contains("eight"));
            assert!(err.contains("to 'unsigned int'"));
            let err = parse("nix@scratchy.labs.cs.uu.nl - - -1").unwrap_err();
            assert!(err.contains("FormatError"));
            assert!(err.contains("failed to convert column"));
            assert!(err.contains("-1"));
            assert!(err.contains("to 'unsigned int'"));
            let err = parse("nix@scratchy.labs.cs.uu.nl - - 8 three").unwrap_err();
            assert!(err.contains("FormatError"));
            assert!(err.contains("failed to convert column"));
            assert!(err.contains("three"));
            assert!(err.contains("to 'float'"));
            let err = parse("nix@scratchy.labs.cs.uu.nl - - 8 -3").unwrap_err();
            assert!(err.contains("UsageError"));
            assert!(err.contains("speed factor must be >= 0"));
            let err = parse("nix@scratchy.labs.cs.uu.nl - - 8 3 - - BAD_BASE64").unwrap_err();
            assert!(err.contains("FormatError"));
            assert!(err.contains("is not valid base64"));
        }

        #[test]
        fn empty_file_ref() {
            let machines = parse("@/dev/null").unwrap();
            assert_eq!(machines.len(), 0);
        }

        #[test]
        fn bad_file() {
            let machines = parse("@/this/does/not/exist/at/all").unwrap();
            assert_eq!(machines.len(), 0);
        }
    }
}
