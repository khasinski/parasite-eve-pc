const {
  app,
  BrowserWindow,
  Menu,
  dialog,
  ipcMain,
  protocol,
  net,
  session,
} = require("electron");
const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");
const crypto = require("node:crypto");
const { pathToFileURL } = require("node:url");
const NAME = "Parasite Eve Lighting Lab",
  SCHEME = "pe-lighting";
const argument = (name) =>
  process.argv.find((a) => a.startsWith(`--${name}=`))?.slice(name.length + 3);
app.setName(NAME);
app.commandLine.appendSwitch("lang", "en-US");
app.setPath(
  "userData",
  argument("data-dir") || path.join(app.getPath("appData"), NAME),
);
protocol.registerSchemesAsPrivileged([
  {
    scheme: SCHEME,
    privileges: {
      standard: true,
      secure: true,
      supportFetchAPI: true,
      corsEnabled: true,
      stream: true,
    },
  },
]);
let window = null,
  backend = null,
  backendURL = null,
  settings = {},
  quitting = false;
const root = () => app.getAppPath();
const settingsPath = () => path.join(app.getPath("userData"), "settings.json");
function persist() {
  fs.mkdirSync(app.getPath("userData"), { recursive: true });
  const temporary = settingsPath() + ".partial";
  fs.writeFileSync(temporary, JSON.stringify(settings, null, 2) + "\n");
  fs.renameSync(temporary, settingsPath());
}
function loadSettings() {
  try {
    settings = JSON.parse(fs.readFileSync(settingsPath(), "utf8"));
  } catch {
    settings = {};
  }
  for (const field of ["disc", "model", "profiles"])
    if (argument(field)) settings[field] = argument(field);
  settings.profiles ||= path.join(app.getPath("userData"), "profiles");
}
function validSources(values) {
  if (!values || typeof values.disc !== "string" || !fs.existsSync(values.disc))
    throw Error("Choose an existing USA disc image.");
  if (!/\.(cue|bin|iso)$/i.test(values.disc))
    throw Error("Choose a CUE, BIN or ISO disc image.");
  if (typeof values.model !== "string" || !fs.existsSync(values.model))
    throw Error("Choose an original Aya model capture (.json).");
  const capture = JSON.parse(fs.readFileSync(values.model, "utf8"));
  if (
    capture.format !== 1 ||
    !capture.vertices?.length ||
    !capture.faces?.length
  )
    throw Error("This is not an Aya model capture.");
  if (
    !fs.existsSync(values.model + ".vram") ||
    fs.statSync(values.model + ".vram").size !== 1048576
  )
    throw Error(
      "The matching .json.vram file must be beside the model capture.",
    );
}
function isSetup(url) {
  return (
    url?.split("?")[0] ===
    pathToFileURL(path.join(root(), "desktop-setup.html")).href
  );
}
function trusted(event) {
  const url = event.senderFrame?.url;
  return (
    event.sender === window?.webContents &&
    (url?.startsWith(`${SCHEME}://app/`) || isSetup(url))
  );
}
function ipc(name, callback) {
  ipcMain.handle(name, async (event, ...args) => {
    if (!trusted(event)) throw Error("Unrecognized editor window");
    return callback(...args);
  });
}
function stopBackend() {
  backendURL = null;
  if (backend) {
    const process = backend;
    backend = null;
    process.stdin.end();
    setTimeout(() => {
      if (process.exitCode === null) process.kill();
    }, 1500).unref();
  }
}
function backendExecutable() {
  if (app.isPackaged)
    return {
      command: path.join(
        process.resourcesPath,
        "backend",
        "lighting-backend",
        process.platform === "win32"
          ? "lighting-backend.exe"
          : "lighting-backend",
      ),
      prefix: [],
    };
  const frozen = argument("backend");
  if (frozen) return { command: frozen, prefix: [] };
  return {
    command:
      argument("python") ||
      process.env.PE_LIGHTING_PYTHON ||
      (process.platform === "win32" ? "python" : "python3"),
    prefix: [path.join(root(), "serve.py")],
  };
}
async function startBackend() {
  stopBackend();
  validSources(settings);
  const executable = backendExecutable();
  const args = [
    ...executable.prefix,
    "--disc",
    settings.disc,
    "--model",
    settings.model,
    "--profiles",
    settings.profiles,
    "--vendor",
    path.join(root(), "node_modules", "three"),
    "--assets-root",
    root(),
    "--port",
    "0",
    "--desktop",
  ];
  const child = spawn(executable.command, args, {
    stdio: ["pipe", "pipe", "pipe"],
    windowsHide: true,
  });
  backend = child;
  let output = "",
    error = "";
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      child.kill();
      reject(
        Error(
          "The asset reader did not start. Check the selected disc and model capture.",
        ),
      );
    }, 30000);
    child.stdout.on("data", (data) => {
      output += data.toString();
      const match = output.match(
        /Lighting editor: (http:\/\/127\.0\.0\.1:\d+)/,
      );
      if (match) {
        clearTimeout(timeout);
        backendURL = match[1];
        resolve();
      }
    });
    child.stderr.on("data", (data) => {
      error = (error + data).slice(-12000);
    });
    child.on("error", (e) => {
      clearTimeout(timeout);
      reject(Error(`Could not start the bundled asset reader: ${e.message}`));
    });
    child.on("exit", (code) => {
      clearTimeout(timeout);
      if (backend === child) {
        backend = null;
        backendURL = null;
      }
      if (!output.includes("Lighting editor:"))
        reject(Error(error.trim() || `Asset reader exited (${code}).`));
      else if (!quitting && code) {
        dialog.showErrorBox(
          NAME,
          "The local asset reader stopped. Reopen the workspace to continue.",
        );
      }
    });
  });
}
async function openWorkspace(values) {
  validSources(values);
  const previous = { ...settings };
  const next = {
    ...settings,
    disc: values.disc,
    model: values.model,
    profiles: values.profiles || settings.profiles,
  };
  const directory = path.join(app.getPath("userData"), "capture");
  fs.mkdirSync(directory, { recursive: true });
  const digest = crypto
    .createHash("sha256")
    .update(fs.readFileSync(next.model))
    .update(fs.readFileSync(next.model + ".vram"))
    .digest("hex")
    .slice(0, 16);
  const copy = path.join(directory, `aya-${digest}.json`);
  if (path.resolve(next.model) !== copy) {
    fs.copyFileSync(next.model + ".vram", copy + ".vram.partial");
    fs.renameSync(copy + ".vram.partial", copy + ".vram");
    fs.copyFileSync(next.model, copy + ".partial");
    fs.renameSync(copy + ".partial", copy);
    next.model = copy;
  }
  settings = next;
  try {
    await startBackend();
    persist();
    await window.loadURL(`${SCHEME}://app/`);
  } catch (error) {
    settings = previous;
    throw error;
  }
}
async function showSetup(error = "") {
  stopBackend();
  await window.loadFile(path.join(root(), "desktop-setup.html"), {
    query: error ? { error } : undefined,
  });
  if (!argument("hidden")) window.show();
}
function installMenu() {
  const template = [];
  if (process.platform === "darwin") template.push({ role: "appMenu" });
  template.push(
    {
      label: "File",
      submenu: [
        {
          label: "Open Workspace…",
          accelerator: "CmdOrCtrl+O",
          click: () => showSetup(),
        },
        {
          label: "Save Room",
          accelerator: "CmdOrCtrl+S",
          click: () =>
            window.webContents.executeJavaScript(
              "document.getElementById('save')?.click()",
            ),
        },
        {
          label: "Choose Profile Folder…",
          click: async () => {
            const result = await dialog.showOpenDialog(window, {
              title: "Choose a folder for lighting profiles",
              defaultPath: settings.profiles,
              properties: ["openDirectory", "createDirectory"],
            });
            if (!result.canceled) {
              settings.profiles = result.filePaths[0];
              persist();
              try {
                await startBackend();
                await window.loadURL(`${SCHEME}://app/`);
              } catch (e) {
                await showSetup(e.message);
              }
            }
          },
        },
        { type: "separator" },
        { role: process.platform === "darwin" ? "close" : "quit" },
      ],
    },
    { role: "editMenu" },
    {
      label: "View",
      submenu: [
        { role: "reload" },
        { role: "togglefullscreen" },
        { role: "resetZoom" },
        { role: "zoomIn" },
        { role: "zoomOut" },
        ...(argument("devtools") ? [{ role: "toggleDevTools" }] : []),
      ],
    },
    { role: "windowMenu" },
  );
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}
function registerIPC() {
  ipc("lighting:settings", () => ({
    disc: settings.disc || "",
    model: settings.model || "",
    profiles: settings.profiles,
  }));
  ipc("lighting:choose-disc", async () => {
    const result = await dialog.showOpenDialog(window, {
      title: "Choose a USA Parasite Eve disc image",
      properties: ["openFile"],
      filters: [
        { name: "PlayStation disc image", extensions: ["cue", "bin", "iso"] },
      ],
    });
    return result.canceled ? null : result.filePaths[0];
  });
  ipc("lighting:choose-model", async () => {
    const result = await dialog.showOpenDialog(window, {
      title: "Choose an original Aya model capture",
      properties: ["openFile"],
      filters: [{ name: "Aya model capture", extensions: ["json"] }],
    });
    return result.canceled ? null : result.filePaths[0];
  });
  ipc("lighting:choose-profiles", async () => {
    const result = await dialog.showOpenDialog(window, {
      title: "Choose a folder for lighting profiles",
      defaultPath: settings.profiles,
      properties: ["openDirectory", "createDirectory"],
    });
    return result.canceled ? null : result.filePaths[0];
  });
  ipc("lighting:open", async (values) => {
    await openWorkspace(values);
    return true;
  });
}
if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on("second-instance", () => {
    if (window) {
      if (window.isMinimized()) window.restore();
      window.show();
      window.focus();
    }
  });
  app
    .whenReady()
    .then(async () => {
      loadSettings();
      registerIPC();
      const map =
        fs
          .readFileSync(path.join(root(), "index.html"), "utf8")
          .match(/<script type="importmap">([\s\S]*?)<\/script>/)?.[1] || "";
      const hash = crypto.createHash("sha256").update(map).digest("base64");
      protocol.handle(SCHEME, async (request) => {
        const url = new URL(request.url);
        if (url.hostname !== "app" || !backendURL)
          return new Response("Workspace is not running", { status: 503 });
        const headers = new Headers(request.headers);
        headers.delete("origin");
        headers.delete("host");
        const response = await net.fetch(
          backendURL + url.pathname + url.search,
          {
            method: request.method,
            headers,
            ...(!["GET", "HEAD"].includes(request.method)
              ? { body: await request.arrayBuffer() }
              : {}),
            bypassCustomProtocolHandlers: true,
          },
        );
        const out = new Headers(response.headers);
        out.set(
          "Content-Security-Policy",
          `default-src 'self'; script-src 'self' 'sha256-${hash}'; style-src 'self'; img-src 'self' data: blob:; connect-src 'self'; object-src 'none'; base-uri 'self'; frame-ancestors 'none'`,
        );
        return new Response(response.body, {
          status: response.status,
          headers: out,
        });
      });
      window = new BrowserWindow({
        title: NAME,
        width: settings.width || 1600,
        height: settings.height || 1000,
        minWidth: 1100,
        minHeight: 740,
        backgroundColor: "#0c1116",
        show: false,
        webPreferences: {
          preload: path.join(root(), "desktop-preload.cjs"),
          contextIsolation: true,
          sandbox: true,
          nodeIntegration: false,
          backgroundThrottling: false,
        },
      });
      window.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
      window.webContents.on("will-navigate", (event, url) => {
        if (!url.startsWith(`${SCHEME}://app/`) && !isSetup(url))
          event.preventDefault();
      });
      session.defaultSession.setPermissionRequestHandler(
        (_contents, _permission, callback) => callback(false),
      );
      session.defaultSession.on("will-download", (_event, item) =>
        item.setSaveDialogOptions({
          title: "Export lighting editor file",
          defaultPath: path.join(
            app.getPath("documents"),
            path.basename(item.getFilename()),
          ),
        }),
      );
      window.on("close", () => {
        [settings.width, settings.height] = window.getSize();
        persist();
      });
      window.once("ready-to-show", () => {
        if (!argument("hidden")) window.show();
      });
      installMenu();
      if (!settings.disc && !settings.model) await showSetup();
      else
        try {
          validSources(settings);
          await openWorkspace(settings);
        } catch (e) {
          await showSetup(e.message);
        }
    })
    .catch((error) => {
      dialog.showErrorBox(NAME, error.message);
      app.quit();
    });
  app.on("activate", () => {
    if (window) {
      window.show();
      window.focus();
    }
  });
  app.on("window-all-closed", () => app.quit());
  app.on("before-quit", () => {
    quitting = true;
    stopBackend();
  });
}
