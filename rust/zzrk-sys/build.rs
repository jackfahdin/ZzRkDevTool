// Build script: compiles zzrk_ffi as a shared library via CMake and
// generates Rust bindings for zzrk_ffi.h with bindgen.

use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn run(cmd: &mut Command, what: &str) {
    let status = cmd
        .status()
        .unwrap_or_else(|e| panic!("failed to spawn {what}: {e}"));
    assert!(status.success(), "{what} failed with {status}");
}

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let project_root = manifest_dir
        .parent()
        .and_then(Path::parent)
        .expect("zzrk-sys must live in <project>/rust/zzrk-sys")
        .to_path_buf();
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let build_dir = out_dir.join("zzrk-cmake-build");

    let header = project_root.join("src/ffi/include/zzrk/ffi/zzrk_ffi.h");

    println!("cargo:rerun-if-changed={}", header.display());
    println!(
        "cargo:rerun-if-changed={}",
        project_root.join("src/ffi/CMakeLists.txt").display()
    );
    for dir in [
        "src/core",
        "src/ffi",
        "src/image",
        "src/platform",
        "src/protocol",
        "src/session",
        "src/storage",
        "src/transport",
        "src/workflow",
    ] {
        println!(
            "cargo:rerun-if-changed={}",
            project_root.join(dir).display()
        );
    }

    let profile = env::var("PROFILE").unwrap_or_else(|_| "debug".into());
    let build_type = if profile == "release" {
        "Release"
    } else {
        "RelWithDebInfo"
    };

    run(
        Command::new("cmake")
            .arg("-S")
            .arg(&project_root)
            .arg("-B")
            .arg(&build_dir)
            .arg("-G")
            .arg("Ninja")
            .arg(format!("-DCMAKE_BUILD_TYPE={build_type}"))
            .arg("-DZZRK_BUILD_GUI=OFF")
            .arg("-DZZRK_BUILD_CLI=OFF")
            .arg("-DZZRK_BUILD_TESTS=OFF")
            .arg("-DZZRK_FFI_SHARED=ON")
            .arg("-DCMAKE_POSITION_INDEPENDENT_CODE=ON"),
        "cmake configure",
    );

    run(
        Command::new("cmake")
            .arg("--build")
            .arg(&build_dir)
            .arg("--target")
            .arg("zzrk_ffi")
            .arg("--parallel"),
        "cmake build zzrk_ffi",
    );

    let lib_dir = build_dir.join("src/ffi");
    assert!(
        lib_dir.join("libzzrk_ffi.so").exists(),
        "libzzrk_ffi.so not found in {}",
        lib_dir.display()
    );

    println!("cargo:rustc-link-search=native={}", lib_dir.display());
    println!("cargo:rustc-link-lib=dylib=zzrk_ffi");
    // rpath for this crate's own test binaries; downstream binaries get the
    // same paths via the DEP_ZZRK_FFI_* metadata (see gui_gpui/build.rs).
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", lib_dir.display());
    println!("cargo:libdir={}", lib_dir.display());

    // libzzrk_ffi.so links the vendored shared libusb; make it resolvable
    // both at link time and at runtime.
    let libusb_dir = project_root.join("thirdParty/libusb/lib");
    if libusb_dir.exists() {
        println!("cargo:rustc-link-search=native={}", libusb_dir.display());
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", libusb_dir.display());
        println!("cargo:libusbdir={}", libusb_dir.display());
    }

    let bindings = bindgen::Builder::default()
        .header(header.to_string_lossy())
        .allowlist_function("zzrk_.*")
        .allowlist_type("zzrk_.*")
        .allowlist_var("ZZRK_.*")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("bindgen failed on zzrk_ffi.h");

    bindings
        .write_to_file(out_dir.join("bindings.rs"))
        .expect("failed to write bindings.rs");
}
