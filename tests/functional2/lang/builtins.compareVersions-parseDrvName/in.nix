let

  name1 = "hello-1.0.2";
  name2 = "hello";
  name3 = "915resolution-0.5.2";
  name4 = "xf86-video-i810-1.7.4";
  name5 = "name-that-ends-with-dash--1.0";

  eq = 0;
  lt = builtins.sub 0 1;
  gt = 1;

  versionTest = v1: v2: expected:
    let d1 = builtins.compareVersions v1 v2;
        d2 = builtins.compareVersions v2 v1;
    in d1 == builtins.sub 0 d2 && d1 == expected;
in

assert (builtins.parseDrvName name1).name == "hello";
assert (builtins.parseDrvName name1).version == "1.0.2";
assert (builtins.parseDrvName name2).name == "hello";
assert (builtins.parseDrvName name2).version == "";
assert (builtins.parseDrvName name3).name == "915resolution";
assert (builtins.parseDrvName name3).version == "0.5.2";
assert (builtins.parseDrvName name4).name == "xf86-video-i810";
assert (builtins.parseDrvName name4).version == "1.7.4";
assert (builtins.parseDrvName name5).name == "name-that-ends-with-dash";
assert (builtins.parseDrvName name5).version == "-1.0";
assert versionTest "1.0" "2.3" lt;
assert versionTest "2.1" "2.3" lt;
assert versionTest "2.3" "2.3" eq;
assert versionTest "2.5" "2.3" gt;
assert versionTest "3.1" "2.3" gt;
assert versionTest "2.3.1" "2.3" gt;
assert versionTest "2.3.1" "2.3a" gt;
assert versionTest "2.3pre1" "2.3" lt;
assert versionTest "2.3pre3" "2.3pre12" lt;
assert versionTest "2.3a" "2.3c" lt;
assert versionTest "2.3pre1" "2.3c" lt;
assert versionTest "2.3pre1" "2.3q" lt;
true
