@echo off
setlocal
set "BP_UI_PREVIEW_ROOT=%~dp0"
set "PE_CONFIG_HOME=%BP_UI_PREVIEW_ROOT%userdata"
set "PSYZ_HEADLESS="
set "PE_BP_UI_PAGE="
set "PE_BP_UI_F1_TEST="
if not exist "%BP_UI_PREVIEW_ROOT%..\..\build-bp-ui-headless\parasite-eve-ui-preview.exe" (
  echo The UI preview executable is missing.
  pause
  exit /b 1
)
if not exist "%BP_UI_PREVIEW_ROOT%..\..\..\Parasite Eve\Parasite Eve (USA) (Disc 1).cue" (
  echo Disc 1 was not found in the project's Parasite Eve folder.
  pause
  exit /b 1
)
start "" /D "%BP_UI_PREVIEW_ROOT%" "%BP_UI_PREVIEW_ROOT%..\..\build-bp-ui-headless\parasite-eve-ui-preview.exe" --disc "%BP_UI_PREVIEW_ROOT%..\..\..\Parasite Eve\Parasite Eve (USA) (Disc 1).cue" --disc2 "%BP_UI_PREVIEW_ROOT%..\..\..\Parasite Eve\Parasite Eve (USA) (Disc 2).cue"
endlocal
