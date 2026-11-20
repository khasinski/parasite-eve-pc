# Third-Party Source Notices

## psy-q-decomp

`src/main/psyq/libspu/SPU_StartDmaRead.c` is adapted from
`src/spu/s_crwa.c` in [sozud/psy-q-decomp](https://github.com/sozud/psy-q-decomp),
revision `6edf9b24721ba02a53eb423b8a7497b82ba0768c`.
Local changes adapt symbols and types to the Parasite Eve codebase. This notice applies to
the adapted source, not to proprietary SDK archives or the game binary.

MIT License

Copyright (c) 2024 the psy-q-decomp contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the " Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next paragraph) shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## PsyZ and SDL

The application links our source fork of [PsyZ](https://github.com/khasinski/parasite-eve-psyz)
and its pinned SDL submodule. PsyZ platform/runtime source is under MPL 2.0;
its `decomp/` directory retains its MIT notice. PsyZ's top-level LICENSE also
identifies unlicensed headers and PSYQ source. These notices remain separate
from the original game's copyright and do not relicense it.

The matching PsyZ sources are available through this repository's pinned
`third_party/psyz` submodule. SDL uses the zlib license, with additional notices
for its bundled components. Desktop packages retain the corresponding license
texts; consult each source file for any additional attribution.

## IBM Plex Sans

`src/main/boot/fonts/ibm_plex_sans.ttf.inc` embeds a subset of IBM Plex Sans Regular,
Copyright (c) 2017 IBM Corp., licensed under the SIL Open Font License, Version 1.1.
The full license text is included at `src/main/boot/fonts/OFL-IBMPlex.txt`.

## Pixeloid Sans (Pixel Sans)

`mods/fonts/Pixel_Sans.png`, the F1 > Font choice "Pixel Sans", is Pixeloid Sans Bold
version 1.0 by GGBotNet ([ggbot.itch.io/pixeloid-font](https://ggbot.itch.io/pixeloid-font)),
drawn at its 9-pixel size into the game's font layout with a one-pixel drop shadow added.
Copyright (c) 2020-2025 GGBotNet (https://ggbot.net/fonts/), with Reserved Font Name
"Pixeloid". Licensed under the SIL Open Font License, Version 1.1; the full license text is
included at `mods/fonts/OFL.txt` and ships beside the picture. As a modified version it does
not use the Reserved Font Name.

## stb_truetype.h

`src/main/boot/stb_truetype.h` is from Sean Barrett's `stb` single-file public domain /
MIT dual-licensed libraries (`https://github.com/nothings/stb`).


## cJSON 1.7.19 — MIT

Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.




## miniz 3.1.1 — MIT

Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.



## libwebp 1.6.0 — BSD

Copyright (c) 2010, Google Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

  * Neither the name of Google nor the names of its contributors may
    be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.




## libwebp patent grant

Additional IP Rights Grant (Patents)
------------------------------------

"These implementations" means the copyrightable works that implement the WebM
codecs distributed by Google as part of the WebM Project.

Google hereby grants to you a perpetual, worldwide, non-exclusive, no-charge,
royalty-free, irrevocable (except as stated in this section) patent license to
make, have made, use, offer to sell, sell, import, transfer, and otherwise
run, modify and propagate the contents of these implementations of WebM, where
such license applies only to those patent claims, both currently owned by
Google and acquired in the future, licensable by Google that are necessarily
infringed by these implementations of WebM. This grant does not include claims
that would be infringed only as a consequence of further modification of these
implementations. If you or your agent or exclusive licensee institute or order
or agree to the institution of patent litigation or any other patent
enforcement activity against any entity (including a cross-claim or
counterclaim in a lawsuit) alleging that any of these implementations of WebM
or any code incorporated within any of these implementations of WebM
constitute direct or contributory patent infringement, or inducement of
patent infringement, then any patent rights granted to you under this License
for these implementations of WebM shall terminate as of the date such
litigation is filed.



## libcurl

Mod downloads use the operating system libcurl on macOS/Linux, and a pinned static libcurl on Windows.

COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2025, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization of the copyright holder.

## libchdr

CHD disc images are read with [libchdr](https://github.com/rtissera/libchdr) (BSD-3-Clause,
copyright Romain Tisserand), vendored unmodified in `third_party/libchdr`. It builds
with its bundled LZMA SDK (public domain), Zstandard (BSD-3-Clause), miniz (MIT) and
dr_flac (public domain / MIT-0).

### libchdr license

```
Copyright Romain Tisserand
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Zstandard 1.5.7 license (bundled with libchdr)

```
BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name Facebook, nor Meta, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## FreeType

The translation editor (`localization/editor`) compiles in FreeType 2.13.2 through the `freetype-sys` crate to rasterize translators' CJK fonts, under the FreeType License (FTL).
Portions of this software are copyright (c) 2024 The FreeType Project (www.freetype.org).
All rights reserved.

## zlib

`localization/editor/vendor/zlib` holds the zlib 1.3.2 headers, and the translation editor links zlib through the`libz-sys` crate, under the zlib License. Copyright (C) 1995-2026 Jean-loup Gailly and Mark Adler. The license is in that folder.

## Resident Evil formats and engine references (import and play)

The importer (`mods/host/import`, `mods/pe_src_re1`, `mods/pe_src_re2`, `mods/pe_src_re3`) and the room
runtime (`mods/pe_re_play`) read the player's own Resident Evil discs. No game data is included. These
projects were used:

**Adapted (tables and layouts ported, with these notices):**

- [biohazard-utils](https://github.com/biorand/biohazard-utils) (MIT, Copyright (c) 2022-2023 Ted John):
  script instruction sizes and signatures (`Bio2ConstantTable.cs`, `Bio3ConstantTable.cs`,
  `OpcodeV2.cs`), item names, message codes (`Room/Msg.cs`), and the model, animation and room layouts
  (`Rbj.cs`, `Edd1.cs`, `Emr.cs`, `Md2.cs`, `Rdt1.cs`, `Rdt2.cs`).
- [BioRand](https://github.com/biorand/classic) (MIT, Copyright (c) 2022-2026 Ted John): how weapons
  are composed onto players (emdui, `TimExtensions.WithWeaponTexture`), RE1 weapon numbers
  (`re1NpcHelper.cs`), RE2 weapon slots, room and door data used to check names.

MIT License: Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
do so, subject to the following conditions: The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**Consulted for behaviour (no code taken):**

- [bio2-ds](https://github.com/MeganGrass/bio2-ds), a decompilation of Resident Evil 2 Dual Shock (no
  license stated): the script engine's if / else / endif and scheduler (`SCE0.C`, `SCE1.C`), the
  engine flags (`def/FLAG.H`), the zone checks and what each zone kind does (`SCE_AT.C`,
  `def/SCENARIO.H`), the pad's key bits for script key checks (`PAD.C`, `SCE2.C`), the frame's order
  and its every-frame handler (`GAME.C`, `SCE0.C` Sce_scheduler / Sce_work_clr), and item use, item
  scripts and item boxes (`ST_SUB00.C`, `SCE2.C`, `SCE_AT.C`). Our interpreter and zone checks are
  written independently from what it shows.
- [OpenBiohazard2](https://github.com/OpenBiohazard2/OpenBiohazard2) (MIT, Copyright (c) 2020 Samuel
  Yuan), a reimplementation of the Resident Evil 2 engine.
- [Bio2ScriptViewer](https://github.com/OpenBiohazard2/Bio2ScriptViewer) (MIT, Copyright (c) 2021 Samuel
  Yuan): RE2 script instruction layouts, used to cross-check ours.
- The reevengi notes (Patrice Mandin) on Resident Evil room files.

If code from a consulted MIT project is ported later, its notice moves to the list above.
