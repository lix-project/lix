---
name: log-format
internalName: logFormat
settingType: LogFormatSetting
defaultExpr: 'LogFormat::Auto'
defaultText: auto
---
Set the format of log output; one of `raw`, `internal-json`, `bar`, `bar-with-logs`, `multiline` or `multiline-with-logs`.

For legacy reasons, the default value "auto" makes the actual log format depend on which command you're using.
The legacy `nix-` CLI will use `raw-with-logs` (or `raw` with `-Q`/`--no-build-output`), and `nix3` commands will use `bar-with-logs`.
