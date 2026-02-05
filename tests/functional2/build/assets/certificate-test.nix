{ sandbox ? true, mode }:

with import ./config.nix;

mkDerivation (
  {
    name = "ssl-export";
    buildCommand = ''
      # Add some indirection, otherwise grepping into the debug output finds the string.
      report () { echo CERT_$1_IN_SANDBOX; }

      ${if sandbox then ''
        # This depends on a proper sandbox otherwise the path may be the outside-of-the-builder's one.
        if [ -f /etc/ssl/certs/ca-certificates.crt ]; then
          content=$(</etc/ssl/certs/ca-certificates.crt)
          if [ "$content" == CERT_CONTENT ]; then
            report present
          else
            report corrupted
            # printf "expected: 'CERT_CONTENT', got: '%s'" "$content"
          fi
        else
          report missing
        fi
      '' else ""}

      if [ -f "$NIX_SSL_CERT_FILE" ]; then
        echo "found $NIX_SSL_CERT_FILE"
        content=$(<$NIX_SSL_CERT_FILE)
        if [ "$content" == CERT_CONTENT ]; then
          report present-env-var
        else
          report corrupted
          # printf "expected: 'CERT_CONTENT', got: '%s'" "$content"
        fi
      else
        report missing
      fi

      # Always fail, because we do not want to bother with fixed-output
      # derivations being cached, and do not want to compute the right hash.
      false;
    '';
  } // rec {
    fixed-output = { outputHash = "sha256:0000000000000000000000000000000000000000000000000000000000000000"; };
    clobbering-impurities = fixed-output // { impureEnvVars = [ "NIX_SSL_CERT_FILE" ]; };
    normal = { };
  }.${mode}
)
