# Android development build

This is an experimental ARM64 Android frontend for the original C game modules,
not an emulator or a separate gameplay implementation. Android 8 (API 26) or newer
is required. It is not yet a supported release target.

## Build

Initialize the repository submodules, install JDK 21, Android SDK 35 and NDK
28.2.13676358, then run from the repository root:

```sh
./android/gradlew -p android :app:assembleDebug
```

Set `JAVA_HOME` and `ANDROID_HOME` if the tools are not discovered automatically.
The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.

## Disc images

Use **Import disc images** to select a folder containing your own CUE/BIN files.
Files are copied into app-private storage, so allow enough free space for both
images and the temporary import. Existing files are never overwritten. Import the
other disc in the same way later; its filenames must differ from installed files.
The launcher offers a starting image and supplies the other imported CUE to the
native disc reader. Use one image per game disc; extra variants are not supported.
Uninstalling the app deletes its imported images, settings and saves.

A controller is currently required. Touch controls, Android settings/disc-missing
prompts, save import/export, crash report export and device compatibility testing
remain unfinished. The desktop file dialog is not an Android file picker.
Automatic two-disc swap still needs an end-to-end test.
Do not distribute this APK as a finished Android port.

The native game runs in the app's private `:game` process. Its original `exit()`
and diagnostic frame-limit exits therefore leave the launcher alive. A normally
returning native command retires that process after SDL cleanup as well, so the
next session starts with fresh C globals and a fresh native audio/video clock.

## Verification so far

A headless API 35 ARM64 emulator loaded a real Disc 1 CUE/BIN, decoded the opening
movie and completed a 25,000-VBlank native-input replay into the opening field
scene (scene `0xa8000148`, dialogue page 13). The game wrote its framebuffer, VRAM,
audio and state diagnostics. Starting through the launcher's Play button also
rendered the opening movie. These checks do not establish full-game compatibility
or audio correctness on physical devices. The APK also downloaded and installed
the production v0.1 HD pack through HTTPS using curl/MbedTLS with certificate
verification enabled, then passed `--verify-backgrounds` on the installed pack.

Debug APKs accept the native diagnostic argument array through the `arguments`
intent extra on GameActivity. Release APKs ignore that extra. Headless debug runs
use the offline audio clock and write `diagnostics.log` into app-private storage;
retrieve it with `adb exec-out run-as com.khasinski.parasiteeve cat files/diagnostics.log`.

Run the SAF regression on a dedicated English-language emulator with an installed
debug APK (it uses synthetic images and stops the app when finished):

```sh
python3 tests/android_launcher.py --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb"
```

It exercises the system folder picker, verifies exact imported bytes, checks CUE
discovery and confirms that a repeated import rejects duplicates without changing
the first copy.

After importing a disc, verify repeated native sessions with:

```sh
python3 tests/android_lifecycle.py --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb" --disc disc1.cue
```

This starts three short hidden GPU sessions, compares their framebuffer hashes,
checks the native VBlank count and confirms that the launcher's PID survives.
It also exercises Android Back during an interactive session and a command that
returns normally from `SDL_main`. System/keyboard Back returns to the launcher;
controller buttons retain their native SDL mappings.
