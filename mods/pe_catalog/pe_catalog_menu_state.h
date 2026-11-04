#ifndef PE_CATALOG_MENU_STATE_H
#define PE_CATALOG_MENU_STATE_H

/* Viewport state is independent of the item format's current ten-slot cap. */
typedef struct PeCatalogCursor { int row, top; } PeCatalogCursor;
void pe_catalog_cursor_clamp(PeCatalogCursor *cursor, int count, int visible);

#endif
