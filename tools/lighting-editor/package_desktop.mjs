import { packager } from "@electron/packager";
import path from "node:path";
import fs from "node:fs";
const [stage, out, resources, icon] = process.argv.slice(2);
const options = {
  dir: stage,
  out,
  name: "Parasite Eve Lighting Lab",
  platform: process.platform,
  arch: process.arch,
  electronVersion: "45.1.0",
  appBundleId: "ovh.polanka.parasite-eve.lighting-lab",
  appCategoryType: "public.app-category.developer-tools",
  asar: false,
  prune: false,
  overwrite: true,
  extraResource: [resources],
};
if (process.platform === "darwin") options.icon = icon;
const results = await packager(options);
fs.writeFileSync(
  path.join(out, "packages.json"),
  JSON.stringify(results, null, 2) + "\n",
);
for (const result of results) console.log(result);
