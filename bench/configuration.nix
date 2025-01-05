{
  config,
  pkgs,
  lib,
  ...
}:

{
  boot = {
    initrd = {
      availableKernelModules = [
        "xhci_pci"
        "ahci"
      ];
      kernelModules = [ "dm-snapshot" ];
      luks.devices = {
        croot = {
          device = "/dev/sdb";
          allowDiscards = true;
        };
      };
    };
    kernelModules = [ "kvm-intel" ];
    kernelPackages = pkgs.linuxPackages_latest;

    loader = {
      systemd-boot.enable = true;
      efi.canTouchEfiVariables = true;
    };
  };

  hardware = {
    enableRedistributableFirmware = true;
    cpu.intel.updateMicrocode = true;
    graphics.enable32Bit = true;
    graphics.extraPackages = with pkgs; [
      vaapiIntel
      intel-media-driver
      intel-compute-runtime
    ];
  };

  fileSystems = {
    "/" = {
      device = "/dev/sda2";
      fsType = "xfs";
      options = [ "noatime" ];
    };

    "/boot" = {
      device = "/dev/sda1";
      fsType = "vfat";
    };

    "/nas" = {
      device = "nas:/";
      fsType = "nfs4";
      options = [
        "ro"
        "x-systemd.automount"
      ];
    };
  };
  swapDevices = [ { device = "/dev/swap"; } ];

  networking = {
    useDHCP = false;
    hostName = "host";
    wireless = {
      enable = true;
      interfaces = [ "eth1" ];
    };
    interfaces = {
      eth0.useDHCP = true;
      eth1.useDHCP = true;
    };
    wg-quick.interfaces = {
      wg0 = {
        address = [ "2001:db8::1" ];
        privateKeyFile = "/etc/secrets/wg0.key";
        peers = [
          {
            publicKey = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
            endpoint = "[2001:db8::2]:61021";
            allowedIPs = [ "2001::db8:1::/64" ];
          }
        ];
      };
    };

    firewall.allowedUDPPorts = [ 4567 ];
  };

  i18n = {
    defaultLocale = "en_US.UTF-8";
    inputMethod.enable = true;
    inputMethod.type = "ibus";
  };

  services = {
    libinput.enable = true;
    xserver = {
      enable = true;
      xkb.layout = "us";
      xkb.variant = "altgr-intl";
      xkb.options = "ctrl:nocaps";
      wacom.enable = true;
      videoDrivers = [ "modesetting" ];
      modules = [ pkgs.xf86_input_wacom ];

      displayManager.sx.enable = true;
      windowManager.i3.enable = true;
    };

    udev.extraHwdb = ''
      # not like this mattered at all
      # we're not running udev from here
    '';

    udev.extraRules = ''
      # ACTION=="add", SUBSYSTEM=="input", ...
    '';
  };

  programs = {
    light.enable = true;
    wireshark = {
      enable = true;
      package = pkgs.wireshark-qt;
    };
    gnupg.agent = {
      enable = true;
    };
  };

  fonts.packages = with pkgs; [
    font-awesome
    noto-fonts
    noto-fonts-cjk-sans
    noto-fonts-emoji
    noto-fonts-extra
    dejavu_fonts
    powerline-fonts
    source-code-pro
    cantarell-fonts
  ];

  users = {
    mutableUsers = false;

    users = {
      user = {
        isNormalUser = true;
        group = "user";
        extraGroups = [
          "wheel"
          "video"
          "audio"
          "dialout"
          "users"
          "kvm"
          "wireshark"
        ];
        password = "unimportant";
      };
    };

    groups = {
      user = { };
    };
  };

  security = {
    pam.loginLimits = [
      {
        domain = "@audio";
        item = "memlock";
        type = "-";
        value = "unlimited";
      }
      {
        domain = "@audio";
        item = "rtprio";
        type = "-";
        value = "99";
      }
      {
        domain = "@audio";
        item = "nofile";
        type = "soft";
        value = "99999";
      }
      {
        domain = "@audio";
        item = "nofile";
        type = "hard";
        value = "99999";
      }
    ];

    sudo.extraRules = [
      {
        users = [ "user" ];
        commands = [
          {
            command = "${pkgs.linuxPackages.cpupower}/bin/cpupower";
            options = [ "NOPASSWD" ];
          }
        ];
      }
    ];
  };

  environment.systemPackages = with pkgs; [
    a2jmidid
    age
    ardour
    bemenu
    blender
    breeze-icons
    breeze-qt5
    bubblewrap
    calf
    claws-mail
    darktable
    duperemove
    emacs
    feh
    file
    firefox
    fluidsynth
    adwaita-icon-theme
    gnuplot
    graphviz
    helm
    i3status-rust
    inkscape
    jack2
    jq
    krita
    ldns
    libqalculate
    libreoffice
    man-pages
    nix-diff
    nix-index
    nix-output-monitor
    open-music-kontrollers.patchmatrix
    pamixer
    pavucontrol
    pciutils
    picom
    pwgen
    redshift
    ripgrep
    rlwrap
    silver-searcher
    soundfont-fluid
    whois
    wol
    xclip
    xdot
    xdotool
    xorg.xkbcomp
    yt-dlp
    zathura
    borgbackup
    linuxPackages.cpupower
    mtr
    kitty
    xf86_input_wacom
  ];

  environment.pathsToLink = [ "/share/soundfonts" ];

  systemd.user.services.run-python = {
    after = [ "network-online.target" ];
    script = ''
      exec ${pkgs.python3}/bin/python
    '';
    serviceConfig = {
      CapabilityBoundingSet = [ "" ];
      KeyringMode = "private";
      LockPersonality = true;
      MemoryDenyWriteExecute = true;
      NoNewPrivileges = true;
      PrivateDevices = true;
      PrivateTmp = true;
      PrivateUsers = true;
      ProcSubset = "pid";
      ProtectClock = true;
      ProtectControlGroups = true;
      ProtectHome = true;
      ProtectHostname = true;
      ProtectKernelLogs = true;
      ProtectKernelModules = true;
      ProtectKernelTunables = true;
      ProtectProc = "invisible";
      ProtectSystem = "strict";
      RestrictAddressFamilies = "AF_INET AF_INET6";
      RestrictNamespaces = true;
      RestrictRealtime = true;
      RestrictSUIDSGID = true;
      SystemCallArchitectures = "native";
      SystemCallFilter = [
        "@system-service"
        "~ @resources @privileged"
      ];
      UMask = "077";
    };
  };

  system.stateVersion = "23.11";
}
