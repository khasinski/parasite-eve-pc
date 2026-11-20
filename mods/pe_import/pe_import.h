#ifndef PE_IMPORT_H
#define PE_IMPORT_H
#include <stddef.h>
#include <stdint.h>

#include "pe_imp.h"
#include "import_host.h"

/* The automatic import (pe_import_job.c): scan the folder, build what source games mark automatic. */
#define PE_IMPORT_FOLDERS 8
typedef struct {
    char discs[1100], store[1100], packs[1100];               /* folders */
    char extra[PE_IMPORT_FOLDERS][1100];                      /* more folders the player added */
    int nextra;
    const ImportHost *host;           /* the game: its data for the builder targets, opened only when something is built */
    const PeImpProgress *progress;    /* the job: "reading <disc>" i of n, "building <name>" k of m */
    const PeImpProgress *step;        /* inside a build: its steps and the room scan */
    /* results; unchanged: disc images skipped because nothing changed since they were last looked at */
    int discs_read, unknown, failed, items, built, current, missing, unchanged;
    char games[200], problem[240], built_names[400], missing_discs[400];
} PeImportJob;
void pe_import_job_run(PeImportJob *j);

#endif
