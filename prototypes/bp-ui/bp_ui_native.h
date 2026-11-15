#ifndef PE_BP_UI_NATIVE_PREVIEW_H
#define PE_BP_UI_NATIVE_PREVIEW_H
struct MenuWidgetNode;
/* Explicit developer-only entry; deliberately never registered with the game.
 * Caller must already own a native menu context. Returns 0 when busy/invalid.
 * This does not enter field-menu mode, register F1 rows or change Wayne. */
int pe_bp_ui_preview_open(struct MenuWidgetNode *from, const char *page_id);
/* Live data: source() returns the current catalog (NULL = use the sample catalog) and the
 * backend commits confirmations. Without a source the screens stay sample-only. */
struct BpUiCatalog;
struct BpUiBackend;
void pe_bp_ui_set_source(const struct BpUiCatalog *(*source)(void), const struct BpUiBackend *backend);
#endif
