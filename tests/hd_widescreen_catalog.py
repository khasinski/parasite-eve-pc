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
