# Foreground side layers (PEW2 kind 1)

Widescreen side art used to be one backdrop behind every actor, so the side
continuation of a foreground object (M001's ambulance and police car) was drawn
under characters walking past it. These tools build a depth-sorted layer for
such objects; see "Title and widescreen artwork" in `docs/HD_BACKGROUNDS.md`.

1. `inspect_room.py ROOM GROUP PACK OUT` renders the native composite, each
   entry on its own (with tile depths and x range) and the pack's HD side
   strips. Entries with a depth below 4088 are foreground layers; the ones
   reaching x 0 or 319 continue into a side.
2. `cut.py SIDES_HD.png SPEC.json OUT` cuts each strip with GrabCut. The
   native silhouette at the seam is fixed foreground/background; SPEC boxes and
   polygons (HD pixels in the 224-wide strip) seed the rest. Check the
   `*-overlay.png` images and adjust the spec (`M001-g0-cut.json` is the
   reviewed M001 one).
3. `build_front.py ROOM GROUP SRC_PACK OUT_PACK MASKDIR left:ENTRY right:ENTRY`
   writes the cut cells as a new atlas and rewrites the room's sidecar as PEW2:
   the original records as backdrops plus the layer, each cell's depth taken
   from the entry's tiles nearest the seam (extrapolated across the side).
   OUT_PACK starts as a hard-linked copy; the source pack is never written.

4. `hd_matte.py ROOM GROUP SRC_PACK OUT_PACK ENTRY ...` fixes the stairs on the
   room's own foreground entries. Their native silhouette is stair-stepped and the
   upscaled art inside the stairs shows the background, so those pixels were pasted
   over actors. It cuts each entry with GrabCut on the room's HD composite, in a band
   from 2 native px inside to 1 px outside the outline, writes RGBA atlases with a
   `PEM1` static-matte marker, and moves the given-up stair pixels into a
   `-under.bin` underlay behind the room where no other layer covers them (so no
   black holes open). Only for entries without animation controllers. Review the
   `matte-ROOM-eE.png` overlays: red is removed, green added.

Requires Python with NumPy, Pillow and OpenCV, and your own Disc 1 image (the
path is set in `inspect_room.py`, like the other scripts in `widescreen/`).
