"""Exercise the production CLI/ESC installer against controlled localhost releases."""
import hashlib,http.server,json,os,pathlib,subprocess,sys,tempfile,threading,time,unittest,zipfile,io,shutil
BINARY=pathlib.Path(sys.argv.pop(1)).resolve() if len(sys.argv)>1 else None
KEY='0123456789abcdef'
def pack(version,kind='good'):
 payload=b'background material'*(16384 if kind=='slow' else 1);ref=b'lossless reference'
 filename='../escape.webp' if kind=='traversal' else KEY+'.webp'
 manifest=dict(version=version,atlases=[dict(file=filename,atlas_sha256=hashlib.sha256(payload).hexdigest(),reference_file=KEY+'.ref.png',reference_sha256=hashlib.sha256(ref).hexdigest())])
 data=io.BytesIO()
 with zipfile.ZipFile(data,'w',compression=zipfile.ZIP_STORED) as z:
  def write(name,data):z.writestr(zipfile.ZipInfo(name,(2026,1,1,0,0,0)),data)
  write('manifest.json',json.dumps(manifest));write(filename,b'corrupt' if kind=='corrupt' else payload);write(KEY+'.ref.png',ref)
 return data.getvalue()
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_GET(self):
  mode=self.server.mode;version='0.1' if mode in ('good','hash') else '0.2'
  blob=pack(version,'slow' if mode=='slow' else mode if mode in ('corrupt','traversal') else 'good')
  if self.path.endswith('/latest'):
   m=dict(version=version,url=f'http://127.0.0.1:{self.server.server_port}/upscale-v{version}.zip',sha256='0'*64 if mode=='hash' else hashlib.sha256(blob).hexdigest(),size=len(blob),format='unsupported' if mode=='format' else 'webp-reference-png')
   blob=json.dumps(m).encode()
  else:
   self.server.downloads+=1
   self.server.client_headers=dict(self.headers)
  self.send_response(200);self.send_header('Content-Length',str(len(blob)));self.end_headers()
  try:
   if mode=='truncated' and self.path.endswith('.zip'):self.wfile.write(blob[:len(blob)//2]);self.close_connection=True
   elif mode=='slow' and self.path.endswith('.zip'):
    for n in range(0,len(blob),2048):self.wfile.write(blob[n:n+2048]);self.wfile.flush();time.sleep(.02)
   else:self.wfile.write(blob)
  except (BrokenPipeError,ConnectionResetError):pass
class Installer(unittest.TestCase):
 def test_portable_install_is_independent_of_working_directory(self):
  self.assertTrue(BINARY and BINARY.exists())
  with tempfile.TemporaryDirectory() as directory:
   root=pathlib.Path(directory);launch=root/'unrelated-cwd';launch.mkdir()
   server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);server.mode='good';server.downloads=0
   threading.Thread(target=server.serve_forever,daemon=True).start()
   try:
    for mode in ('flag','marker','override')+(('bundle',) if sys.platform=='darwin' else ()):
     install=root/mode;install.mkdir();binary_dir=install/'Parasite Eve.app/Contents/MacOS' if mode=='bundle' else install;binary_dir.mkdir(parents=True,exist_ok=True)
     exe=binary_dir/BINARY.name;shutil.copy2(BINARY,exe)
     for dll in BINARY.parent.glob('*.dll'):shutil.copy2(dll,binary_dir/dll.name)
     if mode!='flag':(install/'portable.txt').touch()
     env=dict(os.environ,SDL_AUDIO_DRIVER='dummy',PE_HD_SERVER=f'http://127.0.0.1:{server.server_port}',LD_LIBRARY_PATH=str(BINARY.parent),DYLD_LIBRARY_PATH=str(BINARY.parent));env.pop('PE_CONFIG_HOME',None)
     expected=install/'userdata'
     if mode=='override':expected=root/'explicit-home';env['PE_CONFIG_HOME']=str(expected)
     args=[str(exe)]+(['--portable'] if mode=='flag' else [])+['--download-backgrounds']
     result=subprocess.run(args,cwd=launch,env=env,capture_output=True,text=True,timeout=30)
     self.assertEqual(result.returncode,0,(mode,result.stdout,result.stderr))
     pointer=expected/'mods/upscale/active.json';self.assertTrue(pointer.is_file(),mode)
     self.assertTrue(pathlib.Path(json.loads(pointer.read_text())['directory']).is_relative_to(expected),mode)
     if mode=='override':self.assertFalse((install/'userdata').exists())
    self.assertFalse((launch/'userdata').exists())
   finally:server.shutdown();server.server_close()

 def test_atomic_download_update_and_failures(self):
  self.assertTrue(BINARY and BINARY.exists())
  with tempfile.TemporaryDirectory() as temporary:
   root=pathlib.Path(temporary);config=root/'settings with spaces-zażółć'
   server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);server.daemon_threads=True;server.mode='good';server.downloads=0
   thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
   env=dict(os.environ,PE_CONFIG_HOME=str(config),PE_HD_BACKGROUNDS='',PE_HD_SERVER=f'http://127.0.0.1:{server.server_port}')
   env.pop('PE_HD_TEST_CANCEL_AT',None)
   def run(command='--download-backgrounds',extra=None):
    return subprocess.run([str(BINARY),command],env={**env,**(extra or {})},cwd=root,capture_output=True,text=True,timeout=45)
   pointer=config/'mods/upscale/active.json'
   try:
    result=run();self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertTrue(pointer.is_file(),f'Installer wrote to the wrong Unicode path: {result.stdout} / {result.stderr}; files: {list(root.rglob(chr(42)))}');old=pointer.read_bytes();installed=pathlib.Path(json.loads(old)['directory']);self.assertEqual((installed/(KEY+'.webp')).read_bytes(),b'background material')
    self.assertIn('X-PE-Version',server.client_headers);self.assertIn('X-PE-Build',server.client_headers);self.assertIn('ParasiteEve/',server.client_headers['User-Agent'])
    self.assertEqual(server.downloads,1);result=run();self.assertEqual(result.returncode,0,result.stdout);self.assertEqual(server.downloads,1);self.assertEqual(pointer.read_bytes(),old)
    self.assertEqual(run('--verify-backgrounds').returncode,0)
    for mode in ('hash','corrupt','traversal','truncated','format','slow'):
     server.mode=mode;result=run(extra={'PE_HD_TEST_CANCEL_AT':'1'} if mode=='slow' else None)
     self.assertNotEqual(result.returncode,0,(mode,result.stdout,result.stderr));self.assertEqual(pointer.read_bytes(),old,mode);self.assertEqual((installed/(KEY+'.webp')).read_bytes(),b'background material')
     self.assertFalse(list((config/'mods/upscale').glob('*.partial*')),mode)
    self.assertFalse(list(root.rglob('escape.webp')))
    (installed/(KEY+'.webp')).write_bytes(b'damaged on disk');self.assertNotEqual(run('--verify-backgrounds').returncode,0)
    server.mode='good';result=run();self.assertEqual(result.returncode,0,result.stdout);new=pathlib.Path(json.loads(pointer.read_bytes())['directory']);self.assertNotEqual(new,installed);self.assertEqual((new/(KEY+'.webp')).read_bytes(),b'background material');self.assertEqual((installed/(KEY+'.webp')).read_bytes(),b'damaged on disk')
   finally:server.shutdown();server.server_close()
if __name__=='__main__':unittest.main()
