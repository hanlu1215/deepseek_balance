// 构建脚本：把 exe 图标（仓库根目录的 ks.ico）和应用清单（DPI 感知 +
// comctl32 v6）编进 exe 资源。思路和根目录 CMakeLists.txt 一致：
// rc 脚本现场生成，图标和清单用绝对路径塞进去，从哪个目录构建都能找得到。
// ks.ico 缺失时只警告、不报错，只是 exe 用系统默认图标。

use std::env;
use std::fs;
use std::path::Path;

fn main() {
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR");
    let root = Path::new(&manifest_dir);
    let icon = root.join("../ks.ico");
    let manifest = root.join("app.manifest");

    // rc 脚本里路径一律用正斜杠：windres 会把反斜杠当转义序列。
    let canonical = |p: &Path| {
        p.canonicalize()
            .unwrap_or_else(|_| p.to_path_buf())
            .display()
            .to_string()
            .replace('\\', "/")
    };

    let icon_line = if icon.exists() {
        format!("IDI_APPICON ICON \"{}\"", canonical(&icon))
    } else {
        println!("cargo:warning=找不到图标 {}，生成的 exe 将没有自定义图标。", icon.display());
        "// 没找到 ks.ico，本次构建不带图标资源".to_string()
    };

    // 版本信息里的字符串一律保持 ASCII：rc 脚本的编码在不同工具链下不一致，
    // 塞中文进去容易变乱码。
    let rc = format!(
        r#"
#define IDI_APPICON 1

// exe 图标用仓库根目录的 ks.ico（没有这个文件时这行会变成一句注释）
{icon_line}

// 应用清单：comctl32 v6（右键菜单是现代样式）+ 系统 DPI 感知
1 24 "{manifest}"

1 VERSIONINFO
FILEVERSION     1,0,0,0
PRODUCTVERSION  1,0,0,0
FILEFLAGSMASK   0x3fL
FILEFLAGS       0x0L
FILEOS          0x40004L
FILETYPE        0x1L
FILESUBTYPE     0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "CompanyName",      "DeepSeek Balance Cube\0"
            VALUE "FileDescription",  "DeepSeek Balance Cube (Rust)\0"
            VALUE "FileVersion",      "1.0.0.0\0"
            VALUE "InternalName",     "deepseek_balance_cube\0"
            VALUE "OriginalFilename", "deepseek_balance_cube.exe\0"
            VALUE "ProductName",      "DeepSeek Balance Cube\0"
            VALUE "ProductVersion",   "1.0.0.0\0"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 0x04B0
    END
END
"#,
        icon_line = icon_line,
        manifest = canonical(&manifest),
    );

    let out_dir = env::var("OUT_DIR").expect("OUT_DIR");
    let rc_path = Path::new(&out_dir).join("app.rc");
    fs::write(&rc_path, rc).expect("写入 app.rc 失败");

    println!("cargo:rerun-if-changed=app.manifest");
    println!("cargo:rerun-if-changed={}", icon.display());
    println!("cargo:rerun-if-changed=build.rs");

    let _ = embed_resource::compile(&rc_path, embed_resource::NONE);
}
