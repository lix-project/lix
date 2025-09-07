{
  pkgs ? import <nixpkgs> { },
  # Git commit ID, if available
  lixRevision ? null,
  nix2container,
  lib ? pkgs.lib,
  name ? "lix",
  tag ? "latest",
  bundleNixpkgs ? true,
  channelName ? "nixpkgs",
  channelURL ? "https://nixos.org/channels/nixpkgs-unstable",
  extraPkgs ? [ ],
  maxLayers ? 100,
  nixConf ? { },
  flake-registry ? null,
}:
let
  layerContents = with pkgs; [
    # pulls in glibc and openssl, about 60MB
    { contents = [ coreutils-full ]; }
    # some stuff that is low in the closure graph and small ish, mostly to make
    # incremental lix updates cheaper
    {
      contents = [
        curl
        libxml2
        sqlite
      ];
    }
    # 50MB of git
    { contents = [ gitMinimal ]; }
    # 144MB of nixpkgs
    {
      contents = [ channel ];
      inProfile = false;
    }
  ];

  # These packages are left to be auto layered by nix2container, since it is
  # less critical that they get layered sensibly and they tend to not be deps
  # of anything in particular
  autoLayered = with pkgs; [
    bashInteractive
    gnutar
    gzip
    gnugrep
    which
    less
    wget
    man
    cacert.out
    findutils
    iana-etc
    openssh
    nix
  ];

  defaultPkgs =
    lib.lists.flatten (
      map (x: if !(x ? inProfile) || x.inProfile then x.contents else [ ]) layerContents
    )
    ++ autoLayered
    ++ extraPkgs;

  users = {

    root = {
      uid = 0;
      shell = "${pkgs.bashInteractive}/bin/bash";
      home = "/root";
      gid = 0;
      groups = [ "root" ];
      description = "System administrator";
    };

    nobody = {
      uid = 65534;
      shell = "${pkgs.shadow}/bin/nologin";
      home = "/var/empty";
      gid = 65534;
      groups = [ "nobody" ];
      description = "Unprivileged account (don't use!)";
    };
  }
  // lib.listToAttrs (
    map (n: {
      name = "nixbld${toString n}";
      value = {
        uid = 30000 + n;
        gid = 30000;
        groups = [ "nixbld" ];
        description = "Nix build user ${toString n}";
      };
    }) (lib.lists.range 1 32)
  );

  groups = {
    root.gid = 0;
    nixbld.gid = 30000;
    nobody.gid = 65534;
  };

  userToPasswd = (
    k:
    {
      uid,
      gid ? 65534,
      home ? "/var/empty",
      description ? "",
      shell ? "/bin/false",
      groups ? [ ],
    }:
    "${k}:x:${toString uid}:${toString gid}:${description}:${home}:${shell}"
  );
  passwdContents = (lib.concatStringsSep "\n" (lib.attrValues (lib.mapAttrs userToPasswd users)));

  userToShadow = k: { ... }: "${k}:!:1::::::";
  shadowContents = (lib.concatStringsSep "\n" (lib.attrValues (lib.mapAttrs userToShadow users)));

  # Map groups to members
  # {
  #   group = [ "user1" "user2" ];
  # }
  groupMemberMap = (
    let
      # Create a flat list of user/group mappings
      mappings = (
        builtins.foldl' (
          acc: user:
          let
            groups = users.${user}.groups or [ ];
          in
          acc ++ map (group: { inherit user group; }) groups
        ) [ ] (lib.attrNames users)
      );
    in
    (builtins.foldl' (
      acc: v: acc // { ${v.group} = acc.${v.group} or [ ] ++ [ v.user ]; }
    ) { } mappings)
  );

  groupToGroup =
    k:
    { gid }:
    let
      members = groupMemberMap.${k} or [ ];
    in
    "${k}:x:${toString gid}:${lib.concatStringsSep "," members}";
  groupContents = (lib.concatStringsSep "\n" (lib.attrValues (lib.mapAttrs groupToGroup groups)));

  defaultNixConf = {
    sandbox = "false";
    build-users-group = "nixbld";
    trusted-public-keys = [ "cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=" ];
  };

  nixConfContents =
    (lib.concatStringsSep "\n" (
      lib.mapAttrsToList (
        n: v:
        let
          vStr = if builtins.isList v then lib.concatStringsSep " " v else v;
        in
        "${n} = ${vStr}"
      ) (defaultNixConf // nixConf)
    ))
    + "\n";

  nixpkgs = pkgs.path;
  channel = pkgs.runCommand "channel-nixpkgs" { } ''
    mkdir $out
    ${lib.optionalString bundleNixpkgs ''
      ln -s ${nixpkgs} $out/nixpkgs
      echo "[]" > $out/manifest.nix
    ''}
  '';

  baseSystem =
    let
      rootEnv = pkgs.buildPackages.buildEnv {
        name = "root-profile-env";
        paths = defaultPkgs;
      };
      manifest = pkgs.buildPackages.runCommand "manifest.nix" { } ''
        cat > $out <<EOF
        [
        ${lib.concatStringsSep "\n" (
          builtins.map (
            drv:
            let
              outputs = drv.outputsToInstall or [ "out" ];
            in
            ''
              {
                ${lib.concatStringsSep "\n" (
                  builtins.map (output: ''
                    ${output} = { outPath = "${lib.getOutput output drv}"; };
                  '') outputs
                )}
                outputs = [ ${lib.concatStringsSep " " (builtins.map (x: "\"${x}\"") outputs)} ];
                name = "${drv.name}";
                outPath = "${drv}";
                system = "${drv.system}";
                type = "derivation";
                meta = { };
              }
            ''
          ) defaultPkgs
        )}
        ]
        EOF
      '';
      profile = pkgs.buildPackages.runCommand "user-environment" { } ''
        mkdir $out
        cp -a ${rootEnv}/* $out/
        ln -sf ${manifest} $out/manifest.nix
      '';
      flake-registry-path =
        if (flake-registry == null) then
          null
        else if (builtins.readFileType (toString flake-registry)) == "directory" then
          "${flake-registry}/flake-registry.json"
        else
          flake-registry;
    in
    pkgs.runCommand "base-system"
      {
        inherit
          passwdContents
          groupContents
          shadowContents
          nixConfContents
          ;
        passAsFile = [
          "passwdContents"
          "groupContents"
          "shadowContents"
          "nixConfContents"
        ];
        allowSubstitutes = false;
        preferLocalBuild = true;
      }
      (
        ''
          env
          set -x
          mkdir -p $out/etc

          mkdir -p $out/etc/ssl/certs
          ln -s /nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt $out/etc/ssl/certs

          cat $passwdContentsPath > $out/etc/passwd
          echo "" >> $out/etc/passwd

          cat $groupContentsPath > $out/etc/group
          echo "" >> $out/etc/group

          cat $shadowContentsPath > $out/etc/shadow
          echo "" >> $out/etc/shadow

          mkdir -p $out/usr
          ln -s /nix/var/nix/profiles/share $out/usr/

          mkdir -p $out/nix/var/nix/gcroots
          ln -s /nix/var/nix/profiles $out/nix/var/nix/gcroots/profiles

          mkdir $out/tmp

          mkdir -p $out/var/tmp

          mkdir -p $out/etc/nix
          cat $nixConfContentsPath > $out/etc/nix/nix.conf

          mkdir -p $out/root
          mkdir -p $out/nix/var/nix/profiles/per-user/root

          ln -s ${profile} $out/nix/var/nix/profiles/default-1-link
          ln -s /nix/var/nix/profiles/default-1-link $out/nix/var/nix/profiles/default
          ln -s /nix/var/nix/profiles/default $out/root/.nix-profile

          ln -s ${channel} $out/nix/var/nix/profiles/per-user/root/channels-1-link
          ln -s /nix/var/nix/profiles/per-user/root/channels-1-link $out/nix/var/nix/profiles/per-user/root/channels

          mkdir -p $out/root/.nix-defexpr
          ln -s /nix/var/nix/profiles/per-user/root/channels $out/root/.nix-defexpr/channels
          echo "${channelURL} ${channelName}" > $out/root/.nix-channels

          mkdir -p $out/bin $out/usr/bin
          ln -s ${pkgs.coreutils}/bin/env $out/usr/bin/env
          ln -s ${pkgs.bashInteractive}/bin/bash $out/bin/sh

        ''
        + (lib.optionalString (flake-registry-path != null) ''
          nixCacheDir="/root/.cache/nix"
          mkdir -p $out$nixCacheDir
          globalFlakeRegistryPath="$nixCacheDir/flake-registry.json"
          ln -s ${flake-registry-path} $out$globalFlakeRegistryPath
          mkdir -p $out/nix/var/nix/gcroots/auto
          rootName=$(${pkgs.nix}/bin/nix --extra-experimental-features nix-command hash file --type sha1 --base32 <(echo -n $globalFlakeRegistryPath))
          ln -s $globalFlakeRegistryPath $out/nix/var/nix/gcroots/auto/$rootName
        '')
      );

  layers = builtins.foldl' (
    layersList: el:
    let
      layer = nix2container.buildLayer {
        deps = el.contents;
        layers = layersList;
      };
    in
    layersList ++ [ layer ]
  ) [ ] layerContents;

  image = nix2container.buildImage {

    inherit name tag maxLayers;

    inherit layers;

    copyToRoot = [ baseSystem ];

    initializeNixDatabase = true;

    perms = [
      {
        path = baseSystem;
        regex = "(/var)?/tmp";
        mode = "1777";
      }
    ];

    config = {
      Cmd = [ "/root/.nix-profile/bin/bash" ];
      Env = [
        "USER=root"
        "PATH=${
          lib.concatStringsSep ":" [
            "/root/.nix-profile/bin"
            "/nix/var/nix/profiles/default/bin"
            "/nix/var/nix/profiles/default/sbin"
          ]
        }"
        "MANPATH=${
          lib.concatStringsSep ":" [
            "/root/.nix-profile/share/man"
            "/nix/var/nix/profiles/default/share/man"
          ]
        }"
        "SSL_CERT_FILE=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"
        "GIT_SSL_CAINFO=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"
        "NIX_SSL_CERT_FILE=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt"
        "NIX_PATH=/nix/var/nix/profiles/per-user/root/channels:/root/.nix-defexpr/channels"
      ];

      Labels = {
        "org.opencontainers.image.title" = "Lix";
        "org.opencontainers.image.source" = "https://git.lix.systems/lix-project/lix";
        "org.opencontainers.image.vendor" = "Lix project";
        "org.opencontainers.image.version" = pkgs.nix.version;
        "org.opencontainers.image.description" =
          "Minimal Lix container image, with some batteries included.";
      }
      // lib.optionalAttrs (lixRevision != null) { "org.opencontainers.image.revision" = lixRevision; };
    };

    meta = {
      description = "Docker image for Lix. This is built with nix2container; see that project's README for details";
      longDescription = ''
        Docker image for Lix, built with nix2container.
        To copy it to your docker daemon, nix run .#dockerImage.copyToDockerDaemon
        To copy it to podman, nix run .#dockerImage.copyTo containers-storage:lix
      '';
    };
  };
in
image
// {
  # We don't ship the tarball as the default output because it is a strange thing to want imo
  tarball =
    pkgs.buildPackages.runCommand "docker-image-tarball-${pkgs.nix.version}"
      {
        nativeBuildInputs = [ pkgs.buildPackages.bubblewrap ];
        meta.description = "Docker image tarball with Lix for ${pkgs.system}";
      }
      ''
        mkdir -p $out/nix-support
        image=$out/image.tar
        # bwrap for foolish temp dir selection code that forces /var/tmp:
        # https://github.com/containers/skopeo.git/blob/60ee543f7f7c242f46cc3a7541d9ac8ab1c89168/vendor/github.com/containers/image/v5/internal/tmpdir/tmpdir.go#L15-L18
        mkdir -p $TMPDIR/fake-var/tmp
        args=(--unshare-user --bind "$TMPDIR/fake-var" /var)
        for dir in /*; do
          args+=(--dev-bind "/$dir" "/$dir")
        done
        bwrap ''${args[@]} -- ${lib.getExe image.copyTo} docker-archive:$image
        gzip $image
        echo "file binary-dist $image" >> $out/nix-support/hydra-build-products
      '';
}
