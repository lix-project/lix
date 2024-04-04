# Copy of `nixfmt-rfc-style` vendored from `nixpkgs` master:
# https://github.com/NixOS/nixpkgs/blob/ab6071eb54cc9b66dda436111d4f569e4e56cbf4/pkgs/by-name/ni/nixfmt-rfc-style/package.nix
{
  haskell,
  haskellPackages,
  fetchFromGitHub,
}:
let
  inherit (haskell.lib.compose) justStaticExecutables;
  raw-pkg = haskellPackages.callPackage (
    {
      mkDerivation,
      base,
      cmdargs,
      directory,
      fetchzip,
      filepath,
      lib,
      megaparsec,
      mtl,
      parser-combinators,
      safe-exceptions,
      scientific,
      text,
      transformers,
      unix,
    }:
    mkDerivation {
      pname = "nixfmt";
      version = "0.6.0-unstable-2024-03-14";
      src = fetchFromGitHub {
        owner = "serokell";
        repo = "nixfmt";
        rev = "8d13b593fa8d8d6e5075f541f3231222a08e84df";
        hash = "sha256-HtXvzmfN4wk45qiKZ7V+/5WBV7jnTHfd7iBwF4XGl64=";
      };
      isLibrary = true;
      isExecutable = true;
      libraryHaskellDepends = [
        base
        megaparsec
        mtl
        parser-combinators
        scientific
        text
        transformers
      ];
      executableHaskellDepends = [
        base
        cmdargs
        directory
        filepath
        safe-exceptions
        text
        unix
      ];
      jailbreak = true;
      homepage = "https://github.com/serokell/nixfmt";
      description = "An opinionated formatter for Nix";
      license = lib.licenses.mpl20;
      mainProgram = "nixfmt";
    }
  ) { };
in
justStaticExecutables raw-pkg
