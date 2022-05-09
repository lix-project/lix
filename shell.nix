{ pkgs ? (
    let
      inherit (builtins) fromJSON readFile;
      flakeLock = fromJSON (readFile ./flake.lock);
      locked = flakeLock.nodes.nixpkgs.locked;
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

in
(pkgs.callPackage ./default.nix {
  inherit srcDir nix;
}).overrideAttrs (old: {

  src = null;

  nativeBuildInputs = old.nativeBuildInputs ++ [

    pkgs.treefmt
    pkgs.llvmPackages.clang # clang-format
    pkgs.nixpkgs-fmt
    pkgs.nodePackages.prettier

    (pkgs.python3.withPackages (ps: [
      ps.pytest
      ps.black
    ]))

  ];

  NODE_PATH = "${pkgs.nodePackages.prettier-plugin-toml}/lib/node_modules";

  shellHook = lib.optionalString stdenv.isLinux ''
    export NIX_DEBUG_INFO_DIRS="${pkgs.curl.debug}/lib/debug:${nix.debug}/lib/debug''${NIX_DEBUG_INFO_DIRS:+:$NIX_DEBUG_INFO_DIRS}"
  '';
})
