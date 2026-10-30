"""Create a consistent Lighting Lab download archive and checksum."""
import argparse,hashlib,json,shutil,tarfile,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent;REPO=ROOT.parents[1]
p=argparse.ArgumentParser();p.add_argument('--platform',required=True,choices=['linux','windows','macos']);p.add_argument('--arch',required=True);p.add_argument('--version',required=True);a=p.parse_args()
work=REPO/'build/lighting-editor';release=work/'release';release.mkdir(exist_ok=True)
package=Path(json.loads((work/'desktop/packages.json').read_text())[0]);stem=f'Parasite-Eve-Lighting-Lab-{a.version}-{a.platform}-{a.arch}'
if a.platform=='macos':
    import subprocess
    app=package/'Parasite Eve Lighting Lab.app';target=release/(stem+'.zip');subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(app),str(target)],check=True)
elif a.platform=='windows':
    target=release/(stem+'.zip')
    with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for file in package.rglob('*'):
            if file.is_file():archive.write(file,Path('Parasite Eve Lighting Lab')/file.relative_to(package))
else:
    target=release/(stem+'.tar.gz')
    with tarfile.open(target,'w:gz') as archive:archive.add(package,arcname='Parasite Eve Lighting Lab')
(release/(stem+'.sha256')).write_text(hashlib.sha256(target.read_bytes()).hexdigest()+'  '+target.name+'\n');print(target)
