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

/* A choice in the same band: `title` on the first line, one of `labels` on the second (Left and
 * Right go through them), how to choose on the third. Cross picks, Triangle or Circle backs out.
 * Up to PE_RE_BOX_CHOICES labels of 40 characters; `at` is the one shown first. 1, or 0 as
 * pe_re_box_say. */
#define PE_RE_BOX_CHOICES 80
int pe_re_box_choose(const char *title, const char *const labels[], int n, int at);
/* What was chosen, reported once: its index, -2 backed out, or -1 (nothing yet, or no choice). */
int pe_re_box_choice(void);

#endif
