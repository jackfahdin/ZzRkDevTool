// Works around a missing libxkbcommon-x11 dev symlink on this machine: the
// runtime library (libxkbcommon-x11.so.0) is installed but the linker-name
// symlink (libxkbcommon-x11.so, shipped by libxkbcommon-x11-dev) is not.
// The proper fix is `sudo apt install libxkbcommon-x11-dev`; until then we
// link against a private symlink to the runtime library.

use std::path::Path;

fn main() {
    let runtime = Path::new("/usr/lib/x86_64-linux-gnu/libxkbcommon-x11.so.0");
    let dev_link = Path::new("/usr/lib/x86_64-linux-gnu/libxkbcommon-x11.so");
    let out_dir = std::env::var("OUT_DIR").unwrap();
    let out_dir = Path::new(&out_dir);

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rerun-if-changed=build.rs");

    if !dev_link.exists() && runtime.exists() {
        let link = out_dir.join("libxkbcommon-x11.so");
        let _ = std::fs::remove_file(&link);
        std::os::unix::fs::symlink(runtime, &link)
            .expect("failed to symlink libxkbcommon-x11.so");
    }

    // cargo:rustc-link-arg emitted by a dependency's build script does not
    // reach this binary's link step, so re-emit the rpaths here using the
    // metadata zzrk-sys exports (its manifest sets `links = "zzrk_ffi"`).
    for var in ["DEP_ZZRK_FFI_LIBDIR", "DEP_ZZRK_FFI_LIBUSBDIR"] {
        if let Ok(dir) = std::env::var(var) {
            println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
        }
    }
}
