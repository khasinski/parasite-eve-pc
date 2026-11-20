#ifndef PE_RE_BOX_H
#define PE_RE_BOX_H
/* Messages in the game's own dialogue band (the box its rooms talk in), as the phone mod shows
 * calls: text wrapped to the game's font, three lines a page, Cross turns the page and closes the
 * last. While a box is up Aya stands still (the field lock) and the pad is the box's. */

void pe_re_box_init(const char *mod_id);
/* Show `text` (ASCII; other characters print as spaces). 1, or 0 if no box is free or the font
 * isn't loaded yet (the caller can toast it instead). */
int pe_re_box_say(const char *text);
int pe_re_box_busy(void);
void pe_re_box_forget(void);            /* a room load or a stop: drop it without waiting */

#endif
