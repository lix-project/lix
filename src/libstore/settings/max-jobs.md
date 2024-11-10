---
name: max-jobs
internalName: maxBuildJobs
settingType: MaxBuildJobsSetting
default: 1
aliases: [build-max-jobs]
---
This option defines the maximum number of jobs that Lix will try to
build in parallel. The default is `1`. The special value `auto`
causes Lix to use the number of CPUs in your system. `0` is useful
when using remote builders to prevent any local builds (except for
`preferLocalBuild` derivation attribute which executes locally
regardless). It can be overridden using the `--max-jobs` (`-j`)
command line switch.
