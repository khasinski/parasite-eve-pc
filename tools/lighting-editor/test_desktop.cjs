/* Packaged application test. Every native window stays hidden. */
const { _electron } = require("playwright");
const fs = require("node:fs");
const path = require("node:path");
const os = require("node:os");
const assert = require("node:assert/strict");
const { execFileSync } = require("node:child_process");
const binary = process.env.PE_LIGHTING_APP,
  disc = process.env.PE_TEST_DISC,
  model = process.env.PE_LIGHTING_MODEL;
if (!binary || !disc || !model) {
  console.log("Set PE_LIGHTING_APP, PE_TEST_DISC and PE_LIGHTING_MODEL");
  process.exit(77);
}
const data = fs.mkdtempSync(path.join(os.tmpdir(), "pe-lighting-desktop-"));
const evidence = path.resolve(
  process.env.PE_LIGHTING_EVIDENCE || "build/lighting-editor/desktop-evidence",
);
fs.mkdirSync(evidence, { recursive: true });
let app;
async function launch(args = []) {
  app = await _electron.launch({
    executablePath: binary,
    args: [
      `--data-dir=${data}`,
      "--hidden=1",
      ...(process.platform === "linux"
        ? [
            "--no-sandbox",
            "--use-gl=angle",
            "--use-angle=swiftshader",
            "--enable-unsafe-swiftshader",
          ]
        : []),
      ...args,
    ],
    timeout: 30000,
    env: {
      ...process.env,
      ...(process.platform === "win32" ? {} : { PATH: "/usr/bin:/bin" }),
      PE_LIGHTING_PYTHON: "/missing/python",
    },
  });
  const page = await app.firstWindow();
  return page;
}
async function ready(page) {
  try {
    await page.waitForFunction(
      () => document.body.dataset.ready === "true",
      null,
      { timeout: 30000 },
    );
  } catch (error) {
    console.error("Renderer status:", await page.locator("body").innerText());
    await page.screenshot({ path: path.join(evidence, "failed-startup.png") });
    throw error;
  }
}
function children() {
  if (process.platform === "win32") {
    const escaped = data.replaceAll("'", "''");
    return execFileSync(
      "powershell.exe",
      [
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        `Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'lighting-backend*' -and $_.CommandLine.Contains('${escaped}') } | Select-Object -ExpandProperty ProcessId`,
      ],
      { encoding: "utf8" },
    )
      .trim()
      .split(/\s+/)
      .filter(Boolean)
      .map(Number);
  }
  return execFileSync("ps", ["-axo", "pid,args"], { encoding: "utf8" })
    .split("\n")
    .filter((line) => line.includes("lighting-backend") && line.includes(data))
    .map((line) => +line.trim().split(/\s+/)[0]);
}
async function closed() {
  await app.close();
  app = null;
  await new Promise((r) => setTimeout(r, 900));
  assert.deepEqual(
    children(),
    [],
    "No asset reader may remain after the app exits",
  );
}
(async () => {
  try {
    // An invalid remembered path must still allow the native setup dialogs to recover.
    let page = await launch([`--disc=${data}/missing.cue`]);
    await page.waitForSelector("#openWorkspace");
    assert.match(
      await page.locator("#setupError").textContent(),
      /Choose an existing USA disc/,
    );
    const native = await app.evaluate(({ BrowserWindow, app }) => ({
      packaged: app.isPackaged,
      prefs:
        BrowserWindow.getAllWindows()[0].webContents.getLastWebPreferences(),
    }));
    assert(native.packaged);
    assert(
      native.prefs.contextIsolation &&
        native.prefs.sandbox &&
        !native.prefs.nodeIntegration,
    );
    await app.evaluate(({ dialog }) => {
      dialog.showOpenDialog = async () => ({ canceled: true, filePaths: [] });
    });
    await page.click("#chooseDisc");
    assert.equal(
      await page.locator("#disc").inputValue(),
      `${data}/missing.cue`,
    );
    await app.evaluate(
      ({ dialog }, values) => {
        dialog.showOpenDialog = async (_window, options) => ({
          canceled: false,
          filePaths: [
            options.title.includes("disc image")
              ? values.disc
              : options.title.includes("model capture")
                ? values.model
                : values.profiles,
          ],
        });
      },
      { disc, model, profiles: path.join(data, "profiles") },
    );
    await page.click("#chooseDisc");
    await page.click("#chooseModel");
    await page.click("#chooseProfiles");
    await page.click("#openWorkspace");
    await ready(page);
    assert.equal(page.url(), "pe-lighting://app/");
    assert.equal(await page.locator("html").getAttribute("lang"), "en");
    assert.equal(await page.locator("#save").textContent(), "Save room");
    assert(
      !/[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]/.test(await page.locator("body").innerText()),
    );
    assert.equal(await page.evaluate(() => typeof require), "undefined");
    assert.equal(await page.evaluate(() => typeof process), "undefined");
    const settings = JSON.parse(
      fs.readFileSync(path.join(data, "settings.json")),
    );
    assert(fs.existsSync(settings.model + ".vram"));
    assert(settings.model.startsWith(data));
    assert.notEqual(settings.model, model);
    assert.equal(children().length, 1);
    await page.locator("#yaw").fill("37");
    await page.locator("#ambientIntensity").fill("0.65");
    await page.locator("#lights button").first().click();
    await page.locator("#lightColor").fill("#80cfff");
    await page.locator("#lightIntensity").fill("18");
    await page.screenshot({ path: path.join(evidence, "desktop.png") });
    // A stable application origin preserves an unsaved draft despite a new HTTP port.
    await closed();
    page = await launch();
    await ready(page);
    assert.equal(await page.locator("#yaw").inputValue(), "37");
    assert.equal(await page.locator("#ambientIntensity").inputValue(), "0.65");
    await app.evaluate(({ Menu }) => {
      Menu.getApplicationMenu()
        .items.find((i) => i.label === "File")
        .submenu.items.find((i) => i.label === "Save Room")
        .click();
    });
    await page.waitForFunction(() =>
      document.querySelector("#saveState").textContent.startsWith("Saved"),
    );
    const profile = JSON.parse(
      fs.readFileSync(path.join(data, "profiles/M020.json")),
    ).profile;
    assert.equal(profile.aya.yaw, 37);
    assert.equal(profile.lights[0].color, "#80cfff");
    const exported = path.join(evidence, "export.json");
    await app.evaluate(({ session }, file) => {
      session.defaultSession.once("will-download", (_event, item) =>
        item.setSavePath(file),
      );
    }, exported);
    await page.click("#export");
    await page.waitForTimeout(500);
    assert.equal(JSON.parse(fs.readFileSync(exported)).aya.yaw, 37);
    const png = path.join(evidence, "composite.png");
    await app.evaluate(({ session }, file) => {
      session.defaultSession.once("will-download", (_event, item) =>
        item.setSavePath(file),
      );
    }, png);
    await page.click("#screenshot");
    await page.waitForTimeout(500);
    assert(fs.statSync(png).size > 10000);
    // Reopening a saved profile works without the repo Python environment.
    await closed();
    page = await launch();
    await ready(page);
    assert.equal(await page.locator("#yaw").inputValue(), "37");
    assert.equal(
      await page.locator("#saveState").textContent(),
      "Profile loaded",
    );
    await closed();
    fs.writeFileSync(
      path.join(evidence, "proof.json"),
      JSON.stringify(
        {
          packaged: true,
          english: true,
          bundled_backend: true,
          native_dialogs: true,
          stable_drafts: true,
          native_exports: true,
          profile_roundtrip: true,
          child_cleanup: true,
          renderer_isolated: true,
        },
        null,
        2,
      ) + "\n",
    );
    console.log(
      "PASS: packaged app, English UI, native file pickers/cancel/recovery, private capture copy, native JSON/PNG exports, persistent drafts/profiles, isolated renderer and helper-process cleanup",
    );
  } finally {
    if (app) await app.close();
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
