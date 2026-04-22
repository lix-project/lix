let
  inherit (import ./config.nix) mkDerivation;
  strangeLog =
    logCommands:
    mkDerivation {
      name = "unusual-logging";
      buildCommand = ''
        {
          ${logCommands}
        } >&$NIX_LOG_FD
        touch $out
      '';
    };

  makeBadLog = json: "echo '@nix ${json}'";
  makeBadLogs = logs: builtins.concatStringsSep "\n" (builtins.map makeBadLog logs);
in
rec {
  normalInvalid = strangeLog (makeBadLogs normalInvalidLogs);
  normalInvalidLogs = [
    "1"
    "{}"
    ''{"action": null}''
    ''{"action": 123}''
    "]["
  ];

  invalidFields = strangeLog (makeBadLog invalidFieldsLog);
  invalidFieldsLog = ''{"action": "start", "fields": [1.5], "id": 2, "type": 1, "level": 1, "text": "abc"}'';
}
