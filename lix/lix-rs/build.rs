use std::{env, fs};

fn main() {
    // this is a hack to let rust-analyzer find the generated code. we could
    // also include! from MESON_BUILD_DIR directly, but rust-analyzer cannot
    // be told to look there. not with any method we came across yet anyway.
    fs::write(
        format!("{}/generated.rs", env::var("OUT_DIR").expect("OUT_DIR")),
        fs::read(format!(
            "{}/lix/lix-rs/main.zng.rs",
            env::var("MESON_BUILD_DIR").expect("MESON_BUILD_DIR")
        ))
        .expect("read main.zng.rs"),
    )
    .expect("write generated.rs");
}
