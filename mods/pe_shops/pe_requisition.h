#ifndef PE_REQUISITION_H
#define PE_REQUISITION_H

/* Requisition's backend (pe_requisition.c), shared by its F1 page (pe_requisition_menu.c) and
 * any later shop front end. Goods are identified by id, not row, so a row keeps its identity
 * when the board changes; economy effects use the pseudo ids 0xF000..0xF003. */

#include <stddef.h>
#include <stdint.h>

int pe_shop_count(void);                         /* rows on the board now */
uint16_t pe_shop_item(int row);                  /* goods id of a row, or 0 */
const char *pe_shop_name(uint16_t item);
uint32_t pe_shop_price(uint16_t item);           /* after difficulty and Bargain */
const char *pe_shop_blocked(uint16_t item);      /* why it can't be bought now, or NULL */
int pe_shop_buy(uint16_t item, char *message, size_t size);   /* 1 if bought; message says what happened */
int pe_shop_rescue_needed(void);                 /* Aya has no usable weapon anywhere */
int pe_shop_spare_club(char *message, size_t size);
void pe_shop_details(uint16_t item, char *out, unsigned size);
void pe_shop_at_counter(int on);                 /* a shop front end (Wayne's box) is open */

void pe_requisition_register(const char *mod_id);      /* rules, save block, tuning rows */
void pe_requisition_menu_register(const char *mod_id); /* the F1 page */

#endif
