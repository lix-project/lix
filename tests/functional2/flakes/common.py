from testlib.utils import FileDeclaration, get_global_asset_pack, CopyTemplate, File
from testlib.environ import environ


def simple_flake() -> FileDeclaration:
    return get_global_asset_pack("simple-drv") | {
        "flake.nix": CopyTemplate("assets/simple-flake.nix", {"system": environ.get("system")})
    }


def dependent_flake() -> FileDeclaration:
    return {
        "flake.nix": File(f"""{{
          outputs = {{ self, flake1 }}: {{
            packages.{environ.get("system")}.default = flake1.packages.{environ.get("system")}.default;
            expr = assert builtins.pathExists ./flake.lock; 123;
          }};
        }}""")
    }
