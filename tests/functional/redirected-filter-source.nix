{ path }:
let
  pathStr = builtins.toString path;
in
builtins.path {
  path = builtins.toString path;
  name = "src";
  filter =
    fullPath: type:
    let
      obtained = builtins.substring 0 (builtins.stringLength pathStr) fullPath;
    in
    if obtained == pathStr then
      true
    else
      builtins.throw "bug detected, expected: ${pathStr}, got: ${obtained}";
}
