/* PE Enhancements: combat styles. Prototype playstyles (Quickstep, Striker,
 * Bulwark, Channeler) the player picks in F1 > PE Enhancements > Combat styles.
 * Off by default: then combat is vanilla.
 *
 * Independent of the Wardrobe: costumes are cosmetic. A later version can
 * link a costume to a style through a service (pe_style_for_costume keeps the
 * planned mapping); nothing here reads the Wardrobe today. Design and status:
 * STYLES.md. */
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_style.h"

#define MOD_ID "pe_styles"

static int styles_init(void) {
  pe_menu_begin(PE_MENU_PAGE);
  pe_menu_page("Combat styles",
               "Prototype playstyles with a benefit and a challenge. Off by "
               "default.",
               25);
  pe_menu_begin(0);
  pe_styles_register(MOD_ID);
  return 0;
}

const PeMod pe_mod_pe_styles = {MOD_ID, "PE Enhancements: combat styles",
                                PE_ENHANCEMENTS_VERSION, NULL, styles_init,
                                NULL};
