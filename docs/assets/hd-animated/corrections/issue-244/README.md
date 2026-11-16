# M116 open passage correction (#244)

Pack 0.4 borrowed the intact M056 gate and staircase geometry. The original M116 passage is already open in every phase. Aya and the injured officer therefore appeared to overlap bars or steps that only existed in the HD artwork.

A single OpenAI imagegen clean plate restores the original opening and landing. The registered plate is applied to the same static region of all relevant packets. The warning light, native coverage, depth and animation timing retain their original data. The side-door pose atlases are byte identical to 0.4.

`provenance.json` records the full built-in imagegen prompt, input hashes, raw image hash and accepted registered material. `registration-v1.json` records the global affine fit (44 inliers). The raw and registered PNGs remain in the ignored authoring asset store and its Fenrir backup; the existing 0.4 pack is immutable.

Build a separate candidate, then render every phase using the existing compositor:

```sh
python build_candidate.py --baseline /path/to/pack-0.4 --material M116-open-gate-registered-v1.png --output /path/to/pack-0.5
python ../../composite.py --room M116 --pack /path/to/pack-0.5 --output /path/to/previews
python check_passage.py /path/to/previews --report /path/to/report.json
```

The content regression measures the original clear floor aperture, independently of the patching algorithm. It rejects the closed-bar artwork in all ten controller phases and accepts the corrected frames. Packet generation additionally requires unchanged alpha, references and pixels outside the repair support. `M116-pack-audit.json` verifies that exactly eight keys change and every other atlas, reference and extra remains byte identical.

Runtime proof: `out/release-solid-20261116/M116-original-before-after.png` (original, pack 0.4, corrected pack). All captures use headless GPU rendering.
