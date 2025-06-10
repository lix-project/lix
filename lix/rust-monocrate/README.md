# What the hell is this?

This is a cursed solution to the intersection of the following problems:

- Lix is separated into multiple libraries, which are intended to be buildable as dynamic/shared libraries *or* statlic libraries.
- Those libraries are linked into the Lix executable binary.
- Static libraries also statically link their library dependencies.
- We want to be able to use Rust code in multiple places in the codebase.
- Meson has poor support for Rust and non-Rust sources in the same target, with [internal errors](https://github.com/mesonbuild/meson/issues/15435) preventing mixed targets from linking against non-mixed targets.
- Linking more than one Rust `crate-type = staticlib` is [unsupported](https://github.com/rust-lang/rust/issues/44322) with Rust's standard library.
- Meson [does not support dynamically linking the Rust standard library for `crate-type = staticlib`](https://mesonbuild.com/Release-notes-for-1-9-0.html#new-experimental-option-rust_dynamic_std).
- Meson [ignores `link_args` for Rust targets](https://github.com/mesonbuild/meson/issues/13538)
- Meson [does not set soname of Rust cdylibs](https://github.com/mesonbuild/meson/issues/13537)

So! What do we do? For any Rust code that Lix wants to use, we always link to a single crate, which will reëxport Rust code from any crate (in-tree or out-of-tree) we wish to use.
This "monocrate" is *always* built as a static library, and anything in Lix that depends on Rust code will statically link it in.
When the Lix executable is linked against Lix libraries that are built statically, we do *not* `link_whole` them.
As far as we::Qyriad can tell, this is sufficient to not break everything.
But long-term, we should probably solve this problem by migrating to Rust code at top-level, and having Rustc do the final link.
