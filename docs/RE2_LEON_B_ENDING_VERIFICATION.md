# Leon B ending verification on Windows

Tested the integrated RE2 build at `92ceb2fdb` with the raw Leon disc's MOV3 import and an isolated copy of the native post-Tyrant card and its companion.

- The card restored room 7-02 with the Tyrant defeated and the enemy defeat flags unchanged.
- Test positioning staged the route through 7-01 and 7-00 to the train controls in 7-03. At the controls, a captured frame matched frame 37 of the retail `R703L` movie. The next Cross press skipped the clip and the script continued.
- Leon's reunion is a prerequisite for the ending. In 7-03, procedure 6 sets common flag `0xA9`. Procedure 8 requires both `0xA9` and the post-train flag `0xBF` before enabling ending zone 3. The first run defeated the train boss but had skipped the reunion, which left the ending zone unavailable.
- The corrected route staged a 7-03 checkpoint with the reunion complete. Aya had weapon `0x4A`, 14 loaded rounds and 240 in reserve before enabling Infinite Ammo and PE in the isolated combat profile. The controller replay entered 7-04, defeated EM36, and returned to 7-03. Zone 3 started procedure 10, which set `SYS_ENDING`.
- The first confirmed gap after that trigger is the credits handoff. The runtime showed “The end. (The game's ending would play here.)” in 7-03. No credits sequence played. The debug save command then rejected the snapshot because Aya was outside the free field.

## Recorded inputs

The [input trace](RE2_LEON_B_ENDING_INPUTS.json) contains `[vblanks, raw pad mask]` pairs. The staging section also includes test positioning actions. Set the selected door to **0**, **0**, and **1** at the three consecutive “Go to that door” actions. The escape inputs begin from a native reload of the staged 7-03 checkpoint. The combat inputs follow EM36's battle entry. The ending inputs follow the battle result and return to the field. The trace omits the test harness's battle polling and save commands and requires the corresponding checkpoint state.

The Windows build and the focused room data, session and portability tests passed.
