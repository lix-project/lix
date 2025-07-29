{
  /**
    Path to Lix's source, normally the flake's "self" argument
  */
  self ? pkgs.lib.cleanSource ./.,
  /**
    Already instantiated Nixpkgs
  */
  pkgs,
  /**
    pre-commit-hooks source path, normally from the flake input
  */
  pre-commit-hooks,
}:
let
  inherit (pkgs) lib;
  # Import pre-commit bypassing the flake because flakes don't let
  # you have overlays. Also their implementation forces an
  # unnecessary reimport of nixpkgs for our use cases.
  tools = import (pre-commit-hooks + "/nix/call-tools.nix") pkgs;
  pre-commit-run = pkgs.callPackage (pre-commit-hooks + "/nix/run.nix") {
    inherit tools;
    isFlakes = true;
    # unused!
    gitignore-nix-src = builtins.throw "gitignore-nix-src is unused";
  };
in
pre-commit-run {
  src = self;
  hooks = {
    no-commit-to-branch = {
      enable = true;
      settings.branch = [ "main" ];
    };
    clang-format = {
      enable = true;
      package = pkgs.llvmPackages.libclang.python;
      entry = "${pkgs.llvmPackages.libclang.python}/bin/git-clang-format --binary ${pkgs.llvmPackages.clang-tools}/bin/clang-format";
      files = "^(lix/|tests/)";
      types = [
        "c++"
        "file"
      ];
      excludes = [
        ''^lix/pch/.*$''
        ''\.gen\.hh$''
      ];
      pass_filenames = false; # this will automatically format everything in the stage area of Git.
    };
    check-case-conflicts.enable = true;
    check-executables-have-shebangs = {
      enable = true;
      stages = [ "pre-commit" ];
    };
    check-shebang-scripts-are-executable = {
      enable = true;
      stages = [ "pre-commit" ];
    };
    check-symlinks = {
      enable = true;
      excludes = [ "^tests/functional/lang/symlink-resolution/broken$" ];
    };
    check-merge-conflicts.enable = true;
    end-of-file-fixer = {
      enable = true;
      excludes = [
        "\\.drv$"
        "^tests/functional/lang/"
        ''\.patch$''
        "^tests/functional2/.+\\.exp"
        "^tests/functional2/.+\\.nix"
      ];
    };
    mixed-line-endings = {
      enable = true;
      excludes = [
        "^tests/functional/lang/"
        "^tests/functional2/.+\\.exp"
        "^tests/functional2/.+\\.nix"
      ];
    };
    release-notes = {
      enable = true;
      package = pkgs.build-release-notes;
      files = ''^doc/manual/(change-authors\.yml|rl-next(-dev)?)'';
      pass_filenames = false;
      entry = ''
        ${lib.getExe pkgs.build-release-notes} --change-authors doc/manual/change-authors.yml doc/manual/rl-next
      '';
    };
    change-authors-sorted = {
      enable = true;
      package = pkgs.yq;
      files = ''^doc/manual/change-authors\.yml'';
      entry = "${pkgs.writeShellScript "change-authors-sorted" ''
        set -euo pipefail
        shopt -s inherit_errexit

        echo "changes necessary to sort $1:"
        diff -U3 <(${lib.getExe pkgs.yq} -y . "$1") <(${lib.getExe pkgs.yq} -Sy . "$1")
      ''}";
    };
    check-headers = {
      enable = true;
      package = pkgs.check-headers;
      files = "^(lix/|tests/)";
      types = [
        "c++"
        "file"
        "header"
      ];
      excludes = [
        ''^lix/pch/.*$''
        # generated files; these will never actually be seen by this
        # check, and are left here as documentation
        ''(parser|lexer)-tab\.hh$''
        ''\.gen\.hh$''
      ];
      entry = lib.getExe pkgs.check-headers;
    };
    keep-sorted = {
      enable = true;
      package = pkgs.keep-sorted;
      entry = lib.getExe pkgs.keep-sorted;
    };
    # TODO: Once the test suite is nicer, clean up and start
    # enforcing trailing whitespace on tests that don't explicitly
    # check for it.
    trim-trailing-whitespace = {
      enable = true;
      stages = [ "pre-commit" ];
      excludes = [
        ''^tests/functional/lang/''
        "^tests/functional2/.+\\.exp"
        "^tests/functional2/.+\\.nix"
        ''\.patch$''
      ];
    };
    treefmt = {
      enable = true;
      settings.formatters = [
        pkgs.nixfmt-rfc-style
        pkgs.ruff
      ];
    };
  };
}
