#ifndef PE_MOD_STATE_H
#define PE_MOD_STATE_H

/* Live state a mod shows to test tools: what its F1 rows can't say (a pending payout, the active
 * challenges, a purchase in flight). Debug control's `mod-state` and `snapshot` read it
 * (docs/DEBUG_CONTROL.md). A provider is called only while PE_DEBUG_CONTROL is on and its mod is
 * running, never by the game itself. It only reads: it must not change game or mod state.
 *
 *   static void my_state(PeStateWriter *w, void *user) {
 *       pe_state_int(w, "pending", s_pending);
 *       pe_state_begin_list(w, "active");
 *       for (...) pe_state_text(w, NULL, name);
 *       pe_state_end(w);
 *   }
 *   pe_mod_state_provider("wallet", my_state, NULL);   (from the mod's init)
 *
 * Keys are JSON object keys; pass NULL for a key inside a list. Unclosed lists and objects are
 * closed after the provider returns, and nesting deeper than six levels is dropped. */

typedef struct PeStateWriter PeStateWriter;
typedef void (*PeModStateFn)(PeStateWriter *w, void *user);

/* From a mod's init only; the provider belongs to that mod. 0, or -1: refused (outside init,
 * NULL or empty name, NULL fn, the name already used by this mod, or no room left). */
int pe_mod_state_provider(const char *name, PeModStateFn fn, void *user);

void pe_state_int(PeStateWriter *w, const char *key, long long value);
void pe_state_bool(PeStateWriter *w, const char *key, int value);
void pe_state_text(PeStateWriter *w, const char *key, const char *value);   /* NULL: null */
void pe_state_begin_object(PeStateWriter *w, const char *key);
void pe_state_begin_list(PeStateWriter *w, const char *key);
void pe_state_end(PeStateWriter *w);

#endif
