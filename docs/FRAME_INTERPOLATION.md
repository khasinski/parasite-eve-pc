# Optional 60 Hz presentation

Enable **Escape → Graphics → Gameplay display → 60 Hz (interpolated)**.
The default is **30 Hz (original)**. The choice is saved as the boolean
`graphics.frame_interpolation` in the existing TOML preferences. For testing,
use `--frame-interpolation 0|1`.

The original simulation, scripts, input sampling and audio retain their
29.97 Hz cadence. The SDL GPU renderer presents an additional midpoint at
59.94 Hz. This is smoother presentation, not a 60 Hz simulation or a change
to game speed.

Original model and background packets are paired across the game's two draw
buffers. A separate render target uses midpoint vertex positions and camera
placement; the original VRAM and texture feedback remain untouched. UI stays
sharp. Discrete artwork animation, lighting changes and unregistered effects
retain their authored cadence. New primitives and large movements fall back
to the current pose. Scene/resource changes clear motion history.

The option works with internal resolution, antialiasing, geometry correction,
HD backgrounds and widescreen. It requires additional GPU memory and rendering
work. The legacy OpenGL backend does not support it.

## Verification

`original-gpu-frame-interpolation` checks exact midpoint positions, unchanged
UI/native VRAM, 8x resolution, antialiasing, animated background camera motion
and history reset. `host-settings` checks persistence and option validation.

For the real-game comparison, set `PE_TEST_DISC` to a supported CUE and run:

```
python3 tests/frame_interpolation.py build/parasite-eve-pc
```

Optionally set `PE_TEST_HD_BACKGROUNDS` to the installed pack to include the
HD widescreen/antialiasing case. The headless test compares nine consecutive
VBlanks with the option off/on. Original frames, game state, native VRAM and
PCM must match exactly; additional midpoint frames must differ. Evidence is
written to a temporary directory reported by the test.
