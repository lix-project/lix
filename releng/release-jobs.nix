{ hydraJobs, pkgs }:
let
  inherit (pkgs) lib;
  lix = hydraJobs.build.x86_64-linux;

  systems = [ "x86_64-linux" ];
  dockerSystems = [ "x86_64-linux" ];

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

  targets =
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

  tarballs = pkgs.runCommand "lix-release-tarballs" { } ''
    mkdir -p $out
    ${lib.concatMapStringsSep "\n" doTarball targets}
    cp ${manualTar}/*.tar.gz $out
    cp -r ${lix.doc}/share/doc/nix/manual $out
  '';
in
{
  inherit (hydraJobs) build;
  inherit tarballs;
}
