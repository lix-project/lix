{
    outputs = {self}: {
      packages.@system@.pkgAsPkg = (import ./shell-hello.nix).hello;
      packages.@system@.pkgAsApp = self.apps.@system@.appAsApp;

      apps.@system@.appAsPkg = self.packages.@system@.pkgAsPkg;
      apps.@system@.appAsApp = {
        type = "app";
        program = "${(import ./shell-hello.nix).hello}/bin/hello";
      };
    };
}
