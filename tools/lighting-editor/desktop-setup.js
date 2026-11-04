const api = window.lightingDesktop,
  $ = (id) => document.getElementById(id);
(async () => {
  const settings = await api.settings();
  for (const key of ["disc", "model", "profiles"])
    $(key).value = settings[key] || "";
  $("setupError").textContent =
    new URLSearchParams(location.search).get("error") || "";
})();
for (const [id, field, method] of [
  ["chooseDisc", "disc", "chooseDisc"],
  ["chooseModel", "model", "chooseModel"],
  ["chooseProfiles", "profiles", "chooseProfiles"],
])
  $(id).onclick = async () => {
    try {
      const selected = await api[method]();
      if (selected) $(field).value = selected;
    } catch (e) {
      $("setupError").textContent = e.message;
    }
  };
$("openWorkspace").onclick = async () => {
  const button = $("openWorkspace");
  button.disabled = true;
  $("setupError").textContent = "";
  $("setupStatus").textContent = "Reading original assets…";
  try {
    await api.open({
      disc: $("disc").value,
      model: $("model").value,
      profiles: $("profiles").value,
    });
  } catch (e) {
    $("setupError").textContent = e.message;
    $("setupStatus").textContent = "";
    button.disabled = false;
  }
};
