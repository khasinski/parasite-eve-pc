"""Original UI key stability and deterministic lossless pack encoding."""
import importlib.util
from pathlib import Path
import tempfile
import sys
import json
import hashlib
try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit(77)

spec=importlib.util.spec_from_file_location('hd_interface',Path(__file__).resolve().parents[1]/'tools/hd_interface.py')
ui=importlib.util.module_from_spec(spec);spec.loader.exec_module(ui)
vram=np.zeros((512,1024),dtype=np.uint16)
vram[256:,640:704]=0x1111
vram[240,641]=31
key,original=ui.material(vram,26,(240<<6)|40,0,0,12,12)
assert key=='ed6af081e350b68d'
assert original.getpixel((0,0))==(255,0,0,255)
vram[257,650]=0x2222
assert ui.material(vram,26,(240<<6)|40,0,0,12,12)[0]==key
vram[240,641]=31<<10
assert ui.material(vram,26,(240<<6)|40,0,0,12,12)[0]!=key
vram[240,641]=0x8000
assert ui.material(vram,26,(240<<6)|40,0,0,12,12)[1].getpixel((0,0))==(0,0,0,255)
vram[240,641]=0
assert ui.material(vram,26,(240<<6)|40,0,0,12,12)[1].getpixel((0,0))[3]==0
with tempfile.TemporaryDirectory(prefix='pe-ui-pack-') as tmp:
    root=Path(tmp);source=root/'source';source.mkdir()
    image=Image.new('RGBA',(3,3));image.putpixel((1,1),(255,128,0,255));image.save(source/(key+'.png'))
    ui.build(source,root/'pack')
    manifest=json.loads((root/'pack/manifest.json').read_text())
    assert len(manifest['materials'])==1
    target=root/'pack'/('ui-'+key+'.webp')
    with Image.open(target) as decoded:
        assert decoded.size==(12,12)
        assert decoded.convert('RGBA').tobytes()==ui.scale2x(ui.scale2x(image)).tobytes()
    base=root/'base';base.mkdir();(base/'title.webp').write_bytes(b'original background bytes')
    digest=hashlib.sha256((base/'title.webp').read_bytes()).hexdigest()
    (base/'manifest.json').write_text(json.dumps(dict(atlases=[],extras=[dict(file='title.webp',kind='title-background',sha256=digest)])))
    ui.combine(base,root/'pack',root/'combined','test')
    combined=json.loads((root/'combined/manifest.json').read_text())
    assert combined['interface']['materials']==1 and len(combined['extras'])==2
    (root/'combined/title.webp').write_bytes(b'edited preview')
    assert (base/'title.webp').read_bytes()==b'original background bytes'
    (root/'pack'/('ui-'+key+'.webp')).write_bytes(b'incomplete')
    try:ui.combine(base,root/'pack',root/'bad-combined','test')
    except ValueError:pass
    else:raise AssertionError('Incomplete UI pack was accepted')
    assert not (root/'bad-combined').exists()
    try:ui.build(source,root/'pack')
    except FileExistsError:pass
    else:raise AssertionError('Existing pack was overwritten')
print('PASS: UI keys, palette/native mask classes, lossless WebP and existing-pack protection')
