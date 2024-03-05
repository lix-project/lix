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
  changelog-d,
  boost,
  brotli,
  bzip2,
  curl,
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
  lowdown,
  mdbook,
  mdbook-linkcheck,
  mercurial,
  openssl,
  pkg-config,
  rapidcheck,
  sqlite,
  util-linuxMinimal ? utillinuxMinimal,
  utillinuxMinimal ? null,
  xz,

  busybox-sandbox-shell ? null,

  pname ? "nix",
  versionSuffix ? "",
  officialRelease ? true,
  # Set to true to build the release notes for the next release.
  buildUnreleasedNotes ? false,

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

  topLevelBuildFiles = fileset.unions [
    ./local.mk
    ./Makefile
    ./Makefile.config.in
    ./mk
  ];

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
      ./unit-test-data
    ] ++ lib.optionals (!finalAttrs.dontBuild) [
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

  nativeBuildInputs = [
    bison
    flex
  ] ++ [
    (lib.getBin lowdown)
    mdbook
    mdbook-linkcheck
    autoconf-archive
    autoreconfHook
    pkg-config

    # Tests
    git
    mercurial
    jq
  ] ++ lib.optional stdenv.hostPlatform.isLinux util-linuxMinimal
    ++ lib.optional (!officialRelease && buildUnreleasedNotes) changelog-d;

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
    ++ lib.optionals stdenv.isLinux [ libseccomp ]
    ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid
    # There have been issues building these dependencies
    ++ lib.optional (stdenv.hostPlatform == stdenv.buildPlatform) aws-sdk-cpp-nix
    # FIXME(Qyriad): This is how the flake.nix version does it, but this is cursed.
    ++ lib.optionals (finalAttrs.doCheck) finalAttrs.passthru._checkInputs
  ;

  passthru._checkInputs = [
    gtest
    rapidcheck
  ];

  # FIXME(Qyriad): remove at the end of refactoring.
  checkInputs = finalAttrs.passthru._checkInputs;

  propagatedBuildInputs = [
    boehmgc
    nlohmann_json
  ];

  disallowedReferences = [
    boost
  ];

  preConfigure = lib.optionalString (!finalAttrs.dontBuild && !stdenv.hostPlatform.isStatic) ''
    # Copy libboost_context so we don't get all of Boost in our closure.
    # https://github.com/NixOS/nixpkgs/issues/45462
    mkdir -p $out/lib
    cp -pd ${boost}/lib/{libboost_context*,libboost_thread*,libboost_system*} $out/lib
    rm -f $out/lib/*.a
  '' + lib.optionalString stdenv.hostPlatform.isLinux ''
    chmod u+w $out/lib/*.so.*
    patchelf --set-rpath $out/lib:${stdenv.cc.cc.lib}/lib $out/lib/libboost_thread.so.*
  '' + lib.optionalString stdenv.hostPlatform.isDarwin ''
    for LIB in $out/lib/*.dylib; do
      chmod u+w $LIB
      install_name_tool -id $LIB $LIB
      install_name_tool -delete_rpath ${boost}/lib/ $LIB || true
    done
    install_name_tool -change ${boost}/lib/libboost_system.dylib $out/lib/libboost_system.dylib $out/lib/libboost_thread.dylib
  '';

  configureFlags = lib.optionals stdenv.isLinux [
    "--with-boost=${boost}/lib"
    "--with-sandbox-shell=${busybox-sandbox-shell}/bin/busybox"
  ] ++ lib.optionals (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) [
    "LDFLAGS=-fuse-ld=gold"
  ] ++ [ "--sysconfdir=/etc" ]
    ++ lib.optional stdenv.hostPlatform.isStatic "--enable-embedded-sandbox-shell"
    ++ [ (lib.enableFeature finalAttrs.doCheck "tests") ]
    ++ lib.optionals finalAttrs.doCheck testConfigureFlags
    ++ lib.optional (!canRunInstalled) "--disable-doc-gen"
  ;

  enableParallelBuilding = true;

  makeFlags = "profiledir=$(out)/etc/profile.d PRECOMPILE_HEADERS=1";

  doCheck = true;

  installFlags = "sysconfdir=$(out)/etc";

  postInstall = ''
      mkdir -p $doc/nix-support
      echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
      ${lib.optionalString stdenv.hostPlatform.isStatic ''
      mkdir -p $out/nix-support
      echo "file binary-dist $out/bin/nix" >> $out/nix-support/hydra-build-products
      ''}
      ${lib.optionalString stdenv.isDarwin ''
      install_name_tool \
        -change ${boost}/lib/libboost_context.dylib \
        $out/lib/libboost_context.dylib \
        $out/lib/libnixutil.dylib
      ''}
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

  passthru.finalAttrs = finalAttrs;
  passthru.perl-bindings = pkgs.callPackage ./perl {
    inherit fileset stdenv;
  };
})
