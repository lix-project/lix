{
  nix_2_18,
  lib,
  versionSuffix,
  officialRelease,
}:
getStdenv: final: prev:
let
  currentStdenv = getStdenv final;
  inherit (final) busybox;

  # These packages should receive CppNix since they may link to it or otherwise
  # cause problems (or even just silly mass-rebuilds) if we give them Lix
  overridelist_upstream = [
    "nix-du"
    "nix-init"
    "nurl"
  ];

  # It is not enough to *just* throw whatever the default nix version is at
  # anything in the "don't give lix" list, we have to *also* ensure that we
  # give whatever upstream version as specified in the callPackage invocation.
  #
  # Unfortunately I don't think there is any actual way to directly query that,
  # so we instead do something extremely evil and guess which version it
  # probably was. This code is not generalizable to arbitrary derivations, so
  # it will hopefully not make us cry, at least.
  useCppNixOverlay = lib.genAttrs overridelist_upstream (
    name:
    if (lib.functionArgs prev.${name}.override ? "nix") then
      let
        # Get the two common inputs of a derivation/package.
        inputs = prev.${name}.buildInputs ++ prev.${name}.nativeBuildInputs;
        nixDependency =
          lib.findFirst (drv: (drv.pname or "") == "nix") final.nixVersions.stable_upstream # default to stable nix if nix is not an input
            inputs;
        nixMajor = lib.versions.major (nixDependency.version or "");
        nixMinor = lib.versions.minor (nixDependency.version or "");
        nixAttr = "nix_${nixMajor}_${nixMinor}";
        finalNix = final.nixVersions.${nixAttr};
      in
      prev.${name}.override {
        nix = finalNix;
      }
    else
      prev.${name}
  );
in
useCppNixOverlay
// {
  nixStable = prev.nix;

  nixVersions = prev.nixVersions // {
    # Nix 2.18 has been removed from Nixpkgs ≥ 25.05, so we need to reintroduce it ourselves for our tests.
    nix_2_18 =
      (import nix_2_18).packages.${currentStdenv.hostPlatform.system}.default.overrideAttrs
        (_: {
          pname = "nix";
        });

    stable = final.lix;
    stable_upstream = prev.nixVersions.stable;
  };

  __lixInternal = final.makeScopeWithSplicing' {
    otherSplices = final.generateSplicesForMkScope [ "__lixInternal" ];
    f = final: {
      check-headers = final.callPackage ../../maintainers/check-headers.nix { };
      check-syscalls = final.callPackage ../../maintainers/check-syscalls.nix { };

      default-busybox-sandbox-shell = busybox.override {
        useMusl = true;
        enableStatic = true;
        enableMinimal = true;
        extraConfig = ''
          CONFIG_FEATURE_FANCY_ECHO y
          CONFIG_FEATURE_SH_MATH y
          CONFIG_FEATURE_SH_MATH_64 y

          CONFIG_ASH y
          CONFIG_ASH_OPTIMIZE_FOR_SIZE y

          CONFIG_ASH_ALIAS y
          CONFIG_ASH_BASH_COMPAT y
          CONFIG_ASH_CMDCMD y
          CONFIG_ASH_ECHO y
          CONFIG_ASH_GETOPTS y
          CONFIG_ASH_INTERNAL_GLOB y
          CONFIG_ASH_JOB_CONTROL y
          CONFIG_ASH_PRINTF y
          CONFIG_ASH_TEST y
        '';
      };

      nix = final.callPackage ../../package.nix {
        inherit versionSuffix officialRelease;
        stdenv = currentStdenv;
        busybox-sandbox-shell = final.busybox-sandbox-shell or final.default-busybox-sandbox-shell;
        # See below
        lowdown = final.lowdown_3_0;
        lowdown-unsandboxed = final.lowdown_3_0.override { enableDarwinSandbox = false; };
      };

      lix-clang-tidy = final.callPackage ../../subprojects/lix-clang-tidy { };

      nix-eval-jobs = final.callPackage ../../subprojects/nix-eval-jobs {
        stdenv = currentStdenv;
        srcDir = ../../subprojects/nix-eval-jobs;
      };

      # HACK: We need nix-prefetch-git for fetchCargoVendor for Rust stuff,
      # so it can't use Lix, or we infrec:
      #   lix -> Rust stuff -> fetchCargoVendor -> nix-prefetch-git -> nix (lix)
      # This will eventually become a problem upstream, but until then,
      # apply some duct tape and pray.
      nix-prefetch-git =
        if (lib.functionArgs prev.nix-prefetch-git.override) ? "nix" then
          prev.nix-prefetch-git.override { nix = prev.nix; }
        else
          prev.nix-prefetch-git;

      # Export the patched version of boehmgc that Lix uses into the overlay
      # for consumers of this flake.
      boehmgc-nix = final.nix.passthru.boehmgc-nix;
      # And same thing for our build-release-notes package.
      build-release-notes = final.nix.passthru.build-release-notes;

      # As soon as Nixpkgs updates to >= 3.0.0, change to lowdown_2_0!
      # We don't change the default version in order to not change the hash
      # of Nix/Lix from upstream Nixpkgs.
      lowdown_3_0 =
        if (lib.versions.major prev.lowdown.version == "3") then
          prev.lowdown
        else
          prev.lowdown.overrideAttrs (
            finalAttrs: _prevAttrs: {
              version = "3.0.0";

              src = final.fetchurl {
                url = "https://kristaps.bsd.lv/lowdown/snapshots/lowdown-${finalAttrs.version}.tar.gz";
                sha512 = "94e97234d598382c3c3dc27f9bfdb3a3a2fcf7dbb6a8df3c85ee09f27f792449034a41d49d9cfd3d8450d2de01b8562c20c3d120e65c81af4d7d6c9454119e93";
              };
            }
          );

      capnproto = prev.capnproto.overrideAttrs (old: {
        patches =
          old.patches or [ ]
          ++ [
            # backport of https://github.com/capnproto/capnproto/pull/1810
            ../../misc/capnproto-promise-nodiscard.patch
          ]
          ++ lib.optionals (lib.versionOlder old.version "1.2.0") [
            # backport of https://github.com/capnproto/capnproto/pull/2296
            ../../misc/capnproto-monotonic-clocks-are-a-lie.patch
          ];
      });
    };
  };

  inherit (final.__lixInternal) nix-eval-jobs nix;
  lix = final.__lixInternal.nix;

  nix-doc = prev.nix-doc.override { withPlugin = false; };
}
