"""Offline high-quality WebP pack regression; requires Pillow and cwebp."""
import hashlib,importlib.util,json,shutil,subprocess,sys,tempfile,unittest
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
 def test_corrected_candidate_version_and_sizes(self):
  module.convert(self.source,self.dest,workers=1)
  manifest_path=self.dest/'manifest.json';manifest=json.loads(manifest_path.read_text())
  manifest.update(distribution_bytes=1,image_bytes=2);manifest_path.write_text(json.dumps(manifest))
  original={p.name:p.read_bytes() for p in self.dest.iterdir() if p.is_file()}
  encoder=Path(__file__).resolve().parents[1]/'docs/assets/hd-animated/corrections/issue-56/encode_webp.py'
  for name,args,version in [('versioned',['--version','0.2'],'0.2'),('retained',[],'0.1')]:
   with self.subTest(name=name):
    output=self.root/name
    subprocess.run([sys.executable,str(encoder),'--source',str(self.dest),'--out',str(output),*args],check=True,capture_output=True,text=True)
    result=json.loads((output/'manifest.json').read_text())
    self.assertEqual(result['version'],version)
    self.assertEqual(result['image_bytes'],sum(p.stat().st_size for p in output.iterdir() if p.is_file() and p.name!='manifest.json'))
    self.assertEqual(result['distribution_bytes'],sum(p.stat().st_size for p in output.iterdir() if p.is_file()))
    for filename,data in original.items():
     self.assertEqual((self.dest/filename).read_bytes(),data)
     if filename!='manifest.json':self.assertEqual((output/filename).read_bytes(),data)
 def test_empty_candidate_version_does_not_create_output(self):
  encoder=Path(__file__).resolve().parents[1]/'docs/assets/hd-animated/corrections/issue-56/encode_webp.py'
  result=subprocess.run([sys.executable,str(encoder),'--source',str(self.source),'--out',str(self.dest),'--version',' '],capture_output=True,text=True)
  self.assertNotEqual(result.returncode,0)
  self.assertIn('--version must not be empty',result.stderr)
  self.assertFalse(self.dest.exists())
 def test_candidate_version_matches_downloader_format(self):
  encoder=Path(__file__).resolve().parents[1]/'docs/assets/hd-animated/corrections/issue-56/encode_webp.py'
  for version in ['0.2-rc1','2','0..2','0.2/escape','0.2 '+'.','1.'+'0'*19]:
   with self.subTest(version=version):
    result=subprocess.run([sys.executable,str(encoder),'--source',str(self.source),'--out',str(self.dest),'--version',version],capture_output=True,text=True)
    self.assertNotEqual(result.returncode,0)
    self.assertIn('dotted numeric version',result.stderr)
    self.assertFalse(self.dest.exists())
if __name__=='__main__':unittest.main()
