/* Run with NODE_PATH=build/lighting-editor/deps/node_modules node ...
 * A live local workbench is required. All windows remain headless. */
const { chromium } = require("playwright");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
(async () => {
  const output =
    process.env.PE_LIGHTING_EVIDENCE || "build/lighting-editor/evidence";
  fs.mkdirSync(output, { recursive: true });
  const options = { headless: true };
  if (process.env.PE_TEST_CHROMIUM)
    options.executablePath = process.env.PE_TEST_CHROMIUM;
  const browser = await chromium.launch(options);
  const page = await browser.newPage({
    viewport: { width: 1600, height: 1000 },
    acceptDownloads: true,
  });
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  try {
    await page.goto(process.env.PE_LIGHTING_URL || "http://127.0.0.1:8767");
    await page.waitForFunction(() => document.body.dataset.ready === "true");
    const before = await page.evaluate(() => lightingDiagnostics());
    assert.equal(before.room, 20);
    assert.equal(before.vertices, 442);
    assert(before.floor > 0);
    await page.locator("#wire").uncheck();
    await page.locator("#helpers").uncheck();
    await page
      .locator("#stage")
      .screenshot({ path: path.join(output, "before.png") });
    await page.locator("#lights button").first().click();
    await page.locator("#lightColor").fill("#66b8ff");
    await page.locator("#lightIntensity").fill("90");
    await page.locator("#lightX").fill("250");
    await page.locator("#lightY").fill("-150");
    await page.locator("#lightZ").fill("450");
    await page.waitForTimeout(150);
    await page
      .locator("#stage")
      .screenshot({ path: path.join(output, "after.png") });
    const lit = await page.evaluate(() => lightingDiagnostics());
    assert.equal(lit.lights[0].color, "#66b8ff");
    assert.equal(lit.lights[0].intensity, 90);
    assert.deepEqual(lit.lights[0].position, [250, -150, 450]);
    await page.locator("#helpers").check();
    const gizmo = (await page.evaluate(() => lightingDiagnostics())).gizmos[0];
    let handle = null;
    for (const scale of [0.4, 0.7, 1, 1.4, 1.8, 2.2, 2.6]) {
      const x = gizmo.x + gizmo.dx * scale,
        y = gizmo.y + gizmo.dy * scale;
      await page.mouse.move(x, y);
      if (
        (await page.evaluate(() => lightingDiagnostics())).gizmos[0].axis ===
        "X"
      ) {
        handle = { x, y };
        break;
      }
    }
    assert(handle, "The actual 3D translation gizmo must be pickable");
    await page.mouse.down();
    await page.mouse.move(handle.x + 35, handle.y + 15, { steps: 8 });
    await page.mouse.up();
    assert.notEqual(
      (await page.evaluate(() => lightingDiagnostics())).lights[0].position[0],
      250,
      "Dragging the X handle must move the light",
    );
    await page.locator("#lightX").fill("250");
    await page.locator("#helpers").uncheck();
    await page.locator("#addSpot").click();
    await page.locator("#addDirectional").click();
    assert.equal(
      (await page.evaluate(() => lightingDiagnostics())).lights.length,
      3,
    );
    await page.locator("#remove").click();
    assert.equal(
      (await page.evaluate(() => lightingDiagnostics())).lights.length,
      2,
    );
    // Deliberately invalid placement must not leave Aya floating outside the mesh.
    await page.locator("#ayaX").fill("999999");
    await page.locator("#ayaX").press("Tab");
    assert.deepEqual(
      (await page.evaluate(() => lightingDiagnostics())).aya,
      lit.aya,
    );
    await page.locator("#yaw").fill("70");
    assert.equal(await page.locator("#yawLabel").textContent(), "70°");
    // Project an actual floor centroid using the disc's camera, then place via mouse.
    const candidate = await page.evaluate(async () => {
      const data = await (await fetch("/api/room/20/0")).json(),
        rect = document.querySelector("#game").getBoundingClientRect();
      for (const triangle of data.floor) {
        if (!triangle.walkable) continue;
        const p = [0, 1, 2].map(
            (i) => triangle.points.reduce((sum, v) => sum + v[i], 0) / 3,
          ),
          v = [0, 1, 2].map(
            (i) =>
              data.translation[i] +
              data.rotation
                .slice(i * 3, i * 3 + 3)
                .reduce((sum, n, j) => sum + (n / 4096) * p[j], 0),
          );
        if (v[2] <= 0) continue;
        const x = data.width / 2 + (data.projection * v[0]) / v[2],
          y = data.height / 2 + (data.projection * v[1]) / v[2];
        if (
          x > 20 &&
          x < data.width - 20 &&
          y > data.height * 0.65 &&
          y < data.height * 0.9
        )
          return {
            x: rect.x + (x / data.width) * rect.width,
            y: rect.y + (y / data.height) * rect.height,
            p,
          };
      }
      throw Error("No visible test floor triangle");
    });
    await page.mouse.click(candidate.x, candidate.y);
    const placed = await page.evaluate(() => lightingDiagnostics());
    assert(
      Math.abs(placed.aya[1] - candidate.p[1]) < 0.2,
      "Aya height must come from the floor",
    );
    assert(
      Math.hypot(placed.aya[0] - lit.aya[0], placed.aya[2] - lit.aya[2]) > 10,
      "Mouse must move Aya",
    );
    await page.locator("#save").click();
    await page.waitForFunction(() =>
      document.querySelector("#saveState").textContent.startsWith("Saved"),
    );
    await page.reload();
    await page.waitForFunction(() => document.body.dataset.ready === "true");
    assert.deepEqual(
      (await page.evaluate(() => lightingDiagnostics())).aya,
      placed.aya,
    );
    await page.locator("#room").selectOption("89");
    await page.waitForFunction(() => lightingDiagnostics().room === 89);
    assert((await page.evaluate(() => lightingDiagnostics())).floor > 0);
    await page.locator("#room").selectOption("163");
    await page.waitForFunction(() => lightingDiagnostics().room === 163);
    assert((await page.evaluate(() => lightingDiagnostics())).floor > 0);
    await page.locator("#room").selectOption("20");
    await page.waitForFunction(() => lightingDiagnostics().room === 20);
    assert.deepEqual(
      (await page.evaluate(() => lightingDiagnostics())).aya,
      placed.aya,
    );
    const download = page.waitForEvent("download");
    await page.locator("#screenshot").click();
    await (await download).saveAs(path.join(output, "composite.png"));
    await page.locator("#helpers").check();
    await page.locator("#wire").check();
    await page.screenshot({ path: path.join(output, "editor.png") });
    assert.deepEqual(errors, []);
    console.log(
      "PASS: original mesh/camera, live lighting, add/remove lights, floor picking/snapping, room switching, profile save/reload and composite screenshot; no browser errors",
    );
  } finally {
    await browser.close();
  }
})().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
