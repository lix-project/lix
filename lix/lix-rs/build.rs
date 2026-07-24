use std::{env, fs};

fn main() {
    // this is a hack to let rust-analyzer find the generated code. we could
    // also include! from MESON_BUILD_DIR directly, but rust-analyzer cannot
    // be told to look there. not with any method we came across yet anyway.
    let generated = format!(
        "{}/lix/lix-rs/main.zng.rs",
        env::var("MESON_BUILD_DIR").expect("MESON_BUILD_DIR")
    );
    fs::write(
        format!("{}/generated.rs", env::var("OUT_DIR").expect("OUT_DIR")),
        fs::read(&generated).expect("read main.zng.rs"),
    )
    .expect("write generated.rs");
    println!("cargo::rerun-if-changed={generated}");

    println!("cargo::rerun-if-env-changed=CXX_LINK_DIR_FOR_TEST");
    if let Some(dir) = env::var("CXX_LINK_DIR_FOR_TEST").ok() {
        println!("cargo::rustc-link-search={}", dir);
        println!("cargo::rustc-link-lib=static=lix_cpp_static");
    }

    // pull link args for each dependency we'll need to fully link.
    println!("cargo::rerun-if-env-changed=CXX_LINK_LIBS_FOR_TEST");
    for dep in env::var("CXX_LINK_LIBS_FOR_TEST")
        .ok()
        .into_iter()
        .flat_map(|s| s.split(':').map(|s| s.to_string()).collect::<Vec<_>>())
    {
        // meson has already checked that everything we need exists
        let _ = pkg_config::probe_library(&dep);
    }

    // explicitly pass sanitizer args. we can't use -Zsanitizer or -Zexternal-clangrt
    // even with RUSTC_BOOTSTRAP because that will confuse the hell out of dependency
    // crate tests when they're run. since we only need this for our own tests we can
    // be quite careless about unnecessary dependencies, so just splat all arguments.
    println!("cargo::rerun-if-env-changed=CXX_TEST_SANITIZERS");
    for arg in env::var("CXX_TEST_SANITIZERS")
        .ok()
        .iter()
        .flat_map(|s| s.split(':'))
    {
        println!("cargo::rustc-link-arg={arg}");
    }
}
