import { sign } from "@electron/osx-sign";
import fs from "node:fs";
import { fileURLToPath } from "node:url";
const [app, identity] = process.argv.slice(2);
if (!app || !identity) throw Error("Usage: sign_desktop.mjs APP IDENTITY");
function ignore(file) {
  const stat = fs.lstatSync(file);
  if (stat.isSymbolicLink()) return true;
  if (!stat.isFile()) return false;
  const header = Buffer.alloc(4);
  const fd = fs.openSync(file, "r");
  try {
    fs.readSync(fd, header, 0, 4, 0);
  } finally {
    fs.closeSync(fd);
  }
  return ![
    0xfeedface, 0xfeedfacf, 0xcafebabe, 0xbebafeca, 0xcefaedfe, 0xcffaedfe,
  ].includes(header.readUInt32BE());
}
await sign({
  app,
  identity,
  ignore,
  type: "distribution",
  hardenedRuntime: true,
  optionsForFile: () => ({
    hardenedRuntime: true,
    entitlements: fileURLToPath(
      new URL("./desktop-entitlements.plist", import.meta.url),
    ),
  }),
});
console.log("Signed:", app);
