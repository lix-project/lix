# Upgrading Lix

<div class="warning">

FIXME(Lix): does Lix forward to the installer for `nix upgrade-nix`? Should it, if present? Lix *should* restart the daemon for you [but currently doesn't (issue)](https://git.lix.systems/lix-project/lix/issues/267).

</div>

**For instructions to switch to Lix**, see <https://lix.systems/install>.

Lix may be upgraded by running `nix upgrade-nix` and then restarting the Nix daemon.

## Restarting daemon on Linux

`sudo systemctl daemon-reload && sudo systemctl restart nix-daemon`

## Restarting daemon on macOS

<div class="warning">

FIXME(Lix): Write instructions that, according to the [beta installation guide](https://wiki.lix.systems/link/1) do not sometimes crash macOS (?!)

</div>
