{
  pkgs,
  lib,
  stdenv,
  aws-sdk-cpp,
  # If the patched version of Boehm isn't passed, then patch it based off of
  # pkgs.boehmgc. This allows `callPackage`ing this file without needing to
  # to implement behavior that this package flat out doesn't build without
  # anyway, but also allows easily overriding the patch logic.
  boehmgc-nix ? __forDefaults.boehmgc-nix,
  boehmgc,
  nlohmann_json,
  bison,
  build-release-notes ? __forDefaults.build-release-notes,
  boost,
  brotli,
  bzip2,
  cmake,
  curl,
  doxygen,
  editline-lix ? __forDefaults.editline-lix,
  editline,
  flex,
  git,
  gtest,
  jq,
  libarchive,
  libcpuid,
  libseccomp,
  libsodium,
  lsof,
  lowdown,
  mdbook,
  mdbook-linkcheck,
  mercurial,
  meson,
  ninja,
  openssl,
  pkg-config,
  python3,
  rapidcheck,
  sqlite,
  toml11,
  util-linuxMinimal ? utillinuxMinimal,
  utillinuxMinimal ? null,
  xz,

  busybox-sandbox-shell,

  # internal fork of nix-doc providing :doc in the repl
  lix-doc ? __forDefaults.lix-doc,

  pname ? "lix",
  versionSuffix ? "",
  officialRelease ? false,
  # Set to true to build the release notes for the next release.
  buildUnreleasedNotes ? true,
  internalApiDocs ? false,

  # Not a real argument, just the only way to approximate let-binding some
  # stuff for argument defaults.
  __forDefaults ? {
    canRunInstalled = stdenv.buildPlatform.canExecute stdenv.hostPlatform;

    boehmgc-nix = (boehmgc.override { enableLargeConfig = true; }).overrideAttrs {
      patches = [
        # We do *not* include prev.patches (which doesn't exist in normal pkgs.boehmgc anyway)
        # because if the caller of this package passed a patched boehm as `boehmgc` instead of
        # `boehmgc-nix` then this will almost certainly have duplicate patches, which means
        # the patches won't apply and we'll get a build failure.
        ./boehmgc-coroutine-sp-fallback.diff
      ];
    };

    editline-lix = editline.overrideAttrs (prev: {
      configureFlags = prev.configureFlags or [ ] ++ [ (lib.enableFeature true "sigstop") ];
    });

    lix-doc = pkgs.callPackage ./lix-doc/package.nix { };
    build-release-notes = pkgs.callPackage ./maintainers/build-release-notes.nix { };
  },
}:
let
  inherit (__forDefaults) canRunInstalled;
  inherit (lib) fileset;
  inherit (stdenv) hostPlatform buildPlatform;

  versionJson = builtins.fromJSON (builtins.readFile ./version.json);
  version = versionJson.version + versionSuffix;

  aws-sdk-cpp-nix = aws-sdk-cpp.override {
    apis = [
      "s3"
      "transfer"
    ];
    customMemoryManagement = false;
  };

  # Reimplementation of Nixpkgs' Meson cross file, with some additions to make
  # it actually work.
  mesonCrossFile =
    let
      cpuFamily =
        platform:
        with platform;
        if isAarch32 then
          "arm"
        else if isx86_32 then
          "x86"
        else
          platform.uname.processor;
    in
    builtins.toFile "lix-cross-file.conf" ''
      [properties]
      # Meson is convinced that if !buildPlatform.canExecute hostPlatform then we cannot
      # build anything at all, which is not at all correct. If we can't execute the host
      # platform, we'll just disable tests and doc gen.
      needs_exe_wrapper = false

      [binaries]
      # Meson refuses to consider any CMake binary during cross compilation if it's
      # not explicitly specified here, in the cross file.
      # https://github.com/mesonbuild/meson/blob/0ed78cf6fa6d87c0738f67ae43525e661b50a8a2/mesonbuild/cmake/executor.py#L72
      cmake = 'cmake'
    '';

  # The internal API docs need these for the build, but if we're not building
  # Nix itself, then these don't need to be propagated.
  maybePropagatedInputs = [
    boehmgc-nix
    nlohmann_json
  ];

  # .gitignore has already been processed, so any changes in it are irrelevant
  # at this point. It is not represented verbatim for test purposes because
  # that would interfere with repo semantics.
  baseFiles = fileset.fileFilter (f: f.name != ".gitignore") ./.;

  configureFiles = fileset.unions [ ./version.json ];

  topLevelBuildFiles = fileset.unions ([
    ./meson.build
    ./meson.options
    ./meson
    ./scripts/meson.build
    ./subprojects
  ]);

  functionalTestFiles = fileset.unions [
    ./tests/functional
    ./tests/unit
    (fileset.fileFilter (f: lib.strings.hasPrefix "nix-profile" f.name) ./scripts)
  ];
in
stdenv.mkDerivation (finalAttrs: {
  inherit pname version;

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.intersection baseFiles (
      fileset.unions (
        [
          configureFiles
          topLevelBuildFiles
          functionalTestFiles
        ]
        ++ lib.optionals (!finalAttrs.dontBuild || internalApiDocs) [
          ./boehmgc-coroutine-sp-fallback.diff
          ./doc
          ./misc
          ./src
          ./COPYING
        ]
      )
    );
  };

  VERSION_SUFFIX = versionSuffix;

  outputs =
    [ "out" ]
    ++ lib.optionals (!finalAttrs.dontBuild) [
      "dev"
      "doc"
    ];

  dontBuild = false;

  mesonFlags =
    lib.optionals hostPlatform.isLinux [
      # You'd think meson could just find this in PATH, but busybox is in buildInputs,
      # which don't actually get added to PATH. And buildInputs is correct over
      # nativeBuildInputs since this should be a busybox executable on the host.
      "-Dsandbox-shell=${lib.getExe' busybox-sandbox-shell "busybox"}"
    ]
    ++ lib.optional hostPlatform.isStatic "-Denable-embedded-sandbox-shell=true"
    ++ lib.optional (finalAttrs.dontBuild) "-Denable-build=false"
    ++ [
      # mesonConfigurePhase automatically passes -Dauto_features=enabled,
      # so we must explicitly enable or disable features that we are not passing
      # dependencies for.
      (lib.mesonEnable "internal-api-docs" internalApiDocs)
      (lib.mesonBool "enable-tests" finalAttrs.finalPackage.doCheck)
      (lib.mesonBool "enable-docs" canRunInstalled)
    ]
    ++ lib.optional (hostPlatform != buildPlatform) "--cross-file=${mesonCrossFile}";

  # We only include CMake so that Meson can locate toml11, which only ships CMake dependency metadata.
  dontUseCmakeConfigure = true;

  nativeBuildInputs =
    [
      bison
      flex
      python3
      meson
      ninja
      cmake
    ]
    ++ [
      (lib.getBin lowdown)
      mdbook
      mdbook-linkcheck
    ]
    ++ [
      pkg-config

      # Tests
      git
      mercurial
      jq
      lsof
    ]
    ++ lib.optional hostPlatform.isLinux util-linuxMinimal
    ++ lib.optional (!officialRelease && buildUnreleasedNotes) build-release-notes
    ++ lib.optional internalApiDocs doxygen;

  buildInputs =
    [
      curl
      bzip2
      xz
      brotli
      editline-lix
      openssl
      sqlite
      libarchive
      boost
      lowdown
      libsodium
      toml11
      lix-doc
    ]
    ++ lib.optionals hostPlatform.isLinux [
      libseccomp
      busybox-sandbox-shell
    ]
    ++ lib.optional internalApiDocs rapidcheck
    ++ lib.optional hostPlatform.isx86_64 libcpuid
    # There have been issues building these dependencies
    ++ lib.optional (hostPlatform.canExecute buildPlatform) aws-sdk-cpp-nix
    ++ lib.optionals (finalAttrs.dontBuild) maybePropagatedInputs;

  checkInputs = [
    gtest
    rapidcheck
  ];

  propagatedBuildInputs = lib.optionals (!finalAttrs.dontBuild) maybePropagatedInputs;

  disallowedReferences = [ boost ];

  # Needed for Meson to find Boost.
  # https://github.com/NixOS/nixpkgs/issues/86131.
  env = {
    BOOST_INCLUDEDIR = "${lib.getDev boost}/include";
    BOOST_LIBRARYDIR = "${lib.getLib boost}/lib";
  };

  preConfigure =
    lib.optionalString (!finalAttrs.dontBuild && !hostPlatform.isStatic) ''
      # Copy libboost_context so we don't get all of Boost in our closure.
      # https://github.com/NixOS/nixpkgs/issues/45462
      mkdir -p $out/lib
      cp -pd ${boost}/lib/{libboost_context*,libboost_thread*,libboost_system*} $out/lib
      rm -f $out/lib/*.a
    ''
    + lib.optionalString (!finalAttrs.dontBuild && hostPlatform.isLinux && !hostPlatform.isStatic) ''
      chmod u+w $out/lib/*.so.*
      patchelf --set-rpath $out/lib:${stdenv.cc.cc.lib}/lib $out/lib/libboost_thread.so.*
    ''
    + lib.optionalString (!finalAttrs.dontBuild && hostPlatform.isDarwin) ''
      for LIB in $out/lib/*.dylib; do
        chmod u+w $LIB
        install_name_tool -id $LIB $LIB
        install_name_tool -delete_rpath ${boost}/lib/ $LIB || true
      done
      install_name_tool -change ${boost}/lib/libboost_system.dylib $out/lib/libboost_system.dylib $out/lib/libboost_thread.dylib
    ''
    + ''
      # Workaround https://github.com/NixOS/nixpkgs/issues/294890.
      if [[ -n "''${doCheck:-}" ]]; then
        appendToVar configureFlags "--enable-tests"
      else
        appendToVar configureFlags "--disable-tests"
      fi
    '';

  mesonBuildType = "debugoptimized";

  installTargets = lib.optional internalApiDocs "internal-api-html";

  enableParallelBuilding = true;

  doCheck = canRunInstalled;

  mesonCheckFlags = [
    "--suite=check"
    "--print-errorlogs"
  ];
  # the tests access localhost.
  __darwinAllowLocalNetworking = true;

  # Make sure the internal API docs are already built, because mesonInstallPhase
  # won't let us build them there. They would normally be built in buildPhase,
  # but the internal API docs are conventionally built with doBuild = false.
  preInstall = lib.optional internalApiDocs ''
    meson ''${mesonBuildFlags:-} compile "$installTargets"
  '';

  postInstall =
    lib.optionalString (!finalAttrs.dontBuild) ''
      mkdir -p $doc/nix-support
      echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
    ''
    + lib.optionalString hostPlatform.isStatic ''
      mkdir -p $out/nix-support
      echo "file binary-dist $out/bin/nix" >> $out/nix-support/hydra-build-products
    ''
    + lib.optionalString stdenv.isDarwin ''
      for lib in liblixutil.dylib liblixexpr.dylib; do
        install_name_tool \
          -change "${lib.getLib boost}/lib/libboost_context.dylib" \
          "$out/lib/libboost_context.dylib" \
          "$out/lib/$lib"
      done
    ''
    + lib.optionalString internalApiDocs ''
      mkdir -p $out/nix-support
      echo "doc internal-api-docs $out/share/doc/nix/internal-api/html" >> "$out/nix-support/hydra-build-products"
    '';

  doInstallCheck = finalAttrs.doCheck;

  mesonInstallCheckFlags = [
    "--suite=installcheck"
    "--print-errorlogs"
  ];

  installCheckPhase = ''
    runHook preInstallCheck
    flagsArray=($mesonInstallCheckFlags "''${mesonInstallCheckFlagsArray[@]}")
    meson test --no-rebuild "''${flagsArray[@]}"
    runHook postInstallCheck
  '';

  separateDebugInfo = !hostPlatform.isStatic && !finalAttrs.dontBuild;

  strictDeps = true;

  # strictoverflow is disabled because we trap on signed overflow instead
  hardeningDisable = [ "strictoverflow" ] ++ lib.optional hostPlatform.isStatic "pie";

  meta = {
    mainProgram = "nix";
    platforms = lib.platforms.unix;
  };

  passthru.perl-bindings = pkgs.callPackage ./perl { inherit fileset stdenv; };

  # Export the patched version of boehmgc.
  # flake.nix exports that into its overlay.
  passthru = {
    inherit (__forDefaults) boehmgc-nix editline-lix build-release-notes;

    inherit officialRelease;

    # The collection of dependency logic for this derivation is complicated enough that
    # it's easier to parameterize the devShell off an already called package.nix.
    mkDevShell =
      {
        mkShell,
        just,
        nixfmt,
        glibcLocales,
        pre-commit-checks,
        clang-tools,
        llvmPackages,
        clangbuildanalyzer,
        contribNotice,
        skopeo,
        xonsh,
      }:
      let
        glibcFix = lib.optionalAttrs (buildPlatform.isLinux && glibcLocales != null) {
          # Required to make non-NixOS Linux not complain about missing locale files during configure in a dev shell
          LOCALE_ARCHIVE = "${lib.getLib pkgs.glibcLocales}/lib/locale/locale-archive";
        };

        pythonPackages = (
          p: [
            p.yapf
            p.python-frontmatter
            p.requests
            p.xdg-base-dirs
            (p.toPythonModule xonsh.passthru.unwrapped)
          ]
        );
        pythonEnv = python3.withPackages pythonPackages;

        # pkgs.mkShell uses pkgs.stdenv by default, regardless of inputsFrom.
        actualMkShell = mkShell.override { inherit stdenv; };
      in
      actualMkShell (
        glibcFix
        // {

          name = "lix-shell-env";

          # finalPackage is necessary to propagate stuff that is set by mkDerivation itself,
          # like doCheck.
          inputsFrom = [ finalAttrs.finalPackage ];

          # For Meson to find Boost.
          env = finalAttrs.env;

          mesonFlags =
            # I guess this is necessary because mesonFlags to mkDerivation doesn't propagate in inputsFrom,
            # which only propagates stuff set in hooks? idk.
            finalAttrs.mesonFlags
            # Clangd breaks when GCC is using precompiled headers, so for the devshell specifically
            # we make precompiled C++ stdlib conditional on using Clang.
            # https://git.lix.systems/lix-project/lix/issues/374
            ++ [ (lib.mesonBool "enable-pch-std" stdenv.cc.isClang) ];

          packages =
            lib.optional (stdenv.cc.isClang && hostPlatform == buildPlatform) clang-tools
            ++ [
              pythonEnv
              # docker image tool
              skopeo
              just
              nixfmt
              # Load-bearing order. Must come before clang-unwrapped below, but after clang_tools above.
              stdenv.cc
            ]
            ++ lib.optionals stdenv.cc.isClang [
              # Required for clang-tidy checks.
              llvmPackages.llvm
              llvmPackages.clang-unwrapped.dev
            ]
            ++ lib.optional (pre-commit-checks ? enabledPackages) pre-commit-checks.enabledPackages
            ++ lib.optional (lib.meta.availableOn buildPlatform clangbuildanalyzer) clangbuildanalyzer
            ++ finalAttrs.checkInputs;

          shellHook = ''
            # don't re-run the hook in (other) nested nix-shells
            function lixShellHook() {
              # n.b. how the heck does this become -env-env? well, `nix develop` does it:
              # https://git.lix.systems/lix-project/lix/src/commit/7575db522e9008685c4009423398f6900a16bcce/src/nix/develop.cc#L240-L241
              # this is, of course, absurd.
              if [[ $name != lix-shell-env && $name != lix-shell-env-env ]]; then
                return;
              fi

              PATH=$prefix/bin:$PATH
              unset PYTHONPATH
              export MANPATH=$out/share/man:$MANPATH

              # Make bash completion work.
              XDG_DATA_DIRS+=:$out/share

              ${lib.optionalString (pre-commit-checks ? shellHook) pre-commit-checks.shellHook}
              # Allow `touch .nocontribmsg` to turn this notice off.
              if ! [[ -f .nocontribmsg ]]; then
                cat ${contribNotice}
              fi

              # Install the Gerrit commit-msg hook.
              # (git common dir is the main .git, including for worktrees)
              if gitcommondir=$(git rev-parse --git-common-dir 2>/dev/null) && [[ ! -f "$gitcommondir/hooks/commit-msg" ]]; then
                echo 'Installing Gerrit commit-msg hook (adds Change-Id to commit messages)' >&2
                mkdir -p "$gitcommondir/hooks"
                curl -s -Lo "$gitcommondir/hooks/commit-msg" https://gerrit.lix.systems/tools/hooks/commit-msg
                chmod u+x "$gitcommondir/hooks/commit-msg"
              fi
              unset gitcommondir
            }

            lixShellHook
          '';
        }
      );
  };
})
