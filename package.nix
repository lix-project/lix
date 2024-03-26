{
  pkgs,
  lib,
  stdenv,
  autoconf-archive,
  autoreconfHook,
  aws-sdk-cpp,
  boehmgc,
  nlohmann_json,
  bison,
  build-release-notes,
  boost,
  brotli,
  bzip2,
  curl,
  doxygen,
  editline,
  fileset,
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
  rapidcheck,
  sqlite,
  util-linuxMinimal ? utillinuxMinimal,
  utillinuxMinimal ? null,
  xz,

  busybox-sandbox-shell,

  pname ? "nix",
  versionSuffix ? "",
  officialRelease ? true,
  # Set to true to build the release notes for the next release.
  buildUnreleasedNotes ? false,
  internalApiDocs ? false,
  # Avoid setting things that would interfere with a functioning devShell
  forDevShell ? false,

  # FIXME(Qyriad): build Lix using Meson instead of autoconf and make.
  # This flag will be removed when the migration to Meson is complete.
  buildWithMeson ? false,

  # Not a real argument, just the only way to approximate let-binding some
  # stuff for argument defaults.
  __forDefaults ? {
    canRunInstalled = stdenv.buildPlatform.canExecute stdenv.hostPlatform;
  },
}: let
  inherit (__forDefaults) canRunInstalled;

  version = lib.fileContents ./.version + versionSuffix;

  aws-sdk-cpp-nix = aws-sdk-cpp.override {
    apis = [ "s3" "transfer" ];
    customMemoryManagement = false;
  };

  testConfigureFlags = [
    "RAPIDCHECK_HEADERS=${lib.getDev rapidcheck}/extras/gtest/include"
  ];

  # The internal API docs need these for the build, but if we're not building
  # Nix itself, then these don't need to be propagated.
  maybePropagatedInputs = [
    boehmgc
    nlohmann_json
  ];

  # .gitignore has already been processed, so any changes in it are irrelevant
  # at this point. It is not represented verbatim for test purposes because
  # that would interfere with repo semantics.
  baseFiles = fileset.fileFilter (f: f.name != ".gitignore") ./.;

  configureFiles = fileset.unions [
    ./.version
    ./configure.ac
    ./m4
    # TODO: do we really need README.md? It doesn't seem used in the build.
    ./README.md
  ];

  topLevelBuildFiles = fileset.unions ([
    ./local.mk
    ./Makefile
    ./Makefile.config.in
    ./mk
  ] ++ lib.optionals buildWithMeson [
    ./meson.build
    ./meson.options
    ./meson/cleanup-install.bash
  ]);

 functionalTestFiles = fileset.unions [
    ./tests/functional
    ./tests/unit
    (fileset.fileFilter (f: lib.strings.hasPrefix "nix-profile" f.name) ./scripts)
 ];

in stdenv.mkDerivation (finalAttrs: {
  inherit pname version;

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.intersection baseFiles (fileset.unions ([
      configureFiles
      topLevelBuildFiles
      functionalTestFiles
    ] ++ lib.optionals (!finalAttrs.dontBuild || internalApiDocs) [
      ./boehmgc-coroutine-sp-fallback.diff
      ./doc
      ./misc
      ./precompiled-headers.h
      ./src
      ./COPYING
      ./scripts/local.mk
    ]));
  };

  VERSION_SUFFIX = versionSuffix;

  outputs = [ "out" ]
    ++ lib.optionals (!finalAttrs.dontBuild) [ "dev" "doc" ];

  dontBuild = false;

  # FIXME(Qyriad): see if this is still needed once the migration to Meson is completed.
  mesonFlags = lib.optionals (buildWithMeson && stdenv.hostPlatform.isLinux) [
    "-Dsandbox-shell=${lib.getBin busybox-sandbox-shell}/bin/busybox"
  ];

  nativeBuildInputs = [
    bison
    flex
  ] ++ [
    (lib.getBin lowdown)
    mdbook
    mdbook-linkcheck
    autoconf-archive
  ] ++ lib.optional (!buildWithMeson) autoreconfHook ++ [
    pkg-config

    # Tests
    git
    mercurial
    jq
    lsof
  ] ++ lib.optional stdenv.hostPlatform.isLinux util-linuxMinimal
    ++ lib.optional (!officialRelease && buildUnreleasedNotes) build-release-notes
    ++ lib.optional internalApiDocs doxygen
    ++ lib.optionals buildWithMeson [
      meson
      ninja
  ];

  buildInputs = [
    curl
    bzip2
    xz
    brotli
    editline
    openssl
    sqlite
    libarchive
    boost
    lowdown
    libsodium
  ]
    ++ lib.optionals stdenv.hostPlatform.isLinux [ libseccomp busybox-sandbox-shell ]
    ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid
    # There have been issues building these dependencies
    ++ lib.optional (stdenv.hostPlatform == stdenv.buildPlatform) aws-sdk-cpp-nix
    ++ lib.optionals (finalAttrs.dontBuild) maybePropagatedInputs
  ;

  checkInputs = [
    gtest
    rapidcheck
  ];

  propagatedBuildInputs = lib.optionals (!finalAttrs.dontBuild) maybePropagatedInputs;

  disallowedReferences = [
    boost
  ];

  # Needed for Meson to find Boost.
  # https://github.com/NixOS/nixpkgs/issues/86131.
  env = lib.optionalAttrs (buildWithMeson || forDevShell) {
    BOOST_INCLUDEDIR = "${lib.getDev boost}/include";
    BOOST_LIBRARYDIR = "${lib.getLib boost}/lib";
  };

  preConfigure = lib.optionalString (!finalAttrs.dontBuild && !stdenv.hostPlatform.isStatic) ''
    # Copy libboost_context so we don't get all of Boost in our closure.
    # https://github.com/NixOS/nixpkgs/issues/45462
    mkdir -p $out/lib
    cp -pd ${boost}/lib/{libboost_context*,libboost_thread*,libboost_system*} $out/lib
    rm -f $out/lib/*.a
  '' + lib.optionalString (!finalAttrs.dontBuild && stdenv.hostPlatform.isLinux) ''
    chmod u+w $out/lib/*.so.*
    patchelf --set-rpath $out/lib:${stdenv.cc.cc.lib}/lib $out/lib/libboost_thread.so.*
  '' + lib.optionalString (!finalAttrs.dontBuild && stdenv.hostPlatform.isDarwin) ''
    for LIB in $out/lib/*.dylib; do
      chmod u+w $LIB
      install_name_tool -id $LIB $LIB
      install_name_tool -delete_rpath ${boost}/lib/ $LIB || true
    done
    install_name_tool -change ${boost}/lib/libboost_system.dylib $out/lib/libboost_system.dylib $out/lib/libboost_thread.dylib
  '' + ''
    # Workaround https://github.com/NixOS/nixpkgs/issues/294890.
    if [[ -n "''${doCheck:-}" ]]; then
      appendToVar configureFlags "--enable-tests"
    else
      appendToVar configureFlags "--disable-tests"
    fi
  '';

  configureFlags = lib.optionals stdenv.isLinux [
    "--with-boost=${boost}/lib"
    "--with-sandbox-shell=${busybox-sandbox-shell}/bin/busybox"
  ] ++ lib.optionals (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) [
    "LDFLAGS=-fuse-ld=gold"
  ]
    ++ lib.optional stdenv.hostPlatform.isStatic "--enable-embedded-sandbox-shell"
    ++ lib.optionals (finalAttrs.doCheck || internalApiDocs) testConfigureFlags
    ++ lib.optional (!canRunInstalled) "--disable-doc-gen"
    ++ [ (lib.enableFeature internalApiDocs "internal-api-docs") ]
    ++ lib.optional (!forDevShell) "--sysconfdir=/etc";

  mesonBuildType = lib.optional (buildWithMeson || forDevShell) "debugoptimized";

  installTargets = lib.optional internalApiDocs "internal-api-html";

  enableParallelBuilding = true;

  makeFlags = "profiledir=$(out)/etc/profile.d PRECOMPILE_HEADERS=1";

  doCheck = true;

  mesonCheckFlags = lib.optionals (buildWithMeson || forDevShell) [
    "--suite=check"
  ];

  installFlags = "sysconfdir=$(out)/etc";

  postInstall = lib.optionalString (!finalAttrs.dontBuild) ''
    mkdir -p $doc/nix-support
    echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
  '' + lib.optionalString stdenv.hostPlatform.isStatic ''
    mkdir -p $out/nix-support
    echo "file binary-dist $out/bin/nix" >> $out/nix-support/hydra-build-products
  '' + lib.optionalString stdenv.isDarwin ''
    for lib in libnixutil.dylib libnixexpr.dylib; do
      install_name_tool \
        -change "${lib.getLib boost}/lib/libboost_context.dylib" \
        "$out/lib/libboost_context.dylib" \
        "$out/lib/$lib"
    done
  '' + lib.optionalString internalApiDocs ''
    mkdir -p $out/nix-support
    echo "doc internal-api-docs $out/share/doc/nix/internal-api/html" >> "$out/nix-support/hydra-build-products"
  '';

  doInstallCheck = finalAttrs.doCheck;
  installCheckFlags = "sysconfdir=$(out)/etc";
  installCheckTarget = "installcheck"; # work around buggy detection in stdenv

  preInstallCheck = lib.optionalString stdenv.hostPlatform.isDarwin ''
    export OBJC_DISABLE_INITIALIZE_FORK_SAFETY=YES
  '';

  separateDebugInfo = !stdenv.hostPlatform.isStatic && !finalAttrs.dontBuild;

  strictDeps = true;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  meta.platforms = lib.platforms.unix;

  passthru.perl-bindings = pkgs.callPackage ./perl {
    inherit fileset stdenv;
  };
})
