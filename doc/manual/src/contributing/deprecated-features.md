This section describes the notion of *deprecated features*, and how it fits into the big picture of the development of Lix.

# What are deprecated features?

Deprecated features are disabled by default, with the intent to eventually remove them.
Users must explicitly enable them to keep using them, by toggling the associated [deprecated feature flags](@docroot@/command-ref/conf-file.md#conf-deprecated-features).
This allows backwards compatibility and a graceful transition away from undesired features.

# Which features can be deprecated?

Undesired features should be soft-deprecated by yielding a warning when used for a significant amount of time before the can be deprecated.
Legacy obsolete feature with little to no usage may go through this process faster.
Deprecated features should have a migration path to a preferred alternative.

# Lifecycle of a deprecated feature

This description is not normative, but a feature removal may roughly happen like this:

1. Add a warning when the feature is being used.
2. Disable the feature by default, putting it behind a deprecated feature flag.
  - If disabling the feature started out as an opt-in experimental feature, turn that experimental flag into a no-op or remove it entirely.
    For example, `--extra-experimental-features=no-url-literals` becomes `--extra-deprecated-features=url-literals`.
3. Decide on a time frame for how long that feature will still be supported for backwards compatibility, and clearly communicate that in the error messages.
  - Sometimes, automatic migration to alternatives is possible, and such should be provided if possible
  - At least one NixOS release cycle should be the minimum
4. Finally remove the feature entirely, only keeping the error message for those still using it.

# Relation to language versioning

Obviously, removing anything breaks backwards compatibility.
In an ideal world, we'd have SemVer controls over the language and its features, cleanly allowing us to make breaking changes.
See https://wiki.lix.systems/books/lix-contributors/page/language-versioning and [RFC 137](https://github.com/nixos/rfcs/pull/137) for efforts on that.
However, we do not live in such an ideal world, and currently this goal is so far away, that "just disable it with some back-compat for a couple of years" is the most realistic solution, especially for comparatively minor changes.

# Currently available deprecated features

{{#include @generated@/../../../lix/libutil/deprecated-feature-descriptions.md}}
