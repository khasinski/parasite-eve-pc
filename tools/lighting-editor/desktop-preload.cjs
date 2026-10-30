const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("lightingDesktop", {
  settings: () => ipcRenderer.invoke("lighting:settings"),
  chooseDisc: () => ipcRenderer.invoke("lighting:choose-disc"),
  chooseModel: () => ipcRenderer.invoke("lighting:choose-model"),
  chooseProfiles: () => ipcRenderer.invoke("lighting:choose-profiles"),
  open: (values) => ipcRenderer.invoke("lighting:open", values),
});
