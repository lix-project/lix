---
synopsis: Show error when item from NIX_PATH cannot be downloaded
issues: []
cls: []
category: Fixes
credits: [ma27]
---

For e.g. `nix-instantiate -I https://example.com/404`, you'd only get a warning if the download failed, such as

    warning: Nix search path entry 'https://example.com/404' cannot be downloaded, ignoring

Now, the full error that caused the download failure is displayed with a note that the search
path entry is ignored, e.g.

    warning:
         … while downloading https://example.com/404 to satisfy NIX_PATH lookup, ignoring search path entry

         warning: unable to download 'https://example.com/404': HTTP error 404 ()

         response body: […]
