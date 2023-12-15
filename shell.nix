{ pkgs ? (
    let
      inherit (builtins) fromJSON readFile;
      flakeLock = fromJSON (readFile ./flake.lock);
      inherit (flakeLock.nodes.nixpkgs) locked;
      nixpkgs = assert locked.type == "github"; builtins.fetchTarball {
        url = "https://github.com/${locked.owner}/${locked.repo}/archive/${locked.rev}.tar.gz";
        sha256 = locked.narHash;
      };
    in
    import nixpkgs { }
  )
, srcDir ? null
, nix
}:

let
  inherit (pkgs) lib stdenv;
  nix-eval-jobs = pkgs.callPackage ./default.nix {
    inherit srcDir nix;
  };
in
pkgs.mkShell {
  inherit (nix-eval-jobs) buildInputs;
  nativeBuildInputs = nix-eval-jobs.nativeBuildInputs ++ [
    (pkgs.python3.withPackages (ps: [
      ps.pytest
    ]))
  ] ++ lib.optional stdenv.isLinux # broken on darwin
    (pkgs.writeShellScriptBin "update-include-what-you-use" ''
      #!${pkgs.stdenv.shell}
      export PATH=${pkgs.include-what-you-use}/bin:$PATH
      find src -type f -name '*.cpp' -o -name '*.hh' -print0 | \
        xargs -n1 --null include-what-you-use -std=c++20 -isystem ${lib.getDev nix}/include/nix 2>&1 | \
        fix_includes.py
    '');

  shellHook = lib.optionalString stdenv.isLinux ''
    export NIX_DEBUG_INFO_DIRS="${pkgs.curl.debug}/lib/debug:${nix.debug}/lib/debug''${NIX_DEBUG_INFO_DIRS:+:$NIX_DEBUG_INFO_DIRS}"
  '';
}
