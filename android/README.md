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

The on-screen analog stick and PS1 buttons use the same native bindings and
field walk/run threshold as hardware controllers. Connecting a physical controller
hides the touch gamepad; disconnecting it restores touch. The Settings button
opens the existing native settings menu. Touch input is cleared on pause, focus
loss and controller changes.

Complete Android settings integration
and device compatibility testing remain unfinished. The desktop file dialog is not an Android file picker.
Native save-load requests across missing discs have been tested in both directions.
Continuous campaign and physical-device testing remain unfinished.
Do not distribute this APK as a finished Android port.

Use **Export saves** in the launcher to back up both memory cards through the
system file picker. The ZIP contains the original 8192-byte PsyZ save files in
`bu00/` and `bu10/` directories. Their bytes are unchanged and can be opened in
the desktop save editor or copied into the desktop port's `memory-cards` folder.
This exports individual saves, not a combined MCR card image. Disc images,
settings and diagnostic logs are excluded; source saves are kept.

On a dedicated headless emulator, verify real picker export, both cards, exact
bytes, ZIP integrity, cancellation and source preservation with:

```sh
python3 tests/android_launcher.py --saves-only --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb"
```

The test refuses to overwrite existing saves with its fixture names.

Use **Import saves** to select a ZIP exported by this port or an original
8192-byte `BASLUS-00662000000[01][A-O]` save file. ZIPs retain both card directories;
a single file goes to card 1. Existing filenames are never replaced. Import
checks the archive, every save's original game checksum and all destinations
before creating save files. A rejected selection leaves existing saves intact.
Use the save editor to extract saves from MCR/MCD/MCS card images first.

Verify import, invalid checksums, truncated archives, path rejection,
collisions, raw saves and cancellation on a dedicated emulator:

```sh
python3 tests/android_launcher.py --save-import-only --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb"
```

Use **Export diagnostic report** in the launcher to save a ZIP through Android's
system file picker. It includes the game log, current native log, up to five
recent crash reports and basic app/device version information. Disc images,
settings and memory cards are excluded. Export runs in the background and keeps
the original logs. Reports larger than 64 MiB fail with an explicit error.

Test export and cancellation on a dedicated headless emulator:

```sh
python3 tests/android_launcher.py --report-only --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb"
```

This creates seven uniquely named synthetic reports, exports through the real
document picker, checks ZIP integrity and exact bytes for the newest five,
verifies the file whitelist and source preservation, then removes its fixtures.

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

The touch prototype was also tested with a uinput controller hotplugged after
native startup: Start and D-pad navigation reached the original title menu, and
touch returned after hardware removal. Debug and Release APK builds pass.
An instrumentation test also checks simultaneous stick/button touches, independent
pointer release, cancellation, focus loss, Activity pause and simultaneous L2/R2
against the actual Android View and JNI implementation. Physical-device testing
remains outstanding.

On a dedicated emulator with `disc1.cue` imported and no controller attached:

```sh
./android/gradlew -p android :app:assembleDebug :app:assembleDebugAndroidTest
adb -s emulator-5566 install -r android/app/build/outputs/apk/debug/app-debug.apk
adb -s emulator-5566 install -r android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb -s emulator-5566 shell am force-stop com.khasinski.parasiteeve
adb -s emulator-5566 shell am instrument -w com.khasinski.parasiteeve.test/com.khasinski.parasiteeve.TouchControlsTest
```

The test uses its own diagnostic profile and runs in the private game process.
Its runner is packaged only in the separate test APK, never in the game APK.

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

To test closing the missing-disc importer during a transaction, leave at least
512 MiB free on a dedicated emulator and run:

```sh
python3 tests/android_import_lifecycle.py --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb"
```

This copies a synthetic BIN without a CUE, observes an unfinished copy, closes
its Activity with Back and opens the launcher again. The expected import error
must reach the live launcher, the process must survive, and retry controls must
be enabled. The test removes its own uniquely named fixture. Import transactions
are serialized across launcher Activities; a replacement launcher displays the
current import's state instead of starting another writer in the staging folder.

After importing a disc, verify repeated native sessions with:

```sh
python3 tests/android_lifecycle.py --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb" --disc disc1.cue
```

This starts three short hidden GPU sessions, compares their framebuffer hashes,
checks the native VBlank count and confirms that the launcher's PID survives.
It also exercises Android Back during an interactive session and a command that
returns normally from `SDL_main`. System/keyboard Back returns to the launcher;
controller buttons retain their native SDL mappings.

## Missing disc requests

The original native request pauses the game clock/audio and opens the same
folder importer with the required disc's name. Selecting a CUE returns its local
private-storage path through JNI; the original image validator checks its actual
identity. Selecting the wrong disc cannot replace the mounted image. Cancelling
returns to the existing native retry/quit prompt.

The debug APK provides `--test-select-disc 1|2` to exercise this JNI picker and
validator without playing through a disc boundary. It is not a release option.
With imported images, run:

```sh
python3 tests/android_disc_request.py --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb" --disc1 disc1.cue --disc2 disc2.cue
```

This checks both identities, wrong-disc rejection and cancellation. Omit
`--disc2` to exercise the Disc 1 success and missing/wrong Disc 2 cases with a
single imported image. `--import-folder NAME` additionally imports the second
image from `Download/NAME` through the system picker when it is not installed.

For a real game transition, provide an original native 8192-byte save belonging
to the other disc:

```sh
python3 tests/android_disc_transition.py --serial emulator-5566 --adb "$ANDROID_HOME/platform-tools/adb" --save /path/to/save --start-disc disc1.cue --supply-disc disc2.cue --required-identity 2 --out build/android-transition
```

The debug-only `--test-android-disc-dialogs` admits the real disc request callback
in a finite headless GPU run. `testProfile` selects a fresh app-private diagnostic
directory, so the test stages its card without replacing the player's cards.
The original Continue/card/scene-loading code must request the absent image,
resume after the Android picker and reach the scene token stored in the save.
The tested routes load M273 starting with Disc 1 and M020 starting with Disc 2.
Neither diagnostic option is accepted by release APKs.

## Original native replay tests

The desktop C replay harness can also drive the installed debug APK. Its input
schedule, state assertions, exact VRAM comparison and phone-save checks remain
the same. The adapter uses a fresh Android diagnostic profile and copies its
outputs into the harness's temporary directory; it never stages the player's
cards. Use a dedicated headless emulator with `disc1.cue` already imported.

Create a local executable adapter, for example `build/android-native-replay`:

```sh
#!/bin/sh
exec python3 "$PE_ANDROID_REPLAY_DRIVER" --adb "${ANDROID_ADB:-adb}" \
  --serial "$ANDROID_SERIAL" --disc disc1.cue --native-replay "$@"
```

Then run a built native test harness from the repository root:

```sh
chmod +x build/android-native-replay
export PE_ANDROID_REPLAY_DRIVER="$PWD/tests/android_lifecycle.py"
export ANDROID_SERIAL=emulator-5566
export PE_TEST_DISC="/path/to/Parasite Eve (Disc 1).cue"
build/test-original-actor-animation --native-m022 "$PWD/build/android-native-replay"
```

`PE_TEST_DISC` is required by the C harness; Android reads the imported image
instead. The adapter accepts only finite prerecorded headless runs. It waits for
the native process to retire, transfers diagnostics and generated outputs, then
removes its private profile. A failed run exits unsuccessfully; the desktop
assertions are never replaced with Android-specific expected values.

This route has completed all 200,826 VBlanks through Carnegie Hall on an API 35
ARM64 emulator: the actual phone-created 8192-byte save and final 1 MiB VRAM
matched desktop byte for byte (FNV-1a `ddbea58f2544300e` and
`77627bc3feb8cb34`). This is not a full Android campaign or physical-device test.
