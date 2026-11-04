/* SPDX-License-Identifier: MIT -- use the actual read-only game validator. */
#include "pe_bootstrap.h"
#include "pe1/cdrom.h"
#include "pe1/boot_disc_check.h"
int pe_bootstrap_validate_game_disc(const char *path,unsigned expected,void *user){
    (void)user;PeDisc d;unsigned identity=0;
    if(!pe_disc_open(&d,path))return 0;
    int ok=d.sector_size==2352&&Asset_ValidatePeImage(&d,&identity)==0&&identity==expected;
    pe_disc_close(&d);return ok;
}
