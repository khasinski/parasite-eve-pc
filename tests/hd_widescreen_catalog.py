"""Widescreen artwork scope must not become an unnecessary outpainting queue."""
import importlib.util
from pathlib import Path
import tempfile
import json
import unittest

try:
    import cv2
    from PIL import Image
except ImportError:
    cv2 = None


@unittest.skipIf(cv2 is None, 'Pillow and OpenCV required for authoring')
class Scope(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[1]/'docs/assets/hd-widescreen/prepare.py'
        spec = importlib.util.spec_from_file_location('wide_prepare', path)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def view(self, width=320, height=240, controllers=None):
        return dict(active_bbox=[0,0,width,height], controllers=controllers or [])

    def test_field_view_uses_display_height_not_scrolling_canvas(self):
        self.assertEqual(self.module.field_view_width(), 398)
        self.assertEqual(self.module.field_view_width(height=240), 426)
        self.assertEqual(self.module.scope_record(self.view(512,512))['action'], 'verify-native-wide')
        self.assertEqual(self.module.scope_record(self.view(400,512))['action'], 'verify-native-wide')

    def test_narrow_tall_view_still_requires_horizontal_art(self):
        self.assertEqual(self.module.scope_record(self.view(320,512))['action'], 'extend-static')

    def test_animation_and_short_banner_cannot_enter_static_batch(self):
        self.assertEqual(self.module.scope_record(self.view(controllers=[{}]))['action'], 'extend-with-original-animation')
        self.assertEqual(self.module.scope_record(self.view(height=64))['action'], 'review-intentional-framing')

    def test_prepared_is_a_review_candidate_not_a_verified_map(self):
        result = self.module.scope_record(self.view(), prepared=True)
        self.assertEqual(result['action'], 'review-existing-extension')
        self.assertFalse(result['runtime_verified'])
        self.assertEqual(self.module.scope_record(self.view(512), prepared=True)['action'], 'verify-native-wide')

    def test_empty_dimensions_are_rejected(self):
        with self.assertRaises(ValueError): self.module.field_view_width(height=0)
        with self.assertRaises(ValueError): self.module.scope_record(self.view(0))

    def test_outpainting_export_refuses_native_wide_views_before_writes(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as tmp:
            pack=Path(tmp)/'pack';pack.mkdir()
            manifest=pack/'manifest.json';manifest.write_text('{"widescreen":[]}')
            before=manifest.read_bytes()
            for width in (398,400,512):
                view=self.view(width,512)
                with patch.object(self.module,'catalog',return_value=(
                        {('M235',0):view},{'M235':{}})):
                    with self.assertRaisesRegex(ValueError,'Native-wide views'):
                        self.module.package('M235',0,Path(tmp)/'missing-generated.png',
                                            Path(tmp)/'missing-center.png',pack)
                self.assertEqual(manifest.read_bytes(),before)
                self.assertEqual(list(pack.iterdir()),[manifest])

    def test_packaging_backdrop_preserves_foreground_and_source_hardlinks(self):
        from unittest.mock import patch
        import os
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); pack=root/'pack'; pack.mkdir()
            geom='0123456789abcdef'
            front=dict(group=2,key='123456789abcdef0',bounds=[-56,-39,0,17],
                       positions=[(-56,0),(-40,16)],kind=1,depths=[104,682])
            old=dict(group=1,key='23456789abcdef01',bounds=[-56,376,0,16],
                     positions=[(-56,0)],kind=0,depths=[])
            payload=self.module.encode_sidecar([old,front])
            source=root/'source.bin';source.write_bytes(payload)
            os.link(source,pack/(geom+'-wide.bin'))
            # Another room can name the very same serialized geometry/group binding.
            (pack/'manifest.json').write_text(json.dumps(dict(widescreen=[
                dict(room='M377',group=1,geometry_key=geom)],extras=[])))
            center=root/'center.png';Image.new('RGB',(1280,64),'blue').save(center)
            generated=root/'generated.png';Image.new('RGB',(1728,64),'green').save(generated)
            view=dict(active_bbox=[0,0,320,16],native_origin=[0,0],controllers=[])
            room=dict(geometry_key=geom,entries=[dict(group=1,tiles=1,flags=2)])
            with patch.object(self.module,'catalog',return_value=({('M044',1):view},{'M044':room})), \
                 patch.object(self.module,'register',return_value=(Image.open(generated),{})):
                self.module.package('M044',1,generated,center,pack,margin=48)
                with self.assertRaisesRegex(ValueError,'complete widescreen'):
                    self.module.package('M044',1,generated,center,pack,margin=38)
            result=self.module.read_sidecar(pack/(geom+'-wide.bin'))
            self.assertEqual(next(r for r in result if r['kind']==1),front)
            self.assertEqual(len(result),2)
            backdrop=next(r for r in result if r['kind']==0)
            self.assertEqual(backdrop['bounds'],[-48,368,0,16])
            self.assertEqual(len(backdrop['positions']),6)
            self.assertEqual(source.read_bytes(),payload)
            manifest=json.loads((pack/'manifest.json').read_text())
            self.assertEqual(len(manifest['widescreen']),1)
            self.assertEqual(manifest['widescreen'][0]['room'],'M044')
            index=next(r for r in manifest['extras'] if r['kind']=='widescreen-index')
            self.assertEqual(index['sha256'],self.module.digest(pack/index['file']))

    def test_native_extent_checks_inactive_phases_and_keeps_existing_art(self):
        import numpy as np
        with tempfile.TemporaryDirectory() as tmp:
            assets=Path(tmp)/'assets';pack=Path(tmp)/'pack';pack.mkdir()
            geom='0123456789abcdef';view=dict(self.view(512,240),room='M235',group=0,native_origin=[0,0])
            view['controllers']=[dict(id=0)]
            room=dict(room='M235',geometry_key=geom,entries=[])
            for study in ('hd-static','hd-day1','hd-animated'):
                root=assets/study;(root/'refs').mkdir(parents=True)
                (root/'views.json').write_text('[]')
                (root/'tasks.json').write_text(json.dumps(dict(rooms=[],tasks=[])))
            root=assets/'hd-animated'
            # Opaque PS1 black is valid; a coloured inactive phase outside the
            # corner must not prevent exporting the existing canvas extent.
            black=np.zeros((1,16,16),dtype=np.uint16);black[0,0,0]=0x8000
            colour=np.full((1,16,16),0x1f,dtype=np.uint16)
            np.save(root/'refs/black.npy',black);np.save(root/'refs/colour.npy',colour)
            import hashlib
            tasks=[dict(room='M235',group=0,entry=0,ref='black.npy',tiles=[[0,0]],ref_sha256=hashlib.sha256(black.tobytes()).hexdigest()),
                   dict(room='M235',group=0,entry=1,ref='colour.npy',tiles=[[256,128]],ref_sha256=hashlib.sha256(colour.tobytes()).hexdigest())]
            def write():
                (root/'views.json').write_text(json.dumps([view]))
                (root/'tasks.json').write_text(json.dumps(dict(rooms=[room],tasks=tasks)))
            write();(pack/'manifest.json').write_text('{"widescreen":[],"extras":[]}')
            record=self.module.package_native_extent('M235',0,pack,assets)
            self.assertEqual(record['bounds'],[0,512,0,240])
            self.assertFalse(record['runtime_verified'])
            self.assertEqual(len(record['native_corner_references']),2)
            payload=(pack/(geom+'-wide.bin')).read_bytes()
            atlas=(pack/(record['key']+'.png')).read_bytes()
            # Extent metadata must not paint over flashes or the native clear colour.
            self.assertFalse(np.asarray(Image.open(pack/(record['key']+'.png')))[:,:,3].any())
            self.module.package_native_extent('M235',0,pack,assets)
            self.assertEqual(payload,(pack/(geom+'-wide.bin')).read_bytes())
            self.assertEqual(atlas,(pack/(record['key']+'.png')).read_bytes())
            before=(pack/'manifest.json').read_bytes()
            expected=tasks[1]['ref_sha256'];tasks[1]['ref_sha256']='0'*64;write()
            with self.assertRaisesRegex(ValueError,'checksum'):
                self.module.package_native_extent('M235',0,pack,assets)
            tasks[1]['ref_sha256']=expected
            tasks[1]['tiles']=[[0,0]];write()
            with self.assertRaisesRegex(ValueError,'serialized phase'):
                self.module.package_native_extent('M235',0,pack,assets)
            self.assertEqual(before,(pack/'manifest.json').read_bytes())
            tasks[1]['tiles']=[[256,128]];view['active_bbox'][2]=320;write()
            with self.assertRaisesRegex(ValueError,'already wide'):
                self.module.package_native_extent('M235',0,pack,assets)
            view['active_bbox'][2]=512;write()
            manifest=json.loads(before);manifest['widescreen'][0].pop('native_extent')
            (pack/'manifest.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'existing outpainted'):
                self.module.package_native_extent('M235',0,pack,assets)

    def test_foreground_rebuild_preserves_other_layers_and_updates_hashes(self):
        from unittest.mock import patch
        import runpy,sys,types,hashlib
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'src';dst=root/'dst';masks=root/'masks'
            src.mkdir();masks.mkdir();geom='0123456789abcdef'
            backdrop=dict(group=2,key='1111111111111111',bounds=[-56,376,0,16],
                positions=[(-56,0),(-40,0),(-24,0),(-8,0),(320,0),(336,0),(352,0),(368,0)],kind=0,depths=[])
            other=dict(group=1,key='2222222222222222',bounds=[-56,-55,0,1],
                positions=[(-56,0)],kind=1,depths=[250])
            payload=self.module.encode_sidecar([backdrop,other])
            (src/(geom+'-wide.bin')).write_bytes(payload)
            Image.new('RGBA',(16*68,2*68),(50,80,110,255)).save(src/(backdrop['key']+'.png'))
            Image.new('L',(224,64),255).save(masks/'M044-left-mask.png')
            (src/'manifest.json').write_text(json.dumps(dict(widescreen=[dict(room='M044',group=2,geometry_key=geom)],extras=[])))
            fake=types.ModuleType('inspect_room')
            fake.atlas_cells=lambda pack,key:[__import__('numpy').full((64,64,4),255,dtype='uint8') for _ in range(8)]
            fake.native_layers=lambda room,group:({},[dict(entry=4,tiles=[dict(x=0,y=0,depth=100),dict(x=16,y=0,depth=110)])])
            script=Path(__file__).resolve().parents[1]/'docs/assets/hd-widescreen/foreground/build_front.py'
            for source in (src,dst):
                argv=[str(script),'M044','2',str(source),str(dst),str(masks),'left:4']
                with patch.dict(sys.modules,{'inspect_room':fake,'prepare':self.module}),patch.object(sys,'argv',argv):
                    runpy.run_path(str(script),run_name='__main__')
                records=self.module.read_sidecar(dst/(geom+'-wide.bin'))
                self.assertEqual(len(records),3)
                self.assertIn(other,records)
                self.assertEqual(next(r for r in records if r['kind']==0),backdrop)
                manifest=json.loads((dst/'manifest.json').read_text())
                self.assertEqual(len(manifest['widescreen_foreground']),1)
                for extra in manifest['extras']:
                    self.assertEqual(extra['sha256'],hashlib.sha256((dst/extra['file']).read_bytes()).hexdigest())
            # A cell at the right overscan boundary is only half inside the
            # canvas. Keep its authored half, with transparent padding outside.
            right_mask=Image.new('L',(224,64),255)
            right_mask.paste(0,(8,8,12,12))
            right_mask.save(masks/'M044-right-mask.png')
            fake.native_layers=lambda room,group:({},[dict(entry=4,tiles=[
                dict(x=288,y=0,depth=90),dict(x=304,y=0,depth=100)])])
            argv=[str(script),'M044','2',str(src),str(dst),str(masks),'right:4']
            with patch.dict(sys.modules,{'inspect_room':fake,'prepare':self.module}),patch.object(sys,'argv',argv):
                runpy.run_path(str(script),run_name='__main__')
            front=next(r for r in self.module.read_sidecar(dst/(geom+'-wide.bin'))
                       if r['group']==2 and r['kind']==1)
            self.assertEqual(front['positions'],[(320,0),(336,0),(352,0),(368,0)])
            import numpy as np
            rgba=np.array(Image.open(dst/(front['key']+'.png')).convert('RGBA'))
            cell=rgba[2:66,3*68+2:3*68+66,3]
            self.assertTrue((cell[:,:32]==255).all())
            self.assertTrue((cell[:,32:]==1).all())
            # Preserve the coherent source RGB through a hole in the matte.
            # Alpha 1/255 survives loader extrusion, but extension shader's
            # binary 0.5 cutoff still discards it completely.
            hole=rgba[10:14,10:14]
            self.assertTrue((hole[...,:3]==255).all())
            self.assertTrue((hole[...,3]==1).all())
            self.assertFalse((hole[...,3]/255.0>=0.5).any())

            # M081's irregular tree layer has no single depth plane. Its
            # per-row depths must still use the native (5, 8) tile grid.
            fake.native_layers=lambda room,group:({},[dict(entry=4,tiles=[
                dict(x=293,y=-8,depth=90),dict(x=309,y=-8,depth=100),
                dict(x=293,y=8,depth=300),dict(x=309,y=8,depth=280)])])
            with patch.dict(sys.modules,{'inspect_room':fake,'prepare':self.module}),patch.object(sys,'argv',argv):
                runpy.run_path(str(script),run_name='__main__')
            front=next(r for r in self.module.read_sidecar(dst/(geom+'-wide.bin'))
                       if r['group']==2 and r['kind']==1)
            self.assertEqual(front['positions'],[(309,-8),(325,-8),(341,-8),(357,-8),(373,-8),
                (309,8),(325,8),(341,8),(357,8),(373,8)])
            self.assertEqual(front['depths'],[100,110,120,130,140,280,260,240,220,200])

            # A thin railing can have a disconnected far-depth fragment in the
            # next tile. A reviewed source plane must not infer its slope from
            # that unrelated fragment (the M208 edge case).
            fake.native_layers=lambda room,group:({},[dict(entry=4,tiles=[
                dict(x=0,y=7,depth=1000),dict(x=16,y=7,depth=4087),
                dict(x=0,y=23,depth=1048),dict(x=16,y=23,depth=1008)])])
            guide_path=masks/'M044-left-depth.json'
            guide=dict(entry=4,anchors=[[0,7,1000],[0,23,1048],[16,23,1008]])
            guide_path.write_text(json.dumps(guide))
            argv=[str(script),'M044','2',str(src),str(dst),str(masks),'left:4']
            with patch.dict(sys.modules,{'inspect_room':fake,'prepare':self.module}),patch.object(sys,'argv',argv):
                runpy.run_path(str(script),run_name='__main__')
            records=self.module.read_sidecar(dst/(geom+'-wide.bin'))
            front=next(r for r in records if r['group']==2 and r['kind']==1)
            self.assertEqual(front['depths'],[1112,1072,1032,992,1160,1120,1080,1040])
            self.assertEqual(front['positions'],[(-64,-9),(-48,-9),(-32,-9),(-16,-9),
                (-64,7),(-48,7),(-32,7),(-16,7)])
            self.assertIn(other,records)
            manifest=json.loads((dst/'manifest.json').read_text())
            strip=manifest['widescreen_foreground'][0]['strips'][0]
            self.assertEqual(strip['depth_guide_sha256'],hashlib.sha256(guide_path.read_bytes()).hexdigest())
            stable={p.name:p.read_bytes() for p in dst.iterdir() if p.is_file()}
            bad=[
                ([[0,7,1001],[0,23,1048],[16,23,1008]],'original tile'),
                ([[0,7,1000],[16,7,4087],[0,7,1000]],'stable local plane'),
                ([[0,7,1000],[16,7,4087],[0,23,1048],[16,23,1008]],'stable local plane'),
                ([[0,7,1000],[16,7,4087],[0,23,1048]],'PEW2 foreground range'),
            ]
            for anchors,message in bad:
                guide_path.write_text(json.dumps(dict(entry=4,anchors=anchors)))
                with patch.dict(sys.modules,{'inspect_room':fake,'prepare':self.module}),patch.object(sys,'argv',argv):
                    with self.assertRaisesRegex(ValueError,message):
                        runpy.run_path(str(script),run_name='__main__')
                self.assertEqual(stable,{p.name:p.read_bytes() for p in dst.iterdir() if p.is_file()})
            self.assertEqual((src/(geom+'-wide.bin')).read_bytes(),payload)

    def test_sidecar_roundtrip_and_invalid_depth_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'wide.bin'
            for kind in (0,1):
                record=dict(group=2,key='0123456789abcdef',bounds=[-56,-39,0,17],
                            positions=[(-56,0),(-40,16)],kind=kind,depths=[104,682] if kind else [])
                path.write_bytes(self.module.encode_sidecar([record]))
                self.assertEqual(self.module.read_sidecar(path),[record])
            record['depths']=[]
            with self.assertRaises(ValueError):self.module.encode_sidecar([record])
            path.write_bytes(path.read_bytes()+b'extra')
            with self.assertRaises(ValueError):self.module.read_sidecar(path)

    def test_exact_shared_source_reuses_binding_but_different_phase_does_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets=Path(tmp)/'assets';pack=Path(tmp)/'pack';pack.mkdir()
            rooms=[];views=[];tasks=[];retail=[]
            for i in range(3):
                room=f'M{i+1:03}'
                views.append(dict(self.view(),room=room,group=0,native_origin=[0,0],base_canvas_input='base.png'))
                rooms.append(dict(room=room,geometry_key='0123456789abcdef',entries=[dict(index=0,group=0,tiles=1,flags=2)]))
                tasks.append(dict(room=room,group=0,entry=0,input='base.png',ref='changed.npy' if i==2 else 'ref.npy'))
                retail.append(dict(disc=1,id=room,views=[dict(group=0,width=320,height=240,camera_bounds=[160,160,112,128],tile_bounds_active=[0,0,320,240])]))
            for study in ['hd-static','hd-day1','hd-animated']:
                root=assets/study;(root/'inputs').mkdir(parents=True);(root/'refs').mkdir()
                (root/'views.json').write_text(json.dumps(views if study=='hd-static' else []))
                (root/'tasks.json').write_text(json.dumps(dict(rooms=rooms if study=='hd-static' else [],tasks=tasks if study=='hd-static' else [])))
                Image.new('RGB',(320,240),'blue').save(root/'inputs/base.png')
                (root/'refs/ref.npy').write_bytes(b'original phase')
                (root/'refs/changed.npy').write_bytes(b'different serialized phase')
            (assets/'widescreen').mkdir();(assets/'widescreen/catalog.json').write_text(json.dumps(dict(rooms=retail)))
            (pack/'manifest.json').write_text(json.dumps(dict(widescreen=[dict(room='M001',group=0)])))
            result=self.module.audit_catalog(assets,pack,Path(tmp)/'scope.json',published_pack=pack)
            self.assertEqual(result['variant_binding_reviews'],1)
            self.assertEqual(result['jobs'][0]['action'],'review-variant-binding')
            # Restore identical phase data: all aliases share the published art.
            tasks[2]['ref']='ref.npy'
            (assets/'hd-static/tasks.json').write_text(json.dumps(dict(rooms=rooms,tasks=tasks)))
            result=self.module.audit_catalog(assets,pack,Path(tmp)/'scope.json',published_pack=pack)
            self.assertEqual(result['variant_binding_reviews'],0)
            self.assertEqual(result['unique_binding_jobs'],1)
            self.assertEqual(result['action_counts'],{'verify-published-extension':3})
            self.assertFalse(result['jobs'][0]['new_artwork'])
            self.assertTrue(all(not r['runtime_verified'] for r in result['views']))
            candidate=self.module.audit_catalog(assets,pack,Path(tmp)/'scope.json')
            self.assertEqual(candidate['action_counts'],{'review-existing-extension':3})
            self.assertIsNone(candidate['published_pack_manifest_sha256'])


if __name__ == '__main__': unittest.main()
