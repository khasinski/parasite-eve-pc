/* SPDX-License-Identifier: MIT -- selected application storage roots; no libc before CRT. */
#ifndef PE_PORTABLE_ROOT_H
#define PE_PORTABLE_ROOT_H
/* 1: unique active PS5GameTest app in local/USB homebrew; -1: missing/ambiguous/IO failure. No marker. */
int PePortable_Mode(void);
int PePortable_Error(void);
const char *PePortable_Root(void);
const char *PePortable_AppRoot(void);
const char *PePortable_UserRoot(void);
const char *PePortable_StorageRoot(void);
/* 0 missing defaults,1 deterministic disc root,2 ambiguous defaults. Explicit paths remain valid. */
int PePortable_AssetsState(void);
/* Optional explicit read-only settings import. 0 absent, 1 path, -1 invalid. */
int PePortable_ImportConfig(char *path,unsigned capacity);
#endif
