# Operators

| Name                                   | Syntax                                     | Associativity | Precedence |
|----------------------------------------|--------------------------------------------|---------------|------------|
| [Attribute selection]                  | *attrset* `.` *attrpath* \[ `or` *expr* \] | none          | 1          |
| Function application                   | *func* *expr*                              | left          | 2          |
| [Arithmetic negation][arithmetic]      | `-` *number*                               | none          | 3          |
| [Has attribute]                        | *attrset* `?` *attrpath*                   | none          | 4          |
| List concatenation                     | *list* `++` *list*                         | right         | 5          |
| [Multiplication][arithmetic]           | *number* `*` *number*                      | left          | 6          |
| [Division][arithmetic]                 | *number* `/` *number*                      | left          | 6          |
| [Subtraction][arithmetic]              | *number* `-` *number*                      | left          | 7          |
| [Addition][arithmetic]                 | *number* `+` *number*                      | left          | 7          |
| [String concatenation]                 | *string* `+` *string*                      | left          | 7          |
| [Path concatenation]                   | *path* `+` *path*                          | left          | 7          |
| [Path and string concatenation]        | *path* `+` *string*                        | left          | 7          |
| [String and path concatenation]        | *string* `+` *path*                        | left          | 7          |
| Logical negation (`NOT`)               | `!` *bool*                                 | none          | 8          |
| [Update]                               | *attrset* `//` *attrset*                   | right         | 9          |
| [Less than][Comparison]                | *expr* `<` *expr*                          | none          | 10         |
| [Less than or equal to][Comparison]    | *expr* `<=` *expr*                         | none          | 10         |
| [Greater than][Comparison]             | *expr* `>` *expr*                          | none          | 10         |
| [Greater than or equal to][Comparison] | *expr* `>=` *expr*                         | none          | 10         |
| [Equality]                             | *expr* `==` *expr*                         | none          | 11         |
| Inequality                             | *expr* `!=` *expr*                         | none          | 11         |
| Logical conjunction (`AND`)            | *bool* `&&` *bool*                         | left          | 12         |
| Logical disjunction (`OR`)             | *bool* <code>\|\|</code> *bool*            | left          | 13         |
| [Logical implication]                  | *bool* `->` *bool*                         | none          | 14         |
| \[Experimental\] [Function piping]     | *expr* `\|>` *func*                        | left          | 15         |
| \[Experimental\] [Function piping]     | *expr* `<\|` *func*                        | right         | 16         |

[string]: ./values.md#type-string
[path]: ./values.md#type-path
[number]: ./values.md#type-number
[list]: ./values.md#list
[attribute set]: ./values.md#attribute-set

## Attribute selection

> *attrset* `.` *attrpath* \[ `or` *expr* \]

Select the attribute denoted by attribute path *attrpath* from [attribute set] *attrset*.
If the attribute doesn’t exist, return the *expr* after `or` if provided, otherwise abort evaluation.

An attribute path is a dot-separated list of [attribute names](./values.md#attribute-set).

> *attrpath* = *name* [ `.` *name* ]...

[Attribute selection]: #attribute-selection

## Has attribute

> *attrset* `?` *attrpath*

Test whether [attribute set] *attrset* contains the attribute denoted by *attrpath*.
The result is a [Boolean] value.

[Boolean]: ./values.md#type-boolean

[Has attribute]: #has-attribute

## Arithmetic

Numbers will retain their type unless mixed with other numeric types:
Pure integer operations will always return integers, whereas any operation involving at least one floating point number returns a floating point number.

Integer overflow (of 64-bit signed integers) and division by zero are defined to throw an error.

See also [Comparison] and [Equality].

The `+` operator is overloaded to also work on strings and paths.

[arithmetic]: #arithmetic

## String concatenation

> *string* `+` *string*

Concatenate two [string]s and merge their string contexts.

[String concatenation]: #string-concatenation

## Path concatenation

> *path* `+` *path*

Concatenate two [path]s.
The result is a path.

[Path concatenation]: #path-concatenation

## Path and string concatenation

> *path* + *string*

Concatenate *[path]* with *[string]*.
The result is a path.

> **Note**
>
> The string must not have a string context that refers to a [store path].

[Path and string concatenation]: #path-and-string-concatenation

## String and path concatenation

> *string* + *path*

Concatenate *[string]* with *[path]*.
The result is a string.

> **Important**
>
> The file or directory at *path* must exist and is copied to the [store].
> The path appears in the result as the corresponding [store path].

[store path]: ../glossary.md#gloss-store-path
[store]: ../glossary.md#gloss-store

[String and path concatenation]: #string-and-path-concatenation

## Update

> *attrset1* // *attrset2*

Update [attribute set] *attrset1* with names and values from *attrset2*.

The returned attribute set will have of all the attributes in *attrset1* and *attrset2*.
If an attribute name is present in both, the attribute value from the latter is taken.

[Update]: #update

## Comparison

Comparison is

- [arithmetic] for [number]s
- lexicographic for [string]s and [path]s
- item-wise lexicographic for [list]s:
  elements at the same index in both lists are compared according to their type and skipped if they are equal.

All comparison operators are implemented in terms of `<`, and the following equivalencies hold:

| comparison   | implementation        |
|--------------|-----------------------|
| *a* `<=` *b* | `! (` *b* `<` *a* `)` |
| *a* `>`  *b* |       *b* `<` *a*     |
| *a* `>=` *b* | `! (` *a* `<` *b* `)` |

Note that the above behaviour violates IEEE 754 for floating point numbers with respect to NaN, for instance.
This may be fixed in a future major language revision.

[Comparison]: #comparison-operators

## Equality

The following equality comparison rules are followed in order:

- Comparisons are first, sometimes, performed by identity (pointer value), and whether or not this occurs varies depending on the context in which the comparison is performed; for example, through `builtins.elem`, comparison of lists, or other cases.
  The exact instances in which this occurs, aside from direct list and attribute set comparisons as discussed below, are too dependent on implementation details to meaningfully document.

  See [note on identity comparison](#identity-comparison) below.
- Comparisons between a combination of integers and floating point numbers are first converted to floating point then compared as floating point.
- Comparisons between values of differing types, besides the ones mentioned in the above rule, are unequal.
- Strings are compared as their string values, disregarding string contexts.
- Paths are compared as their absolute form (since they are stored as such).
- [Functions][function] are always considered unequal, including with themselves.
- The following are compared in the typical manner:
  - Integers
  - Floating point numbers have equality comparison per IEEE 754.

    Note that this means that just like in most languages, floating point arithmetic results are not typically equality comparable, and should instead be compared by checking that the absolute difference is less than some error margin.
  - Booleans
  - Null
- [Attribute sets][attribute set] are compared following these rules in order:
  - If both attribute sets have the same identity (via pointer equality), they are considered equal, regardless of whether the contents have reflexive equality (e.g. even if there are functions contained within).

    See [note on identity comparison](#identity-comparison) below.
  - If both attribute sets have `type = "derivation"` and have an attribute `outPath` that is equal, they are considered equal.

    This means that two results of `builtins.derivation`, regardless of other things added to their attributes via `//` afterwards (or `passthru` in nixpkgs), will compare equal if they passed the same arguments to `builtins.derivation`.
  - Otherwise, they are compared element-wise in an unspecified order.
    Although this order *may* be deterministic in some cases, this is not guaranteed, and correct code must not rely on this ordering behaviour.

    The order determines which elements are evaluated first and thus, if there are throwing values in the attribute set, which of those get evaluated, if any, before the comparison returns an unequal result.
- Lists are compared following these rules in order:
  - If both lists have the same identity (via pointer equality), they are considered equal, regardless of whether the contents have reflexive equality (e.g. even if there are functions contained within).

    See [note on identity comparison](#identity-comparison) below.
  - Otherwise, they are compared element-wise in list order.

[function]: ./constructs.md#functions

[Equality]: #equality

### Identity comparison

In the current revision of the Nix language, values are first compared by identity (pointer equality).
This means that values that are not reflexively equal (that is, they do not satisfy `a == a`), such as functions, are nonetheless sometimes compared as equal with themselves if they are placed in attribute sets or lists, or are compared through other indirect means.

Whether identity comparison applies to a given usage of the language aside from direct list and attribute set comparison is strongly dependent on implementation details to the point it is not feasible to document the exact instances.

This is rather unfortunate behaviour which is regrettably load-bearing on nixpkgs (such as with the `type` attribute of NixOS options) and cannot be changed for the time being.
It may be changed in a future major language revision.

Correct code must not rely on this behaviour.

For example:

```
nix-repl> let f = x: 1; s = { func = f; }; in [ (f == f) (s == s) ]
[ false true ]
```

## Logical implication

Equivalent to `!`*b1* `||` *b2*.

[Logical implication]: #logical-implication

## \[Experimental\] Function piping

*This language feature is still experimental and may change at any time. Enable `--extra-experimental-features pipe-operator` to use it.*

Pipes are a dedicated operator for function application, but with reverse order and a lower binding strength.
This allows you to chain function calls together in way that is more natural to read and requires less parentheses.

`a |> f b |> g` is equivalent to `g (f b a)`.
`g <| f b <| a` is equivalent to `g (f b a)`.

Example code snippet:

```nix
defaultPrefsFile = defaultPrefs
  |> lib.mapAttrsToList (
    key: value: ''
      // ${value.reason}
      pref("${key}", ${builtins.toJSON value.value});
    ''
  )
  |> lib.concatStringsSep "\n"
  |> pkgs.writeText "nixos-default-prefs.js";
```

Note how `mapAttrsToList` is called with two arguments (the lambda and `defaultPrefs`),
but moving the last argument in front of the rest improves the reading flow.
This is common for functions with long first argument, including all `map`-like functions.

[Function piping]: #experimental-function-piping
