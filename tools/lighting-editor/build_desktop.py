"""Build the editor and its private Python runtime as a desktop application."""
import argparse,json,os,platform,shutil,subprocess,sys,importlib.metadata
from pathlib import Path
ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[1]

def run(args):subprocess.run([str(a) for a in args],cwd=REPO,check=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--reuse-backend',action='store_true');parser.add_argument('--game',type=Path,default=REPO/('build/parasite-eve-pc.exe' if os.name=='nt' else 'build/parasite-eve-pc'));args=parser.parse_args()
    work=REPO/'build/lighting-editor';work.mkdir(parents=True,exist_ok=True)
    backend=work/'desktop-backend/lighting-backend'
    if not args.reuse_backend or not backend.exists():
        run([sys.executable,'-m','PyInstaller','--noconfirm','--onedir','--name','lighting-backend',
             '--distpath',work/'desktop-backend','--workpath',work/'pyinstaller-work','--specpath',work,
             '--exclude-module','cv2','--exclude-module','matplotlib','--exclude-module','scipy','--exclude-module','tkinter',ROOT/'serve.py'])
    stage=work/'desktop-stage'
    if stage.exists():shutil.rmtree(stage)
    stage.mkdir()
    for name in ['index.html','editor.js','style.css','desktop.cjs','desktop-preload.cjs','desktop-setup.html','desktop-setup.js','desktop-setup.css']:
        shutil.copy2(ROOT/name,stage/name)
    manifest=json.loads((ROOT/'package.json').read_text());manifest.pop('devDependencies',None);manifest.pop('scripts',None)
    (stage/'package.json').write_text(json.dumps(manifest,indent=2)+'\n')
    vendor=stage/'node_modules/three';vendor.mkdir(parents=True)
    for name in ['build','examples/jsm']:
        shutil.copytree(ROOT/'node_modules/three'/name,vendor/name)
    for name in ['LICENSE','package.json']:
        shutil.copy2(ROOT/'node_modules/three'/name,vendor/name)
    shutil.copy2(REPO/'LICENSE.md',stage/'LICENSE.md')
    licenses=stage/'licenses';licenses.mkdir()
    for name in ['LICENSE','LICENSES.chromium.html']:
        shutil.copy2(ROOT/'node_modules/electron/dist'/name,licenses/('Electron-'+name))
    for name in ['numpy','Pillow','pyinstaller']:
        distribution=importlib.metadata.distribution(name)
        for file in distribution.files:
            if '/licenses/' not in str(file):continue
            destination=licenses/name/str(file).split('/licenses/',1)[1]
            destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(distribution.locate_file(file),destination)
    for directory in [Path(sys.base_prefix),*Path(sys.base_prefix).parents]:
        source=next((directory/name for name in ['LICENSE','LICENSE.txt'] if (directory/name).is_file()),None)
        if source:shutil.copy2(source,licenses/'Python-LICENSE.txt');break
    (licenses/'README.txt').write_text('Electron 45.1.0 and Chromium: adjacent licenses. Three.js 0.187.0: ../node_modules/three/LICENSE.\nPython, NumPy and Pillow: adjacent licenses. Python runtime is bundled by PyInstaller (GPL with bootloader exception).\nGame assets are read from the user-owned disc and capture, not shipped in this application.\n')
    resources=work/'desktop-resources/backend'
    if resources.exists():shutil.rmtree(resources)
    resources.mkdir(parents=True);shutil.copytree(backend,resources/'lighting-backend')
    if not args.game.exists():raise FileNotFoundError('Build the original game asset importer: '+str(args.game))
    shutil.copy2(args.game,resources/('lighting-capture.exe' if os.name=='nt' else 'lighting-capture'))
    out=work/'desktop';out.mkdir(exist_ok=True)
    run(['node',ROOT/'package_desktop.mjs',stage,out,resources,REPO/'packaging/macos/ParasiteEve.icns'])
    packages=json.loads((out/'packages.json').read_text())
    if sys.platform=='darwin':
        app=Path(packages[0])/'Parasite Eve Lighting Lab.app'
        run(['codesign','--force','--deep','--sign','-',app])
        run(['codesign','--verify','--deep','--strict',app])
        print('Desktop application:',app)
    else:print('Desktop application:',packages[0])
    (work/'desktop-build.json').write_text(json.dumps(dict(packages=packages,platform=sys.platform,architecture=platform.machine(),bundled_python=True,user_game_assets_included=False),indent=2)+'\n')
if __name__=='__main__':main()
