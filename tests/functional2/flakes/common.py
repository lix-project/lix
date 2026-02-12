from testlib.utils import FileDeclaration, get_global_asset_pack, CopyTemplate
from testlib.environ import environ


def simple_flake() -> FileDeclaration:
    return get_global_asset_pack("simple-drv") | {
        "flake.nix": CopyTemplate("assets/simple-flake.nix", {"system": environ.get("system")})
    }
