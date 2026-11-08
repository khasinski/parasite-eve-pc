/* SPDX-License-Identifier: MIT -- optional owner-selected private testkit copy. */
#ifndef PE_PORTABLE_TESTKIT_H
#define PE_PORTABLE_TESTKIT_H
/* 0 absent, 1 exclusively created, 2 existing destination preserved, -1 failed. */
int PePortable_SeedTestCard(const char *source,const char *destination);
#endif
