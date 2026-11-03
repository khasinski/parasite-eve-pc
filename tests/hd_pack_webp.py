"""Offline high-quality WebP pack regression; requires Pillow and cwebp."""
import hashlib,importlib.util,json,shutil,tempfile,unittest
from pathlib import Path
from PIL import Image
spec=importlib.util.spec_from_file_location('compress_webp',Path(__file__).resolve().parents[1]/'docs/assets/hd-poc/compress_webp.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
@unittest.skipUnless(shutil.which('cwebp'),'cwebp is required')
class WebpPack(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);self.source=self.root/'source';self.source.mkdir();self.dest=self.root/'webp'
  self.image=Image.new('RGB',(68,136));self.image.putdata([(x*3,y,70) if y<68 else (0,255 if (x+y)%2 else 123,0) for y in range(136) for x in range(68)])
  self.key='0123456789abcdef';p=self.source/(self.key+'.png');self.image.save(p)
  self.manifest=dict(version='0.1',atlases=[dict(key=self.key,file=p.name,atlas_sha256=hashlib.sha256(p.read_bytes()).hexdigest())]);self.write()
 def write(self):(self.source/'manifest.json').write_text(json.dumps(self.manifest))
 def test_reference_and_title_are_exact(self):
  title=self.source/'title.png';self.image.save(title);self.manifest['extras']=[dict(file=title.name,kind='title-background',sha256=hashlib.sha256(title.read_bytes()).hexdigest())];self.write()
  module.convert(self.source,self.dest,workers=1)
  with Image.open(self.dest/(self.key+'.ref.png')) as ref:self.assertEqual(ref.convert('RGBA').tobytes(),self.image.crop((0,68,68,136)).convert('RGBA').tobytes())
  with Image.open(self.dest/'title.webp') as title:self.assertEqual(title.convert('RGBA').tobytes(),self.image.convert('RGBA').tobytes())
  manifest=json.loads((self.dest/'manifest.json').read_text());r=manifest['atlases'][0]
  with Image.open(self.dest/r['file']) as a,Image.open(self.dest/r['reference_file']) as b:self.assertEqual(r['pixel_sha256'],hashlib.sha256(a.convert('RGBA').tobytes()+b.convert('RGBA').tobytes()).hexdigest())
  self.assertFalse(manifest['compression']['lossless'])
 def test_authored_coverage_alpha_is_lossless(self):
  self.image=self.image.convert('RGBA')
  for y in range(68):
   for x in range(68):
    rgb=self.image.getpixel((x,y))[:3];self.image.putpixel((x,y),(*rgb,(x*17+y*31)%256))
  path=self.source/(self.key+'.png');self.image.save(path)
  self.manifest['atlases'][0]['atlas_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();self.write()
  module.convert(self.source,self.dest,workers=1)
  with Image.open(self.dest/(self.key+'.webp')) as material:
   self.assertEqual(material.convert('RGBA').getchannel('A').tobytes(),self.image.crop((0,0,68,68)).getchannel('A').tobytes())
  with Image.open(self.dest/(self.key+'.ref.png')) as reference:
   self.assertEqual(reference.convert('RGBA').tobytes(),self.image.crop((0,68,68,136)).tobytes())
 def test_bad_source_never_activates(self):
  self.manifest['atlases'][0]['atlas_sha256']='0'*64;self.write()
  with self.assertRaises(ValueError):module.convert(self.source,self.dest,workers=1)
  self.assertFalse(self.dest.exists());self.assertFalse(list(self.root.glob('webp.partial-*')))
 def test_existing_pack_is_retained(self):
  self.dest.mkdir();(self.dest/'keep').write_text('yes')
  with self.assertRaises(FileExistsError):module.convert(self.source,self.dest)
  self.assertEqual((self.dest/'keep').read_text(),'yes')
if __name__=='__main__':unittest.main()
