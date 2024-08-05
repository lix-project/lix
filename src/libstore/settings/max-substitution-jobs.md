---
name: max-substitution-jobs
internalName: maxSubstitutionJobs
type: unsigned int
default: 16
aliases: [substitution-max-jobs]
---
This option defines the maximum number of substitution jobs that Nix
will try to run in parallel. The default is `16`. The minimum value
one can choose is `1` and lower values will be interpreted as `1`.
