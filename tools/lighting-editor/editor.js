import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { TransformControls } from "three/addons/controls/TransformControls.js";
const $ = (id) => document.getElementById(id),
  SCALE = 100,
  SIGN = [1, -1, -1];
const world = (p) =>
  new THREE.Vector3(p[0] / SCALE, -p[1] / SCALE, -p[2] / SCALE);
const game = (v) =>
  [v.x * SCALE, -v.y * SCALE, -v.z * SCALE].map(
    (n) => Math.round(n * 100) / 100,
  );
const scene = new THREE.Scene(),
  aya = new THREE.Group(),
  lightRoot = new THREE.Group(),
  overlay = new THREE.Group();
scene.add(aya, lightRoot, overlay);
const ambient = new THREE.AmbientLight(0xffffff, 0.25);
scene.add(ambient);
const gameCamera = new THREE.PerspectiveCamera(),
  orbitCamera = new THREE.PerspectiveCamera(45, 1, 0.01, 10000);
const makeRenderer = (canvas) => {
  const r = new THREE.WebGLRenderer({
    canvas,
    alpha: true,
    antialias: true,
    preserveDrawingBuffer: true,
  });
  r.setPixelRatio(Math.min(devicePixelRatio, 2));
  r.setClearColor(0x000000, 0);
  r.outputColorSpace = THREE.SRGBColorSpace;
  r.toneMapping = THREE.ACESFilmicToneMapping;
  r.shadowMap.enabled = false;
  return r;
};
const renderer = makeRenderer($("game")),
  orbitRenderer = makeRenderer($("orbit"));
const orbit = new OrbitControls(orbitCamera, $("orbit"));
orbit.enableDamping = true;
const transforms = [
  new TransformControls(gameCamera, $("game")),
  new TransformControls(orbitCamera, $("orbit")),
];
for (const t of transforms) {
  t.setSize(0.7);
  overlay.add(t.getHelper());
  t.addEventListener("dragging-changed", (e) => {
    orbit.enabled = !e.value;
  });
  t.addEventListener("objectChange", () => {
    if (t.object === aya) snapAya();
    else updateLightFromObject(t.object);
    changed();
    syncInspector();
  });
}
const raycaster = new THREE.Raycaster(),
  pointer = new THREE.Vector2(),
  materials = [],
  markers = [],
  textureReady = [];
let catalog = [],
  room = null,
  profile = null,
  model = null,
  floorPick = null,
  floorLines = null,
  selected = "aya",
  lights = new Map(),
  loading = false,
  loadToken = 0,
  dirty = false,
  ready = false;
let ayaBaseYaw = 0;
function error(e) {
  $("error").textContent = e instanceof Error ? e.message : String(e);
  console.error(e);
}
async function fetchJSON(url, options) {
  const response = await fetch(url, options);
  const data = await response.json();
  if (!response.ok) throw Error(data.error || response.statusText);
  return data;
}
function download(data, name, type) {
  const url = URL.createObjectURL(new Blob([data], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}
function materialFor(face) {
  let texture = null;
  if (face.page >= 0)
    textureReady.push(
      new Promise((resolve, reject) => {
        texture = new THREE.TextureLoader().load(
          `/api/texture/${face.page & 511}/${face.clut}.png`,
          () => resolve(),
          undefined,
          reject,
        );
      }),
    );
  if (texture) {
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.magFilter = THREE.NearestFilter;
    texture.minFilter = THREE.NearestFilter;
    texture.generateMipmaps = false;
  }
  const lit = new THREE.MeshStandardMaterial({
    map: texture,
    roughness: 0.8,
    metalness: 0,
    vertexColors: true,
    side: THREE.DoubleSide,
    alphaTest: 0.5,
  });
  const unlit = new THREE.MeshBasicMaterial({
    map: texture,
    vertexColors: true,
    side: THREE.DoubleSide,
    alphaTest: 0.5,
  });
  const normals = new THREE.MeshNormalMaterial({ side: THREE.DoubleSide });
  materials.push({ lit, unlit, normals });
  return materials.at(-1);
}
function buildAya(data) {
  const groups = new Map();
  for (const face of data.faces) {
    const key = `${face.page & 511}:${face.clut}:${face.page < 0}`;
    if (!groups.has(key))
      groups.set(key, { face, positions: [], normals: [], uv: [], colors: [] });
    const group = groups.get(key);
    const triangles =
      face.indices.length === 4
        ? [
            [0, 1, 2],
            [1, 3, 2],
          ]
        : [[0, 1, 2]];
    for (const tri of triangles)
      for (const j of tri) {
        const vertex = data.vertices[face.indices[j]];
        group.positions.push(
          vertex[0] / SCALE,
          -vertex[1] / SCALE,
          -vertex[2] / SCALE,
        );
        group.normals.push(vertex[3], -vertex[4], -vertex[5]);
        group.uv.push(
          (face.uv[j][0] + 0.5) / 256,
          1 - (face.uv[j][1] + 0.5) / 256,
        );
        const color = face.colors[j].map((v) => Math.min(1, v / 128));
        const linear = new THREE.Color().setRGB(...color, THREE.SRGBColorSpace);
        group.colors.push(linear.r, linear.g, linear.b);
      }
  }
  for (const group of groups.values()) {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute(
      "position",
      new THREE.Float32BufferAttribute(group.positions, 3),
    );
    geometry.setAttribute(
      "normal",
      new THREE.Float32BufferAttribute(group.normals, 3),
    );
    geometry.normalizeNormals();
    geometry.setAttribute("uv", new THREE.Float32BufferAttribute(group.uv, 2));
    geometry.setAttribute(
      "color",
      new THREE.Float32BufferAttribute(group.colors, 3),
    );
    geometry.computeBoundingSphere();
    const options = materialFor(group.face);
    const mesh = new THREE.Mesh(geometry, options.lit);
    mesh.userData.materials = options;
    aya.add(mesh);
  }
  ayaBaseYaw = 0;
  return Promise.all(textureReady);
}
function buildFloor(data) {
  if (floorLines) {
    overlay.remove(floorLines);
    floorLines.geometry.dispose();
    floorLines.material.dispose();
  }
  if (floorPick) floorPick.geometry.dispose();
  const coordinates = [];
  for (const triangle of data.floor)
    if (triangle.walkable)
      for (const p of triangle.points) coordinates.push(...world(p));
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(coordinates, 3),
  );
  geometry.computeBoundingBox();
  floorPick = new THREE.Mesh(
    geometry,
    new THREE.MeshBasicMaterial({ side: THREE.DoubleSide }),
  );
  floorPick.updateMatrixWorld(true);
  floorLines = new THREE.LineSegments(
    new THREE.WireframeGeometry(geometry),
    new THREE.LineBasicMaterial({
      color: 0x8fcbb1,
      transparent: true,
      opacity: 0.42,
      depthTest: false,
    }),
  );
  overlay.add(floorLines);
  $("meshBadge").textContent = coordinates.length
    ? `${coordinates.length / 9} trójkątów podłoża`
    : "Brak siatki w danych pokoju";
  $("center").disabled = !coordinates.length;
}
function floorAt(x, z, preferY = aya.position.y) {
  if (!floorPick) return null;
  raycaster.ray.origin.set(x, 10000, z);
  raycaster.ray.direction.set(0, -1, 0);
  const hits = raycaster.intersectObject(floorPick, false);
  if (!hits.length) return null;
  hits.sort(
    (a, b) => Math.abs(a.point.y - preferY) - Math.abs(b.point.y - preferY),
  );
  return hits[0].point;
}
function snapAya() {
  const hit = floorAt(aya.position.x, aya.position.z);
  if (hit) aya.position.y = hit.y;
  else if (profile) {
    aya.position.copy(world(profile.aya.position));
    $("hint").textContent = "Pozycja poza siatką — Aya pozostaje na podłożu.";
  }
}
function centerAya() {
  if (!floorPick?.geometry.attributes.position.count) return;
  const p = floorPick.geometry.attributes.position;
  // A centroid of an actual triangle is guaranteed to lie on the source mesh.
  let chosen = null,
    best = Infinity;
  for (let i = 0; i < p.count; i += 3) {
    const c = new THREE.Vector3();
    for (let j = 0; j < 3; ++j)
      c.add(new THREE.Vector3().fromBufferAttribute(p, i + j));
    c.multiplyScalar(1 / 3);
    const projected = c.clone().project(gameCamera);
    const distance = projected.x * projected.x + projected.y * projected.y;
    if (projected.z < 1 && distance < best) {
      chosen = c;
      best = distance;
    }
  }
  if (chosen) aya.position.copy(chosen);
  else aya.position.fromBufferAttribute(p, 0);
  if (profile && !loading) {
    profile.aya.position = game(aya.position);
    changed();
  }
  syncInspector();
  resetOrbit();
}
function configureCamera(data) {
  const r = data.rotation.map((n) => n / 4096),
    t = data.translation;
  const view = new THREE.Matrix4().set(
    r[0],
    -r[1],
    -r[2],
    t[0] / SCALE,
    -r[3],
    r[4],
    r[5],
    -t[1] / SCALE,
    -r[6],
    r[7],
    r[8],
    -t[2] / SCALE,
    0,
    0,
    0,
    1,
  );
  gameCamera.fov = THREE.MathUtils.radToDeg(
    2 * Math.atan(data.height / (2 * data.projection)),
  );
  gameCamera.aspect = data.width / data.height;
  gameCamera.near = 0.01;
  gameCamera.far = 10000;
  gameCamera.updateProjectionMatrix();
  gameCamera.matrixAutoUpdate = false;
  gameCamera.matrixWorldAutoUpdate = false;
  gameCamera.matrixWorld.copy(view).invert();
  gameCamera.matrix.copy(gameCamera.matrixWorld);
  gameCamera.matrixWorldInverse.copy(view);
}
function resetOrbit() {
  const target = aya.position.clone().add(new THREE.Vector3(0, 1.5, 0));
  orbit.target.copy(target);
  orbitCamera.position.copy(target).add(new THREE.Vector3(8, 7, 10));
  orbit.update();
}
function attachSelected() {
  for (const t of transforms) {
    const object = selected === "aya" ? aya : lights.get(selected)?.holder;
    t.detach();
    if (object && $("helpers").checked) t.attach(object);
    t.showY = selected !== "aya";
  }
}
function select(id) {
  selected = id;
  attachSelected();
  syncInspector();
  renderLightList();
}
function lightObject(data) {
  const holder = new THREE.Group();
  holder.position.copy(world(data.position));
  holder.userData.lightId = data.id;
  let light;
  if (data.type === "directional")
    light = new THREE.DirectionalLight(data.color, data.intensity);
  else if (data.type === "spot") {
    light = new THREE.SpotLight(
      data.color,
      data.intensity,
      data.range / SCALE,
      THREE.MathUtils.degToRad(data.angle),
      0.35,
      2,
    );
  } else
    light = new THREE.PointLight(
      data.color,
      data.intensity,
      data.range / SCALE,
      2,
    );
  holder.add(light);
  lightRoot.add(holder);
  const marker = new THREE.Mesh(
    new THREE.SphereGeometry(0.12, 12, 8),
    new THREE.MeshBasicMaterial({ color: data.color, depthTest: false }),
  );
  marker.userData.lightId = data.id;
  holder.add(marker);
  markers.push(marker);
  let helper = null;
  if (data.type === "spot") helper = new THREE.SpotLightHelper(light);
  else if (data.type === "directional")
    helper = new THREE.DirectionalLightHelper(light, 0.4);
  else helper = new THREE.PointLightHelper(light, 0.2);
  overlay.add(helper);
  if (light.target) {
    light.target.position.copy(world(data.target));
    scene.add(light.target);
  }
  return { data, holder, light, marker, helper };
}
function applyLights() {
  for (const item of lights.values()) {
    lightRoot.remove(item.holder);
    overlay.remove(item.helper);
    item.helper.dispose();
    if (item.light.target) scene.remove(item.light.target);
    item.marker.geometry.dispose();
    item.marker.material.dispose();
    item.light.dispose();
  }
  lights.clear();
  markers.length = 0;
  for (const data of profile.lights) lights.set(data.id, lightObject(data));
  ambient.color.set(profile.ambient.color);
  ambient.intensity = profile.ambient.intensity;
  for (const material of materials) material.lit.roughness = profile.roughness;
  renderer.toneMappingExposure = orbitRenderer.toneMappingExposure =
    profile.exposure;
  renderLightList();
  attachSelected();
}
function updateLightFromObject(object) {
  const item = lights.get(object?.userData.lightId);
  if (item) item.data.position = game(object.position);
}
function changed() {
  if (!profile || loading) return;
  profile.aya.position = game(aya.position);
  profile.aya.yaw = +$("yaw").value;
  profile.ambient = {
    color: $("ambientColor").value,
    intensity: +$("ambientIntensity").value,
  };
  profile.exposure = +$("exposure").value;
  profile.roughness = +$("roughness").value;
  dirty = true;
  $("saveState").textContent = "Niezapisane zmiany";
  localStorage.setItem(`pe-lighting-${profile.map}`, JSON.stringify(profile));
}
function renderLightList() {
  if (!profile) return;
  $("lightCount").textContent = `${profile.lights.length} / 16`;
  $("lights").replaceChildren();
  for (const data of profile.lights) {
    const b = document.createElement("button");
    b.classList.toggle("active", data.id === selected);
    const s = document.createElement("span");
    s.className = "swatch";
    s.style.background = data.color;
    const label = document.createElement("span");
    label.textContent = `${data.type === "point" ? "Punktowe" : data.type === "spot" ? "Reflektor" : "Kierunkowe"} · ${data.id}`;
    b.append(s, label);
    b.onclick = () => select(data.id);
    $("lights").append(b);
  }
}
function syncInspector() {
  if (!profile) return;
  const p = game(aya.position);
  ["ayaX", "ayaY", "ayaZ"].forEach((id, i) => ($(id).value = p[i]));
  $("coordinates").textContent = `Aya: ${p.map(Math.round).join(" / ")}`;
  const item = lights.get(selected);
  $("lightEditor").hidden = !item;
  if (!item) return;
  const d = item.data;
  $("lightIntensity").max = d.type === "directional" ? 4 : 150;
  $("lightIntensity").step = d.type === "directional" ? 0.01 : 0.1;
  $("targetFields").hidden = d.type === "point";
  $("aim").hidden = d.type === "point";
  ["targetX", "targetY", "targetZ"].forEach(
    (id, i) => ($(id).value = d.target[i]),
  );
  $("selectedTitle").textContent = d.id;
  $("lightColor").value = d.color;
  $("lightIntensity").value = d.intensity;
  $("lightRange").value = d.range;
  $("lightAngle").value = d.angle;
  ["lightX", "lightY", "lightZ"].forEach(
    (id, i) => ($(id).value = d.position[i]),
  );
  $("angleRow").hidden = d.type !== "spot";
}
function defaultProfile(map) {
  const p = game(aya.position);
  return {
    format: 1,
    map,
    aya: { position: p, yaw: 0 },
    ambient: { color: "#ffffff", intensity: 0.25 },
    exposure: 1,
    roughness: 0.8,
    lights: [
      {
        id: "key",
        type: "point",
        position: [p[0] - 250, p[1] - 350, p[2] + 200],
        target: [p[0], p[1] - 170, p[2]],
        color: "#fff1dd",
        intensity: 8,
        range: 1600,
        angle: 35,
      },
    ],
  };
}
function applyProfile(data) {
  profile = data;
  aya.position.copy(world(data.aya.position));
  $("yaw").value = data.aya.yaw;
  aya.rotation.y = -THREE.MathUtils.degToRad(data.aya.yaw - ayaBaseYaw);
  $("yawLabel").textContent = `${data.aya.yaw}°`;
  $("ambientColor").value = data.ambient.color;
  $("ambientIntensity").value = data.ambient.intensity;
  $("exposure").value = data.exposure;
  $("roughness").value = data.roughness;
  applyLights();
  syncInspector();
}
async function loadRoom(map, group = 0) {
  const token = ++loadToken;
  loading = true;
  $("loading").hidden = false;
  $("error").textContent = "";
  for (const t of transforms) t.detach();
  try {
    const [data, saved] = await Promise.all([
      fetchJSON(`/api/room/${map}/${group}`),
      fetchJSON(`/api/profile/${map}`),
    ]);
    if (token !== loadToken) return;
    room = data;
    configureCamera(data);
    buildFloor(data);
    aya.position.copy(world(model.origin));
    const originalFloor = floorAt(aya.position.x, aya.position.z);
    if (originalFloor) aya.position.copy(originalFloor);
    else centerAya();
    profile = defaultProfile(map);
    let draft;
    try {
      draft = JSON.parse(localStorage.getItem(`pe-lighting-${map}`));
    } catch {}
    applyProfile(draft || saved.profile || profile);
    snapAya();
    syncInspector();
    selected = "aya";
    attachSelected();
    resetOrbit();
    $("roomTitle").textContent =
      `${catalog.find((r) => r.map === map)?.id || map} · ujęcie ${group}`;
    $("roomInfo").textContent =
      `${data.width} × ${data.height} · ${data.floor.length} trójkątów · ${data.background_animated ? "tło: zamrożona klatka animacji" : "oryginalne tło"}`;
    $("background").src = `/api/room/${map}/${group}.png`;
    dirty = !!draft;
    $("saveState").textContent = dirty
      ? "Przywrócono szkic"
      : saved.profile
        ? "Wczytano zapis"
        : "Nowy profil";
    $("view").replaceChildren();
    for (const v of catalog.find((r) => r.map === map).views) {
      const option = new Option(
        `Ujęcie ${v.group} · ${v.width}×${v.height}`,
        v.group,
      );
      $("view").add(option);
    }
    $("view").value = group;
    resize();
    await $("background").decode();
    if (token === loadToken) $("loading").hidden = true;
  } catch (e) {
    error(e);
    $("loading").textContent = "Nie udało się wczytać pokoju.";
  } finally {
    if (token === loadToken) loading = false;
  }
}
function resize() {
  if (!room) return;
  const area = $("gameArea"),
    ratio = room.width / room.height,
    w = area.clientWidth - 48,
    h = area.clientHeight - 37;
  let sw = Math.min(w, h * ratio),
    sh = sw / ratio;
  sw = Math.max(1, sw);
  sh = Math.max(1, sh);
  $("stage").style.width = sw + "px";
  $("stage").style.height = sh + "px";
  renderer.setSize(sw, sh, false);
  const c = $("orbit");
  orbitRenderer.setSize(c.clientWidth, c.clientHeight, false);
  orbitCamera.aspect = c.clientWidth / c.clientHeight;
  orbitCamera.updateProjectionMatrix();
}
function pointerRay(event, camera, canvas) {
  const bounds = canvas.getBoundingClientRect();
  pointer.set(
    ((event.clientX - bounds.left) / bounds.width) * 2 - 1,
    (-(event.clientY - bounds.top) / bounds.height) * 2 + 1,
  );
  raycaster.setFromCamera(pointer, camera);
}
$("game").addEventListener("pointerdown", (event) => {
  if (loading || event.button || transforms[0].axis) return;
  pointerRay(event, gameCamera, $("game"));
  const hits = raycaster.intersectObjects(markers, false);
  if (hits.length) {
    select(hits[0].object.userData.lightId);
    return;
  }
  if ($("place").checked && floorPick) {
    const hit = raycaster.intersectObject(floorPick, false)[0];
    if (hit) {
      aya.position.copy(hit.point);
      select("aya");
      changed();
      syncInspector();
    }
  }
});
$("room").onchange = () => loadRoom(+$("room").value);
$("view").onchange = () => loadRoom(+$("room").value, +$("view").value);
$("search").oninput = () => {
  const text = $("search").value.toLowerCase();
  for (const option of $("room").options)
    option.hidden = !option.text.toLowerCase().includes(text);
};
$("selectAya").onclick = () => select("aya");
$("center").onclick = centerAya;
$("resetOrbit").onclick = resetOrbit;
$("yaw").oninput = () => {
  aya.rotation.y = -THREE.MathUtils.degToRad(+$("yaw").value - ayaBaseYaw);
  $("yawLabel").textContent = $("yaw").value + "°";
  changed();
};
for (const id of ["ayaX", "ayaZ"])
  $(id).onchange = () => {
    aya.position.copy(
      world([+$("ayaX").value, +$("ayaY").value, +$("ayaZ").value]),
    );
    snapAya();
    changed();
    syncInspector();
  };
for (const id of ["ambientColor", "ambientIntensity", "exposure", "roughness"])
  $(id).oninput = () => {
    changed();
    applyLights();
  };
for (const [id, type] of [
  ["addPoint", "point"],
  ["addSpot", "spot"],
  ["addDirectional", "directional"],
])
  $(id).onclick = () => {
    if (!profile || profile.lights.length >= 16) return;
    const p = game(aya.position),
      key = crypto.randomUUID().slice(0, 8);
    profile.lights.push({
      id: key,
      type,
      position: [p[0] + 200, p[1] - 300, p[2] + 100],
      target: [p[0], p[1] - 170, p[2]],
      color: "#d4e9ff",
      intensity: type === "directional" ? 1 : 40,
      range: 1600,
      angle: 35,
    });
    selected = key;
    applyLights();
    changed();
    syncInspector();
  };
for (const id of [
  "lightColor",
  "lightIntensity",
  "lightX",
  "lightY",
  "lightZ",
  "lightRange",
  "lightAngle",
  "targetX",
  "targetY",
  "targetZ",
])
  $(id).oninput = () => {
    const item = lights.get(selected);
    if (!item) return;
    Object.assign(item.data, {
      color: $("lightColor").value,
      intensity: +$("lightIntensity").value,
      position: [+$("lightX").value, +$("lightY").value, +$("lightZ").value],
      range: +$("lightRange").value,
      angle: +$("lightAngle").value,
      target: [+$("targetX").value, +$("targetY").value, +$("targetZ").value],
    });
    applyLights();
    changed();
  };
$("remove").onclick = () => {
  profile.lights = profile.lights.filter((l) => l.id !== selected);
  selected = "aya";
  applyLights();
  changed();
  syncInspector();
};
$("aim").onclick = () => {
  const item = lights.get(selected);
  if (!item) return;
  const p = game(aya.position);
  item.data.target = [p[0], p[1] - 170, p[2]];
  applyLights();
  changed();
};
$("helpers").onchange = attachSelected;
$("shading").onchange = () => {
  for (const mesh of aya.children)
    mesh.material = mesh.userData.materials[$("shading").value];
};
$("save").onclick = async () => {
  try {
    const result = await fetchJSON(`/api/profile/${room.map}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(profile),
    });
    dirty = false;
    localStorage.removeItem(`pe-lighting-${room.map}`);
    $("saveState").textContent = `Zapisano ${result.saved}`;
  } catch (e) {
    error(e);
  }
};
$("export").onclick = () =>
  download(
    JSON.stringify(profile, null, 2),
    `M${String(room.map).padStart(3, "0")}-lighting.json`,
    "application/json",
  );
$("import").onchange = async (event) => {
  try {
    const data = JSON.parse(await event.target.files[0].text());
    if (
      data.format !== 1 ||
      !Array.isArray(data.lights) ||
      data.lights.length > 16 ||
      data.map !== room.map
    )
      throw Error("Wybierz pokój zgodny z importowanym profilem.");
    await fetchJSON(`/api/profile/${room.map}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(data),
    });
    applyProfile(data);
    changed();
  } catch (e) {
    error(e);
  }
};
function draw() {
  if (!room) return;
  orbit.update();
  aya.updateMatrixWorld(true);
  floorLines.visible = $("wire").checked;
  for (const item of lights.values()) {
    item.marker.visible = $("helpers").checked;
    item.helper.visible = $("helpers").checked;
    item.helper.update();
  }
  for (const t of transforms) t.getHelper().visible = $("helpers").checked;
  transforms[1].getHelper().visible = false;
  renderer.render(scene, gameCamera);
  floorLines.visible = true;
  transforms[0].getHelper().visible = false;
  transforms[1].getHelper().visible = $("helpers").checked;
  orbitRenderer.render(scene, orbitCamera);
  transforms[0].getHelper().visible = $("helpers").checked;
}
$("screenshot").onclick = () => {
  const previous = $("helpers").checked,
    wire = $("wire").checked;
  $("helpers").checked = false;
  $("wire").checked = false;
  draw();
  const canvas = document.createElement("canvas");
  canvas.width = $("game").width;
  canvas.height = $("game").height;
  const context = canvas.getContext("2d");
  context.fillStyle = "#000";
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.drawImage($("background"), 0, 0, canvas.width, canvas.height);
  context.drawImage($("game"), 0, 0);
  const data = canvas.toDataURL("image/png");
  const a = document.createElement("a");
  a.href = data;
  a.download = `M${room.map}-aya-lighting.png`;
  a.click();
  $("helpers").checked = previous;
  $("wire").checked = wire;
};
new ResizeObserver(resize).observe($("gameArea"));
window.addEventListener("resize", resize);
window.addEventListener("beforeunload", () => {
  if (profile && dirty)
    localStorage.setItem(`pe-lighting-${profile.map}`, JSON.stringify(profile));
});
function animate() {
  requestAnimationFrame(animate);
  draw();
}
animate();
try {
  [catalog, model] = await Promise.all([
    fetchJSON("/api/rooms"),
    fetchJSON("/api/model"),
  ]);
  await buildAya(model);
  for (const data of catalog)
    $("room").add(
      new Option(`${data.id} · ${data.views.length} ujęć`, data.map),
    );
  $("room").value = 20;
  await loadRoom(20);
  ready = true;
  document.body.dataset.ready = "true";
} catch (e) {
  error(e);
}
// Read-only diagnostics for autonomous rendering and geometry tests.
window.lightingDiagnostics = () => ({
  ready,
  room: room?.map,
  group: room?.group,
  aya: game(aya.position),
  lights: profile?.lights,
  vertices: model?.vertices.length,
  faces: model?.faces.length,
  floor: room?.floor.length,
  dirty,
  gameCamera: gameCamera.matrixWorldInverse.elements,
  gizmos: transforms.map((t, i) => {
    const camera = i ? orbitCamera : gameCamera,
      canvas = i
        ? document.getElementById("orbit")
        : document.getElementById("game"),
      rect = canvas.getBoundingClientRect(),
      p =
        t.object?.getWorldPosition(new THREE.Vector3()) || new THREE.Vector3(),
      q = p
        .clone()
        .add(new THREE.Vector3(1, 0, 0))
        .project(camera),
      n = p.clone().project(camera);
    return {
      axis: t.axis,
      x: rect.x + ((n.x + 1) * rect.width) / 2,
      y: rect.y + ((1 - n.y) * rect.height) / 2,
      dx: ((q.x - n.x) * rect.width) / 2,
      dy: (-(q.y - n.y) * rect.height) / 2,
    };
  }),
});
