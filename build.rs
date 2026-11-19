use std::{env, fs, path::PathBuf, process::Command};

fn main() {
    println!("cargo:rerun-if-changed=resources/launcher/VoltChanger.ico");
    println!("cargo:rerun-if-changed=Cargo.toml");
    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }
    let sdk = PathBuf::from(
        env::var_os("ProgramFiles(x86)").expect("Windows SDK requires ProgramFiles(x86)"),
    )
    .join("Windows Kits/10/bin");
    let mut compilers: Vec<_> = fs::read_dir(&sdk)
        .expect("Install the Windows SDK")
        .filter_map(Result::ok)
        .map(|e| e.path().join("x64/rc.exe"))
        .filter(|p| p.is_file())
        .collect();
    compilers.sort();
    let compiler = compilers
        .last()
        .expect("Windows SDK resource compiler rc.exe missing");
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let icon = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap())
        .join("resources/launcher/VoltChanger.ico");
    let version = env::var("CARGO_PKG_VERSION").unwrap();
    let numeric = version
        .split('.')
        .map(|v| v.split('-').next().unwrap())
        .collect::<Vec<_>>()
        .join(",");
    let source = format!(
        r#"
1 ICON "{}"
1 VERSIONINFO
FILEVERSION {},0
PRODUCTVERSION {},0
FILEFLAGSMASK 0x3fL
FILEFLAGS 0
FILEOS 0x40004L
FILETYPE 0x1L
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "CompanyName", "VoltCoreLabs\0"
   VALUE "FileDescription", "VoltChanger - Dota 2 companion\0"
   VALUE "FileVersion", "{}\0"
   VALUE "InternalName", "VoltChanger\0"
   VALUE "OriginalFilename", "VoltChanger.exe\0"
   VALUE "ProductName", "VoltChanger\0"
   VALUE "ProductVersion", "{}\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x409, 1200
 END
END
"#,
        icon.display().to_string().replace('\\', "/"),
        numeric,
        numeric,
        version,
        version
    );
    let rc = out.join("voltchanger.rc");
    let bytes: Vec<u8> = std::iter::once(0xfeffu16)
        .chain(source.encode_utf16())
        .flat_map(u16::to_le_bytes)
        .collect();
    fs::write(&rc, bytes).unwrap();
    let resource = out.join("voltchanger.res");
    let result = Command::new(compiler)
        .args(["/nologo", "/fo"])
        .arg(&resource)
        .arg(&rc)
        .status()
        .expect("Run rc.exe");
    assert!(result.success(), "Windows resource compilation failed");
    println!(
        "cargo:rustc-link-arg-bin=VoltChanger={}",
        resource.display()
    );
}
