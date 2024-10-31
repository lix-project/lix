source common.sh

expectStderr 1 nix hash to-sri md5-rrdBU2a35b2PM2ZO+n/zGw== \
  | grepQuiet "md5 values are not allowed"
expectStderr 1 nix hash to-sri sha1-SXZKz6Po0xFryhnhSDvvOfAuBOo= \
  | grepQuiet "sha1 values are not allowed"

nix hash to-sri --type md5 a180c3fe91680389c210c99def54d9e0 2>&1 \
  | grepQuiet "md5 hashes are considered weak"
nix hash to-sri --type sha1 49764acfa3e8d3116bca19e1483bef39f02e04ea 2>&1 \
  | grepQuiet "sha1 hashes are considered weak"
