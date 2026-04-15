builtins.mapAttrs (name: value: name + "-" + value) { x = "foo"; y = "bar"; }
