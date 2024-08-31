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
  build-release-notes ? __forDefaults.build-release-notes,
  boost,
  brotli,
  bzip2,
  callPackage,
  capnproto-lix ? __forDefaults.capnproto-lix,
  capnproto,
  cmake,
  curl,
  doxygen,
  editline-lix ? __forDefaults.editline-lix,
  editline,
  git,
  gtest,
  jq,
  libarchive,
  libcpuid,
  libseccomp,
  libsodium,
  lix-clang-tidy ? null,
  llvmPackages,
  lsof,
  lowdown,
  mdbook,
  mdbook-linkcheck,
  mercurial,
  meson,
  ninja,
  openssl,
  pegtl,
  pkg-config,
  python3,
  rapidcheck,
  rustPlatform,
  rustc,
  sqlite,
  toml11,
  util-linuxMinimal ? utillinuxMinimal,
  utillinuxMinimal ? null,
  xz,

  busybox-sandbox-shell,

  pname ? "lix",
  versionSuffix ? "",
  officialRelease ? __forDefaults.versionJson.official_release,
  # Set to true to build the release notes for the next release.
  buildUnreleasedNotes ? true,
  internalApiDocs ? false,

  # Support garbage collection in the evaluator.
  enableGC ? sanitize == null || !builtins.elem "address" sanitize,
  # List of Meson sanitize options. Accepts values of b_sanitize, e.g.
  # "address", "undefined", "thread".
  # Enabling the "address" sanitizer will disable garbage collection in the evaluator.
  sanitize ? null,
  # Turn compiler warnings into errors.
  werror ? false,

  lintInsteadOfBuild ? false,

  # Not a real argument, just the only way to approximate let-binding some
  # stuff for argument defaults.
  __forDefaults ? {
    canRunInstalled = stdenv.buildPlatform.canExecute stdenv.hostPlatform;

    versionJson = builtins.fromJSON (builtins.readFile ./version.json);

    boehmgc-nix = boehmgc.override { enableLargeConfig = true; };

    editline-lix = editline.overrideAttrs (prev: {
      configureFlags = prev.configureFlags or [ ] ++ [ (lib.enableFeature true "sigstop") ];
    });

    build-release-notes = callPackage ./maintainers/build-release-notes.nix { };

    # needs explicit c++20 to enable coroutine support
    capnproto-lix = capnproto.overrideAttrs { CXXFLAGS = "-std=c++20"; };
  },
}:
let
  inherit (__forDefaults) canRunInstalled;
  inherit (lib) fileset;
  inherit (stdenv) hostPlatform buildPlatform;

  version = __forDefaults.versionJson.version + versionSuffix;

  aws-sdk-cpp-nix =
    if aws-sdk-cpp == null then
      null
    else
      aws-sdk-cpp.override {
        apis = [
          "s3"
          "transfer"
        ];
        customMemoryManagement = false;
      };

  # Reimplementation of Nixpkgs' Meson cross file, with some additions to make
  # it actually work.
  mesonCrossFile = builtins.toFile "lix-cross-file.conf" ''
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
  maybePropagatedInputs = lib.optional enableGC boehmgc-nix ++ [ nlohmann_json ];

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
    # Required for meson to generate Cargo wraps
    ./Cargo.lock
  ]);

  functionalTestFiles = fileset.unions [
    ./tests/functional
    ./tests/unit
    (fileset.fileFilter (f: lib.strings.hasPrefix "nix-profile" f.name) ./scripts)
  ];
in
assert (lintInsteadOfBuild -> lix-clang-tidy != null);
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
        ++ lib.optionals (!finalAttrs.dontBuild || internalApiDocs || lintInsteadOfBuild) [
          ./doc
          ./misc
          ./src
          ./COPYING
        ]
        ++ lib.optionals lintInsteadOfBuild [ ./.clang-tidy ]
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

  dontBuild = lintInsteadOfBuild;

  mesonFlags =
    let
      sanitizeOpts = lib.optional (
        sanitize != null
      ) "-Db_sanitize=${builtins.concatStringsSep "," sanitize}";
    in
    lib.optionals hostPlatform.isLinux [
      # You'd think meson could just find this in PATH, but busybox is in buildInputs,
      # which don't actually get added to PATH. And buildInputs is correct over
      # nativeBuildInputs since this should be a busybox executable on the host.
      "-Dsandbox-shell=${lib.getExe' busybox-sandbox-shell "busybox"}"
    ]
    ++ lib.optional hostPlatform.isStatic "-Denable-embedded-sandbox-shell=true"
    ++ lib.optional (finalAttrs.dontBuild && !lintInsteadOfBuild) "-Denable-build=false"
    ++ lib.optional lintInsteadOfBuild "-Dlix-clang-tidy-checks-path=${lix-clang-tidy}/lib/liblix-clang-tidy.so"
    ++ [
      # mesonConfigurePhase automatically passes -Dauto_features=enabled,
      # so we must explicitly enable or disable features that we are not passing
      # dependencies for.
      (lib.mesonEnable "gc" enableGC)
      (lib.mesonEnable "internal-api-docs" internalApiDocs)
      (lib.mesonBool "enable-tests" (finalAttrs.finalPackage.doCheck || lintInsteadOfBuild))
      (lib.mesonBool "enable-docs" canRunInstalled)
      (lib.mesonBool "werror" werror)
    ]
    ++ lib.optional (hostPlatform != buildPlatform) "--cross-file=${mesonCrossFile}"
    ++ sanitizeOpts;

  # We only include CMake so that Meson can locate toml11, which only ships CMake dependency metadata.
  dontUseCmakeConfigure = true;

  nativeBuildInputs =
    [
      python3
      meson
      ninja
      cmake
      rustc
      capnproto-lix
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
    ++ lib.optional internalApiDocs doxygen
    ++ lib.optionals lintInsteadOfBuild [
      # required for a wrapped clang-tidy
      llvmPackages.clang-tools
      # required for run-clang-tidy
      llvmPackages.clang-unwrapped
    ];

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
      pegtl
      capnproto-lix
    ]
    ++ lib.optionals hostPlatform.isLinux [
      libseccomp
      busybox-sandbox-shell
    ]
    ++ lib.optional internalApiDocs rapidcheck
    ++ lib.optional hostPlatform.isx86_64 libcpuid
    # There have been issues building these dependencies
    ++ lib.optional (hostPlatform.canExecute buildPlatform) aws-sdk-cpp-nix
    ++ lib.optionals (finalAttrs.dontBuild) maybePropagatedInputs
    # I am so sorry. This is because checkInputs are required to pass
    # configure, but we don't actually want to *run* the checks here.
    ++ lib.optionals lintInsteadOfBuild finalAttrs.checkInputs;

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

    # Meson allows referencing a /usr/share/cargo/registry shaped thing for subproject sources.
    # Turns out the Nix-generated Cargo dependencies are named the same as they
    # would be in a Cargo registry cache.
    MESON_PACKAGE_CACHE_DIR = finalAttrs.cargoDeps;
  };

  cargoDeps = rustPlatform.importCargoLock { lockFile = ./Cargo.lock; };

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
      # Fix up /usr/bin/env shebangs relied on by the build
      patchShebangs --build tests/ doc/manual/
    '';

  mesonBuildType = "debugoptimized";

  installTargets = lib.optional internalApiDocs "internal-api-html";

  enableParallelBuilding = true;

  doCheck = canRunInstalled && !lintInsteadOfBuild;

  mesonCheckFlags = [
    "--suite=check"
    "--print-errorlogs"
  ];
  # the tests access localhost.
  __darwinAllowLocalNetworking = true;

  # Make sure the internal API docs are already built, because mesonInstallPhase
  # won't let us build them there. They would normally be built in buildPhase,
  # but the internal API docs are conventionally built with doBuild = false.
  preInstall =
    (lib.optionalString internalApiDocs ''
      meson ''${mesonBuildFlags:-} compile "$installTargets"
    '')
    # evil, but like above, we do not want to run an actual build phase
    + lib.optionalString lintInsteadOfBuild ''
      ninja clang-tidy
    '';

  installPhase = lib.optionalString lintInsteadOfBuild ''
    runHook preInstall
    touch $out
    runHook postInstall
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

  # Export the patched version of boehmgc.
  # flake.nix exports that into its overlay.
  passthru = {
    inherit (__forDefaults)
      boehmgc-nix
      editline-lix
      build-release-notes
      pegtl
      ;

    # The collection of dependency logic for this derivation is complicated enough that
    # it's easier to parameterize the devShell off an already called package.nix.
    mkDevShell =
      {
        mkShell,

        bashInteractive,
        clangbuildanalyzer,
        doxygen,
        glibcLocales,
        just,
        nixfmt-rfc-style,
        skopeo,
        xonsh,

        # Lix specific packages
        pre-commit-checks,
        contribNotice,
        check-syscalls,

        # debuggers
        gdb,
        rr,
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
            p.packaging
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
            lib.optional (stdenv.cc.isClang && hostPlatform == buildPlatform) llvmPackages.clang-tools
            ++ [
              # Why are we providing a bashInteractive? Well, when you run
              # `bash` from inside `nix develop`, say, because you are using it
              # via direnv, you will by default get bash (unusable edition).
              bashInteractive
              check-syscalls
              pythonEnv
              # docker image tool
              skopeo
              just
              nixfmt-rfc-style
              # Included above when internalApiDocs is true, but we set that to
              # false intentionally to save dev build time.
              # To build them in a dev shell, you can set -Dinternal-api-docs=enabled when configuring.
              doxygen
              # Load-bearing order. Must come before clang-unwrapped below, but after clang_tools above.
              stdenv.cc
            ]
            ++ [
              pkgs.rust-analyzer
              pkgs.cargo
              pkgs.rustc
              pkgs.rustfmt
              pkgs.rustPlatform.rustLibSrc
              pkgs.rustPlatform.rustcSrc
            ]
            ++ lib.optionals stdenv.cc.isClang [
              # Required for clang-tidy checks.
              llvmPackages.llvm
              llvmPackages.clang-unwrapped.dev
            ]
            ++ lib.optional (pre-commit-checks ? enabledPackages) pre-commit-checks.enabledPackages
            ++ lib.optional (lib.meta.availableOn buildPlatform clangbuildanalyzer) clangbuildanalyzer
            ++ lib.optional (!stdenv.isDarwin) gdb
            ++ lib.optional (lib.meta.availableOn buildPlatform rr) rr
            ++ finalAttrs.checkInputs;

          shellHook = ''
            # don't re-run the hook in (other) nested nix-shells
            function lixShellHook() {
              # n.b. how the heck does this become -env-env? well, `nix develop` does it:
              # https://git.lix.systems/lix-project/lix/src/commit/7575db522e9008685c4009423398f6900a16bcce/src/nix/develop.cc#L240-L241
              # this is, of course, absurd.
              if [[ $name != lix-shell-env && $name != lix-shell-env-env ]]; then
                return
              fi

              PATH=$prefix/bin''${PATH:+:''${PATH}}
              unset PYTHONPATH
              export MANPATH=$out/share/man:''${MANPATH:-}

              # Make bash completion work.
              XDG_DATA_DIRS+=:$out/share

              if [[ ! -f ./.this-is-lix ]]; then
                echo "Dev shell not started from inside a Lix repo, skipping repo setup" >&2
                return
              fi

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

    perl-bindings = pkgs.callPackage ./perl { inherit fileset stdenv; };

    binaryTarball = pkgs.callPackage ./nix-support/binary-tarball.nix {
      nix = finalAttrs.finalPackage;
    };
  };
})
