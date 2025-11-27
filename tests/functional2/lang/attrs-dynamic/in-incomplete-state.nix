let b = builtins.toJSON a; a = { foo = "bar"; "${builtins.seq b "baz"}" = b; }; in a
