# Fixing imported models

How to make an imported model usable by a mod (costumes, enemies, held weapons, props), and
where each kind of fix belongs. Read this before changing a model reader, the costume builder,
the enemy builder, or any mod that shows imported models.

Baseline: feat/pe-import after the fit layer (see `git log -- mods/host/import/pe_imp_fit.c`). The model audit covered RE1 (Director's Cut Dual
Shock), RE2 disc 1 and RE3: 2,360 models, none failing to load. Results are in
`PE_Model_Audit_2026-11-19.md` (kept beside the repo; it lists ids and measurements only).

## The rule

An imported model is **never patched in place**, and a mod **never fixes a model itself**.
Readers record what the disc says. Fixes happen in one of four places (below), always on a copy,
always versioned, always with a reason when something is refused.

## Where a fix goes

| The problem | Example | Fix it in |
|---|---|---|
| **The reader reads the disc wrong** | a wrong texture/skeleton pairing, a wrong control code, the wrong section | **The reader** (`mods/pe_src_reN/`). Bump the reader's `.version` so stores re-read. Never work around a reading bug downstream. |
| **The data is true to the game but awkward to use** | placeholder (stub) parts, degenerate faces, faces outside the texture, scale, facing (RE models face −z) | **The fit layer** (`mods/host/import/pe_imp_fit.c`, see below). Applied per use, on a copy. |
| **One consumer can't take it** | a costume texture holds 2 pages and 16 colours per group; the costume builder is rigid-only | **That consumer's builder** (`pe_wardrobe/pe_skin_build.c`, `pe_enemy_import/`). Convert it, or refuse with a reason. |
| **One asset needs a person's call** | "this 15-bone model isn't a person", a height or name override | **A data table** in the game module, like the naming tables (`g_re2_naming`, `pe_imp_set_naming`). Not code. |

When unsure, **refuse with a reason**. A refused model is safe; a model built wrong (wrong texels,
wrong bones) is a bug players see.

## Every fix, step by step

1. **Measure it.** Add the check to the model audit (`tests/pe_model_audit.c`) and run it, so you
   know how many models are affected before changing anything:

   ```bash
   cmake --build build-mods --target test-mod-model-audit
   ```

   Then run `build-mods/mods/test-mod-model-audit.exe` with `PE_TEST_RE1_DISC`,
   `PE_TEST_RE2_DISC`, `PE_TEST_RE3_DISC` set to your own disc images (any subset) and
   `PE_AUDIT_OUT` to where the CSV should go.
2. **Decide:** fix it or refuse it. Write the reason down in the code comment.
3. **Implement it in the right layer** (table above).
4. **Test it.**
   - A fixture: a made-up model (or a copy of a real one with one thing changed, as the costume
     builder test moves one face two pages over) that triggers the problem.
   - The disc tests, run locally with your own discs (`tests/pe_mod_rdt_test.c`, the skin-build test).
   - The audit's counts: usable models per category must not drop.
5. **Version it.** Bump the version of what changed (reader, fit layer, builder) so stored results
   refresh.
6. **Document it** in `mods/README.md` (the section for that game or builder): what was wrong, the
   rule now, and the counts.

Never commit anything from a game disc: no extracted models, textures, CSVs of disc files beyond
ids and measurements, or fixtures cut from real files.

## Known facts from the audit

- **Texture pages.** A face's texel is `x = (page − first page the model uses) × 128 + u`, `y = v`
  (8-bit textures; a page is 128 texels). The costume builder used `(page & 1) × 128`, which reads
  the wrong page for a model on a third page (RE2 `pl0d`); fixed in `555f242d4`. A costume's texture
  holds two pages, so a model spanning three is refused until the builder can repack it.
- **Bone count isn't a body type.** RE1 `em100d/em1012/em110d/em1112` and `em2d` have 15 bones and
  aren't standing people. Decide "wearable" by skeleton shape (the tree and limb proportions of the
  RE human), not by count.
- **Stub parts** (one face) are placeholders: RE3's skinned enemies use them by design; RE2 `pl0d`
  and its weapons have 12–13. A costume drops them; an enemy keeps the bone.
- **Skinned models** are only RE3's enemies (45). The costume builder is rigid-only: refuse them as
  costumes, accept them as enemies.
- **Size.** Standing people are about 2,700–3,200 units in all three games. Take scale from the
  skeleton (head to foot in the rest pose), not the mesh's bounding box, which posed or prone
  models distort.
- **Props** carry most texture faults (faces past the picture's height in RE2/RE3 objects) and many
  are flat (decals, paper). Clamp or drop those faces for props; never block a level on them.

## The fit layer

`mods/host/import/pe_imp_fit.h` / `.c`. Mods ask for a model fitted for a use instead of loading it raw:

```c
int pe_imp_fit(const char *root, const char *asset_id, PeImpUse use,   /* COSTUME, ENEMY, WEAPON, PROP */
               PeImpModel *out, PeImpFitInfo *info, char *why, size_t n);
int pe_imp_fit_model(const PeImpModel *src, const char *asset_id, PeImpUse use, PeImpModel *out,
                     PeImpFitInfo *info, char *why, size_t n);            /* a Model already loaded */
int pe_imp_fit_deny(const char *asset_id, unsigned uses, const char *reason);   /* a person's call */
```

- **Classify.** A costume or held weapon needs:
  - the RE human skeleton: RE2's 15-bone tree, which RE1's and RE3's players are read into;
  - a joint height of 1,200–3,200 units;
  - rigid parts;
  - a texture on at most two pages, with every face inside it.

  An enemy or a prop takes any model with faces. A refusal says why: "bone 5 hangs from 3: not the RE
  human skeleton", "its faces use texture pages 0 to 2; a costume's texture holds two".
- **Normalize, on a copy, for that use only:**
  - zero-area faces are dropped for every use;
  - placeholder parts (one face in a 16-unit box) are emptied for costumes and weapons, keeping their bones;
  - faces outside the texture are dropped for props.

  The stored Model is never changed.
- **Report** (`PeImpFitInfo`):
  - is it a person, is it skinned;
  - its skeleton height and facing (−z for the RE games);
  - its texture pages;
  - faces kept and dropped.

  Height and facing are measured, not applied: each builder fits size and direction its own way.
- **No cache.** Fitting reads an already-stored Model (no disc) and takes milliseconds, so a rule
  change applies to the next build. Bump `PE_IMP_FIT_VERSION` and the consumer targets' versions
  when a rule changes what gets built.

Consumers:
- The costume target (`pe_wardrobe/pe_skin_target.c`) fits for `PE_IMP_USE_COSTUME`. It uses the
  rig registered for the asset's id, else the game's human rig (`pe_imp_rig_for_game`) when the fit
  finds a person; that brings RE2's people among its enemies to the Wardrobe.
- The enemy target (`pe_enemy_import/pe_enemy_target.c`) fits for `PE_IMP_USE_ENEMY`.

Consumers keep their own last checks (as `texture_fits` in the costume builder) and don't trust the
fit layer blindly. A model that fails to load at runtime falls back to Aya's default; it never
blocks a save or a room.

Tests: `test-mod-fit` (made-up models: every rule and refusal, no disc). The model audit has a
`wearable` column with each refusal's reason.

On the discs (RE1 DC, RE2 disc 1, RE3), wearable:

| | RE1 | RE2 | RE3 |
|---|---|---|---|
| players | 24 of 24 | 8 of 10 | 12 of 12 |
| weapons | 32 of 32 | 32 of 35 | 84 of 84 |
| people among the enemies | 0 | 31 | 0 |

The refusals:
- **Three texture pages:** RE2 `pl0a`, `pl0d` and `pl0d`'s weapons; RE2 people em40 (Irons), em44/em46 (Ben),
  em4a (Marvin), em59/em5a (Claire and Leon outfits). These need the costume builder to repack textures.
- **Skeleton in the wrong order:** RE1's and RE3's enemy-folder people are in their own game's bone order. Reading them into RE2's
  order, as the player readers do, would make them candidates (RE3's are also skinned).
- **Not a person:** RE2 `em2d`.
