with import ./config.nix;
let
  setXattrLinux = inodeTgt: name: value: "setfattr -n ${name} -v \"${value}\" ${inodeTgt}";
  setExtendedAttribute = if isDarwin then setXattrDarwin else setXattrLinux;
  setAndCheckACLLinux = inodeTgt: perm: ''
    setfacl -m ${perm} ${inodeTgt}
    getfacl -an ${inodeTgt} | grep -i "mask::rw." || { echo no mask in the ACL, setfacl ineffective?; exit 1; }
  '';
  setAndCheckACL = if isDarwin then setAndCheckACLDarwin else setAndCheckACLLinux;

  wellKnownLinuxNames = {
    "user.mime_type" = "text/html5";
    "user.deriver" = "yue";

    # On Linux, inside of the sandbox, you cannot write to these because CAP_SYS_ADMIN is required.
    # Those are protected xattrs.
    # Root inside the root namespace can set those.
    # "trusted.md5sum" = "...";
    # "security.selinux" = "...";
  };
  wellKnownACLs = [
    # sets xattr system.posix_acl_access on Linux.
    "u:1000:rwX"
  ];
  mapAttrsToList = fn: attrs: map (k: fn k attrs.${k}) (builtins.attrNames attrs);

  linuxScenariosForAnInode = withACL: inodeTgt:
    builtins.concatStringsSep "\n" (
      (mapAttrsToList (name: value: setExtendedAttribute inodeTgt name value) wellKnownLinuxNames)
      ++ (if withACL then map (setAndCheckACL inodeTgt) wellKnownACLs else [ ])
    );

  setXattrDarwin = inodeTgt: name: value: "xattr -w ${name} \"${value}\" ${inodeTgt}";
  setAndCheckACLDarwin = inodeTgt: perm: ''
    chmod +a "${perm}" "${inodeTgt}"
    ls -el "${inodeTgt}"
    ls -el "${inodeTgt}" | grep -Ei '${perm}'
  '';
  # NOTE(Qyriad): Darwin ACLs have some kind of sorting order, so ideally we should parse them to check them.
  # However: fuck that, for now. So we've just written them in The Order here so the text-match.
  wellKnownACLsDarwin = [
    "group:everyone allow read,write"
    "group:staff allow read,delete,writesecurity,chown"
  ];
  darwinScenariosForAnInode = withACL: inodeTgt:
    builtins.concatStringsSep "\n" (
      (mapAttrsToList (name: value: setXattrDarwin inodeTgt name value) wellKnownLinuxNames)
      ++ (if withACL then map (setAndCheckACLDarwin inodeTgt) wellKnownACLsDarwin else [ ])
    );

  scenariosForAnInode = if isDarwin then darwinScenariosForAnInode else linuxScenariosForAnInode;
in
{
  during-build = mkDerivation {
    name = "xattrs-during-build";
    buildCommand = ''
      touch work
      ${scenariosForAnInode true "./work"}
      echo meow > $out
    '';
  };

  in-root-outputs-file = mkDerivation {
    name = "xattrs-on-root-output-file";
    buildCommand = ''
      touch $out

      ${
        # We are not allowed to set ACLs on the root of the output directory.
        # There's an explicit suspicious permission check that will cause this build to be rejected.
        scenariosForAnInode false "$out"
      }
    '';
  };

  in-root-outputs-dir = mkDerivation {
    name = "xattrs-on-root-output-dir";
    buildCommand = ''
      touch $out

      ${
        # We are not allowed to set ACLs on the root of the output directory.
        # There's an explicit suspicious permission check that will cause this build to be rejected.
        scenariosForAnInode false "$out"
      }
    '';
  };

  in-output-content = mkDerivation {
    name = "xattrs-under-directory-output";
    buildCommand = ''
      mkdir -p $out
      touch $out/test
      mkdir -p $out/test2 $out/test3
      touch $out/test3/test4

      ${scenariosForAnInode true "$out/test"}
      ${scenariosForAnInode true "$out/test2"}
      ${scenariosForAnInode true "$out/test3/test4"}
    '';
  };
}
