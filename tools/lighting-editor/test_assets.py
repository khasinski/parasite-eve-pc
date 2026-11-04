"""Synthetic floor/texture/profile checks; optional real-disc room survey."""
import importlib.util,io,json,os,struct,tempfile,unittest,sys
from pathlib import Path
try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit(77)
from assets import collision,model_texture,Rooms,payload,u32,Disc
from serve import profile_valid,save_profile

class Assets(unittest.TestCase):
    def test_flat_heights_are_per_region(self):
        blob=bytearray(128);struct.pack_into('<HHH',blob,2,1,1,0);struct.pack_into('<III',blob,24,48,64,0);struct.pack_into('<I',blob,40,44)
        struct.pack_into('<h',blob,44,-123);struct.pack_into('<6h',blob,48,-100,0,100,0,0,200)
        struct.pack_into('<BB3H',blob,64,0,0,0,1,2)
        self.assertEqual(collision(blob)[0]['points'],[[-100,-123,0],[100,-123,0],[0,-123,200]])
        blob[64]=128;self.assertFalse(collision(blob)[0]['walkable'])
    def test_sloped_xyz_mesh(self):
        blob=bytearray(160);struct.pack_into('<HHH',blob,2,1,1,1);struct.pack_into('<III',blob,24,48,80,112)
        struct.pack_into('<9h',blob,48,0,0,0,100,-50,0,0,-100,100);struct.pack_into('<BBHi3H',blob,80,0,0,0,0,0,1,2)
        struct.pack_into('<3i',blob,112,32768,65536,65536)
        self.assertEqual(collision(blob)[0]['points'],[[0,0,0],[100,-50,0],[0,-100,100]])
        struct.pack_into('<H',blob,88,65000)
        with self.assertRaises(ValueError):collision(blob)
    def test_cue_resolves_its_relative_bin(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);data=bytearray(18*2048);data[16*2048+1:16*2048+6]=b'CD001'
            (root/'disc image.bin').write_bytes(data)
            cue=root/'disc.cue';cue.write_text('FILE "disc image.bin" BINARY\n  TRACK 01 MODE2/2352\n')
            disc=Disc(cue)
            try:self.assertEqual(disc.sectors(16,1)[1:6],b'CD001')
            finally:disc.data.close();disc.file.close()
            cue.write_text('REM invalid cue')
            with self.assertRaises(ValueError):Disc(cue)

    def test_palette_mask_and_page_coordinates(self):
        vram=np.zeros((512,1024),dtype='<u2');vram[256,960]=0x3210;vram[448,0:4]=[0,31,0x8000,31<<10]
        image=Image.open(io.BytesIO(model_texture(vram,31,448<<6))).convert('RGBA')
        self.assertEqual([image.getpixel((x,0)) for x in range(4)],[(0,0,0,0),(255,0,0,255),(0,0,0,255),(0,0,255,255)])
    def test_profile_roundtrip_and_incomplete_rejection(self):
        p=dict(format=1,map=20,aya=dict(position=[1,0,2],yaw=20),ambient=dict(color='#ffffff',intensity=.3),exposure=1,roughness=.8,lights=[])
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp);save_profile(directory,20,p,{'test':True});original=(directory/'M020.json').read_bytes()
            self.assertEqual(json.loads(original)['profile'],p)
            p['exposure']=float('nan')
            with self.assertRaises(ValueError):save_profile(directory,20,p,{})
            self.assertEqual((directory/'M020.json').read_bytes(),original)
            self.assertEqual(len(list(directory.iterdir())),1)
    @unittest.skipUnless(os.getenv('PE_TEST_BIN'),'needs a local USA BIN')
    def test_all_original_room_geometry(self):
        rooms=Rooms(Path(os.environ['PE_TEST_BIN']));self.assertGreaterEqual(len(rooms.catalog),400)
        slopes=0;floors=0;views=0
        for room in rooms.catalog:
            blob=rooms.raw(room['map']);directory=u32(blob,4)
            floor=collision(payload(blob,directory,0x18));floors+=bool(floor)
            slopes+=any(len({p[1] for p in tri['points']})>1 for tri in floor)
            for triangle in floor:
                for point in triangle['points']:self.assertEqual(len(point),3)
            views+=len(room['views'])
        self.assertGreaterEqual(floors,400);self.assertGreater(slopes,0);self.assertGreaterEqual(views,500)
        print(f'Original geometry: {len(rooms.catalog)} rooms, {views} cameras, {slopes} rooms with slopes')
        for room in (1,20,89,163,195,320):
            data,image=rooms.load(room,0);self.assertEqual(Image.open(io.BytesIO(image)).size,(data['width'],data['height']))
            self.assertTrue(data['floor'])

if __name__=='__main__':unittest.main()
