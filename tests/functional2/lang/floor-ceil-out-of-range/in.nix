let
  big = 65536 * 65536 * 65536 * 32767;
in
{
  floor-bad-weird-int = builtins.floor (big - 1);
  floor-okay-weird-int = builtins.floor big;
  ceil-bad-weird-int = builtins.ceil (big - 1);
  ceil-okay-weird-int = builtins.ceil big;
}
