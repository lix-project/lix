{ pkgs, ... }: {
  # Used to find the project root
  projectRootFile = "flake.lock";

  programs.deno.enable = true;

  programs.clang-format.enable = true;

  settings.formatter = {
    nix = {
      command = "sh";
      options = [
        "-eucx"
        ''
          ${pkgs.deadnix}/bin/deadnix --edit "$@"

          for i in "$@"; do
            ${pkgs.statix}/bin/statix fix "$i"
          done

          ${pkgs.nixpkgs-fmt}/bin/nixpkgs-fmt "$@"
        ''
        "--"
      ];
      includes = [ "*.nix" ];
      excludes = [ ];
    };

    clang-format = { };

    python = {
      command = "sh";
      options = [
        "-eucx"
        ''
          ${pkgs.python3.pkgs.black}/bin/black "$@"
          ${pkgs.ruff}/bin/ruff --fix "$@"
        ''
        "--" # this argument is ignored by bash
      ];
      includes = [ "*.py" ];
    };
  };
}
