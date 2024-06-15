{ hydraJobs, pkgs }:
let
  inherit (pkgs) lib;
  lix = hydraJobs.build.x86_64-linux;

  # This is all so clumsy because we can't use arguments to functions in
  # flakes, and certainly not with n-e-j.
  profiles = {
    # Used for testing
    x86_64-linux-only = {
      systems = [ "x86_64-linux" ];
      dockerSystems = [ "x86_64-linux" ];
    };
    all = {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-darwin"
      ];
      dockerSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
    };
  };

  doTarball =
    {
      target,
      targetName,
      rename ? null,
    }:
    ''
      echo "doing: ${target}"
      # expand wildcard
      filename=$(echo ${target}/${targetName})
      basename="$(basename $filename)"

      echo $filename $basename
      cp -v "$filename" "$out"
      ${lib.optionalString (rename != null) ''
        mv "$out/$basename" "$out/${rename}"
        basename="${rename}"
      ''}
      sha256sum --binary $filename | cut -f1 -d' ' > $out/$basename.sha256
    '';

  targetsFor =
    { systems, dockerSystems }:
    builtins.map (system: {
      target = hydraJobs.binaryTarball.${system};
      targetName = "*.tar.xz";
    }) systems
    ++ builtins.map (system: {
      target = hydraJobs.dockerImage.${system}.tarball;
      targetName = "image.tar.gz";
      rename = "lix-${lix.version}-docker-image-${system}.tar.gz";
    }) dockerSystems;

  manualTar = pkgs.runCommand "lix-manual-tarball" { } ''
    mkdir -p $out
    cp -r ${lix.doc}/share/doc/nix/manual lix-${lix.version}-manual
    tar -cvzf "$out/lix-${lix.version}-manual.tar.gz" lix-${lix.version}-manual
  '';

  tarballsFor =
    { systems, dockerSystems }:
    pkgs.runCommand "lix-release-tarballs" { } ''
      mkdir -p $out
      ${lib.concatMapStringsSep "\n" doTarball (targetsFor {
        inherit systems dockerSystems;
      })}
      ${doTarball {
        target = manualTar;
        targetName = "lix-*.tar.gz";
      }}
      cp -r ${lix.doc}/share/doc/nix/manual $out
    '';
in
(builtins.mapAttrs (
  _:
  { systems, dockerSystems }:
  {
    build = lib.filterAttrs (x: _: builtins.elem x systems) hydraJobs.build;
    tarballs = tarballsFor { inherit systems dockerSystems; };
  }
) profiles)
// {
  inherit (hydraJobs) build;
  inherit tarballsFor;
}
