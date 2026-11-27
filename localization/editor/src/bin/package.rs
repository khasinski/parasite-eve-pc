//! Build and smoke-test the standalone editor on the current desktop platform.
//!
//!     cargo run --release --bin package -- --out editor-dist
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

use pe_translation_editor::app::TITLE;
use pe_translation_editor::cjk::FontSource;
use pe_translation_editor::roundtrip::run;

type Failure = Box<dyn std::error::Error>;

fn platform() -> Result<(&'static str, &'static str), Failure> {
    let system = match std::env::consts::OS {
        "windows" => "windows",
        "macos" => "macos",
        "linux" => "linux",
        other => return Err(format!("unsupported platform {other}").into()),
    };
    let arch = match std::env::consts::ARCH {
        "x86_64" => "x64",
        "aarch64" => "arm64",
        other => other,
    };
    Ok((system, arch))
}

fn info_plist() -> String {
    format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>{TITLE}</string>
  <key>CFBundleDisplayName</key><string>{TITLE}</string>
  <key>CFBundleExecutable</key><string>{TITLE}</string>
  <key>CFBundleIdentifier</key><string>io.github.parasite-eve-pc.translation-editor</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>{}</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
"#,
        env!("CARGO_PKG_VERSION")
    )
}

/// The application under `release`: a bundle on macOS, a folder elsewhere. Returns its executable.
fn install_application(release: &Path, built: &Path, system: &str) -> Result<PathBuf, Failure> {
    let executable = match system {
        "macos" => {
            let contents = release.join(format!("{TITLE}.app")).join("Contents");
            fs::create_dir_all(contents.join("MacOS"))?;
            fs::write(contents.join("Info.plist"), info_plist())?;
            contents.join("MacOS").join(TITLE)
        }
        "windows" => release.join(TITLE).join(format!("{TITLE}.exe")),
        _ => release.join(TITLE).join(TITLE),
    };
    fs::create_dir_all(executable.parent().ok_or("no parent folder")?)?;
    fs::copy(built, &executable)?;
    if system == "macos" {
        // The bundle needs its own signature on Apple Silicon. This is not Developer ID signing.
        let bundle = release.join(format!("{TITLE}.app"));
        let signed = Command::new("codesign").args(["--force", "--deep", "--sign", "-"]).arg(&bundle).status()?;
        if !signed.success() {
            return Err("codesign failed".into());
        }
    }
    Ok(executable)
}

fn files_under(root: &Path, folder: &Path, out: &mut Vec<PathBuf>) -> Result<(), Failure> {
    for entry in fs::read_dir(folder)? {
        let path = entry?.path();
        if path.is_dir() {
            files_under(root, &path, out)?;
        } else {
            out.push(path);
        }
    }
    Ok(())
}

fn write_zip(root: &Path, target: &Path) -> Result<(), Failure> {
    use std::io::Write;
    let mut files = Vec::new();
    files_under(root, root, &mut files)?;
    files.sort();
    let mut archive = zip::ZipWriter::new(fs::File::create(target)?);
    let options = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    for file in files {
        let name = file.strip_prefix(root)?.to_string_lossy().replace('\\', "/");
        archive.start_file(name, options)?;
        archive.write_all(&fs::read(file)?)?;
    }
    archive.finish()?;
    Ok(())
}

fn write_tar_gz(root: &Path, target: &Path) -> Result<(), Failure> {
    let encoder = flate2::write::GzEncoder::new(fs::File::create(target)?, flate2::Compression::default());
    let mut archive = tar::Builder::new(encoder);
    archive.append_dir_all(".", root)?;
    archive.into_inner()?.finish()?;
    Ok(())
}

fn build(output: &Path) -> Result<(), Failure> {
    let (system, arch) = platform()?;
    let name = format!("pe-translation-editor-{system}-{arch}");
    let editor = Path::new(env!("CARGO_MANIFEST_DIR"));
    let cargo = std::env::var_os("CARGO").unwrap_or_else(|| "cargo".into());
    let compiled = Command::new(cargo).args(["build", "--release", "--bin", "pe-translation-editor"]).current_dir(editor).status()?;
    if !compiled.success() {
        return Err("cargo build failed".into());
    }
    let built = std::env::current_exe()?.with_file_name(format!("pe-translation-editor{}", std::env::consts::EXE_SUFFIX));
    fs::create_dir_all(output)?;
    let output = std::path::absolute(output)?;
    let work = tempfile::Builder::new().prefix("pe-editor-").tempdir()?;
    let release = work.path().join("release");
    fs::create_dir(&release)?;
    run(&release, None, &FontSource::Demo)?;
    fs::copy(editor.join("START-HERE.md"), release.join("START-HERE.md"))?;
    fs::copy(editor.join("fonts/OFL.txt"), release.join("FONT-LICENSE.txt"))?;
    fs::copy(editor.join("fonts/SOURCE.md"), release.join("FONT-SOURCE.md"))?;
    let executable = install_application(&release, &built, system)?;

    let proof = work.path().join("smoke.txt");
    let status = Command::new(&executable).arg("--smoke").arg(&proof).current_dir(work.path()).status()?;
    if !status.success() {
        return Err(format!("the packaged demo exited with {status}").into());
    }
    let result = fs::read_to_string(&proof)?;
    if !result.starts_with("2 compiled box(es). Layout and control checks passed.") {
        return Err(format!("Packaged demo did not pass: {result}").into());
    }
    println!("{result}");

    let archive = output.join(format!("{name}{}", if system == "windows" { ".zip" } else { ".tar.gz" }));
    if system == "windows" {
        write_zip(&release, &archive)?;
    } else {
        write_tar_gz(&release, &archive)?;
    }
    println!("Built and tested {}", archive.display());
    Ok(())
}

fn main() -> ExitCode {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    let [flag, output] = args.as_slice() else {
        eprintln!("usage: package --out DIR");
        return ExitCode::from(2);
    };
    if flag != "--out" {
        eprintln!("usage: package --out DIR");
        return ExitCode::from(2);
    }
    match build(Path::new(output)) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
