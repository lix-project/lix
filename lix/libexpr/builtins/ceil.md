---
name: ceil
args: [number]
---
Returns an integer value containing the smallest integer greater than or equal to *number* (which is either a floating-point or integer value).

If the result is out of range for the integer type, such as NaN, infinity, or a number with magnitude out of range, Lix throws an evaluation error.

Lix currently throws an evaluation error for some *integer* inputs between 2\*\*52 and 2\*\*63 - 1 as those previously experienced floating-point precision loss due to a Nix bug (https://github.com/NixOS/nix/issues/12899).
In a future release, such integers will be passed through.
