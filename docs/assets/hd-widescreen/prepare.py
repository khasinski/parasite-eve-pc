"""Register reviewed outpainting and package only new horizontal side tiles.
The existing atlases and their 4:3 pixels are never rewritten.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import cv2
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
ASSETS = ROOT / 'docs/assets'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def catalog(assets=ASSETS):
    views = {}
    rooms = {}
    for study in ('hd-static', 'hd-day1', 'hd-animated'):
        root = assets / study
        tasks = json.loads((root / 'tasks.json').read_text())
        rooms.update({r['room']: r for r in tasks['rooms'] if 'geometry_key' in r})
        for v in json.loads((root / 'views.json').read_text()):
            views[v['room'], v['group']] = {**v, 'study': study}
    return views, rooms


def field_view_width(width=320, height=224):
    """Match PsyZ's whole-pixel 16:9 viewport for the NTSC field display.

    A tall scrolling background is still viewed through 224 screen lines;
    dividing its whole canvas width by its whole height gives the wrong scope.
    """
    if width <= 0 or height <= 0:
        raise ValueError('Invalid field dimensions')
    aspect = width / height
    return 2 * math.floor(min(width * (16 / 9) / aspect, width / .75) / 2)


def scope_record(view, target_width=398, prepared=False, published=False):
    left, top, right, bottom = view['active_bbox']
    width, height = right-left, bottom-top
    if width <= 0 or height <= 0:
        raise ValueError('Empty source view')
    if width >= target_width:
        action = 'verify-native-wide'
    elif height < 224:
        action = 'review-intentional-framing'
    elif published:
        action = 'verify-published-extension'
    elif prepared:
        action = 'review-existing-extension'
    elif view['controllers']:
        action = 'extend-with-original-animation'
    else:
        action = 'extend-static'
    return dict(source_width=width, source_height=height,
                target_width=target_width, action=action,
                new_pixels_required_by_width=width < target_width,
                runtime_verified=False)


def audit_catalog(assets, pack, output, published_pack=None):
    """Inventory scope and aliases; never label dimensions as runtime proof."""
    from collections import Counter, defaultdict
    views, rooms = catalog(assets)
    retail = json.loads((assets/'widescreen/catalog.json').read_text())
    native = {(r['id'],v['group']):v for r in retail['rooms'] if r['disc']==1 for v in r['views']}
    work_path=assets/'hd-widescreen/expansion-20261108/work-catalog.json'
    work=json.loads(work_path.read_text())['records'] if work_path.exists() else []
    edge_entries={(r['room'],r['group']):r.get('animated_edge_entries') for r in work}
    prepared={(r['room'],r['group']) for r in work if r.get('work_status') in
              ('already-installed','installed-static-extension')}
    manifest=json.loads((pack/'manifest.json').read_text())
    known_art={(r['room'],r['group']) for r in manifest.get('widescreen',[])}
    prepared |= known_art
    shipped=json.loads((published_pack/'manifest.json').read_text()) if published_pack else {}
    published={(r['room'],r['group']) for r in shipped.get('widescreen',[])}
    groups=defaultdict(list); records=[]; hashes={}
    def cached_digest(path):
        if path not in hashes:hashes[path]=digest(path)
        return hashes[path]
    study_data={study:json.loads((assets/study/'tasks.json').read_text()) for study in ('hd-static','hd-day1','hd-animated')}
    for key,view in sorted(views.items()):
        room,group=key
        source=assets/view['study']/'inputs'/view['base_canvas_input']
        if not source.exists():raise FileNotFoundError(source)
        geometry=rooms[room]['geometry_key']; frame=native.get(key,{})
        entries=[e for e in rooms[room]['entries'] if e['group']==group and e['tiles']]
        source_tasks=[t for t in study_data[view['study']]['tasks'] if (t['room'],t['group'])==key]
        contexts=[(t['entry'],cached_digest(assets/view['study']/'inputs'/t['input']),
                   cached_digest(assets/view['study']/'refs'/t['ref'])) for t in source_tasks]
        signature=hashlib.sha256(json.dumps([sorted(contexts),[(e['index'],e['flags']) for e in entries]],sort_keys=True).encode()).hexdigest()
        controlled={slot['entry'] for c in view['controllers'] for slot in c['slots']}
        unstable=[e['index'] for e in entries if (e['index'] not in controlled and not e['flags']&2) or e['flags']&12]
        record=dict(room=room,group=group,study=view['study'],geometry_key=geometry,
                    native_origin=view['native_origin'],active_bbox=view['active_bbox'],
                    source_sha256=cached_digest(source),serialized_context_sha256=signature,
                    script_or_scroll_review_entries=unstable,controller_count=len(view['controllers']),
                    serialized_edge_animation_entries=edge_entries.get(key),
                    camera_bounds=frame.get('camera_bounds'),
                    original_tile_bounds=frame.get('tile_bounds_active'),
                    **scope_record(view,field_view_width(),key in prepared,key in published))
        if record['action']=='extend-static' and unstable:record['action']='review-script-switched-or-scrolled'
        record['camera_extent_review_required']=bool(frame.get('camera_bounds') and
            (frame['camera_bounds'][0]!=frame['camera_bounds'][1] or frame['camera_bounds'][2]!=frame['camera_bounds'][3]))
        groups[(geometry,group)].append(record)
        records.append(record)
    aliases=[]
    for (geometry,group),family in sorted(groups.items()):
        if len(family)<2:continue
        contexts={(r['source_sha256'],r['serialized_context_sha256'],tuple(r['active_bbox']),tuple(r['native_origin'])) for r in family}
        aliases.append(dict(geometry_key=geometry,group=group,
                            views=[f"{r['room']}-g{r['group']:02}" for r in family],
                            distinct_source_contexts=len(contexts),
                            variant_binding_review_required=len(contexts)>1))
    jobs=[]
    for (geometry,group),family in sorted(groups.items()):
        contexts={(r['serialized_context_sha256'],r['source_sha256'],tuple(r['active_bbox']),tuple(r['native_origin'])) for r in family}
        published_family=[r for r in family if (r['room'],r['group']) in published]
        prepared_family=[r for r in family if (r['room'],r['group']) in prepared]
        if len(contexts)==1:
            representative=(published_family or prepared_family or family)[0]
            for record in family:
                record['shared_artwork_representative']=[representative['room'],group]
                if record['new_pixels_required_by_width'] and record['source_height']>=224:
                    if published_family:record['action']='verify-published-extension'
                    elif prepared_family:record['action']='review-existing-extension'
            jobs.append(dict(geometry_key=geometry,group=group,
                             representative=[representative['room'],group],
                             aliases=[[r['room'],group] for r in family],
                             action=representative['action'],new_artwork=representative['action'] in
                             ('extend-static','extend-with-original-animation'),runtime_verified=False,
                             serialized_edge_animation_entries=representative['serialized_edge_animation_entries']))
        else:
            jobs.append(dict(geometry_key=geometry,group=group,
                             aliases=[[r['room'],group] for r in family],
                             action='review-variant-binding',new_artwork=False,runtime_verified=False))
    unrepresented=[dict(room=k[0],group=k[1],dimensions=[v['width'],v['height']],
                        action='review-native-scene-without-hd-background',runtime_verified=False)
                   for k,v in sorted(native.items()) if k not in views]
    result=dict(scope='All existing HD background views; geometry-only views tracked separately. Source dimensions and aliases are triage, not complete camera/layer/runtime qualification.',
                field_view=dict(width=320,height=224,widescreen_width=field_view_width()),
                source_pack_manifest_sha256=digest(pack/'manifest.json'),
                published_pack_manifest_sha256=digest(published_pack/'manifest.json') if published_pack else None,
                hd_views=len(records),retail_views=len(native),
                action_counts=dict(Counter(r['action'] for r in records)),
                shared_geometry_families=len(aliases),
                variant_binding_reviews=sum(a['variant_binding_review_required'] for a in aliases),
                unique_binding_jobs=len(jobs),job_action_counts=dict(Counter(j['action'] for j in jobs)),
                views=records,aliases=aliases,jobs=jobs,native_only_views=unrepresented,
                requirements=['No unnecessary outpainting of native-wide views',
                              'Coherent foreground and backdrop materials and HD mattes',
                              'Original animation phases, palette changes, scripts and gameplay remain authoritative',
                              'Existing 4:3 materials retained except explicitly approved artwork corrections',
                              'Camera-edge and actor-occlusion checks with hidden GPU replay',
                              'Published pack uses versioning, checked hashes and atomic installation'])
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2)+'\n')
    return result


def register(center, generated, margin=224):
    """Similarity registration; reject composition drift rather than hide it."""
    a = cv2.cvtColor(np.asarray(center.convert('RGB')), cv2.COLOR_RGB2GRAY)
    b = cv2.cvtColor(np.asarray(generated.convert('RGB')), cv2.COLOR_RGB2GRAY)
    sift = cv2.SIFT_create(nfeatures=6000)
    ka, da = sift.detectAndCompute(a, None)
    kb, db = sift.detectAndCompute(b, None)
    if da is None or db is None:
        raise ValueError('No reliable image registration features')
    matches = cv2.BFMatcher().knnMatch(db, da, k=2)
    good = [pair[0] for pair in matches if len(pair) == 2 and pair[0].distance < .65 * pair[1].distance]
    if len(good) < 30:
        raise ValueError('Too few registration matches')
    src = np.float32([kb[m.queryIdx].pt for m in good])
    dst = np.float32([ka[m.trainIdx].pt for m in good]) + [margin, 0]
    matrix, inliers = cv2.estimateAffinePartial2D(src, dst, method=cv2.RANSAC,
        ransacReprojThreshold=2, maxIters=10000)
    if matrix is None or int(inliers.sum()) < 30 or inliers.mean() < .65:
        raise ValueError('Outpainting moved the original composition')
    error = np.linalg.norm(cv2.transform(src[None], matrix)[0] - dst, axis=1)[inliers[:, 0] > 0]
    p95 = float(np.quantile(error, .95))
    if p95 > 2 or abs(matrix[0, 1]) > .02:
        raise ValueError('Outpainting cannot be aligned safely')
    pixels = cv2.warpAffine(np.asarray(generated.convert('RGB')), matrix,
        (center.width + margin * 2, center.height), borderMode=cv2.BORDER_REPLICATE)
    # Check that the warp has real generated pixels everywhere in the new sides.
    coverage = cv2.warpAffine(np.ones(b.shape, np.uint8), matrix,
        (center.width + margin * 2, center.height), flags=cv2.INTER_NEAREST)
    if not coverage[2:-2,2:-2].all():
        raise ValueError('Registered image does not cover the whole extension')
    pixels[:, margin:margin+center.width] = np.asarray(center.convert('RGB'))
    return Image.fromarray(pixels), dict(transform=matrix.tolist(), matches=len(good),
        inliers=int(inliers.sum()), residual_p95=p95)


def read_sidecar(path):
    """Read either authoring format without discarding depth-sorted layers."""
    data = Path(path).read_bytes()
    if data[:4] not in (b'PEW1', b'PEW2'):
        raise ValueError('Unknown widescreen sidecar format')
    version2 = data[:4] == b'PEW2'
    count, = struct.unpack_from('<I', data, 4)
    records = []; offset = 8
    for _ in range(count):
        group, cells, key, *bounds = struct.unpack_from('<IIQiiii', data, offset)
        offset += 32
        kind = 0
        if version2:
            kind, = struct.unpack_from('<I', data, offset); offset += 4
        if kind not in (0, 1):
            raise ValueError('Unknown widescreen layer kind')
        positions = [struct.unpack_from('<hh', data, offset + j*4) for j in range(cells)]
        offset += cells*4
        depths = list(struct.unpack_from(f'<{cells}H', data, offset)) if kind else []
        offset += cells*2 if kind else 0
        records.append(dict(group=group, key=f'{key:016x}', bounds=bounds,
                            positions=positions, depths=depths, kind=kind))
    if offset != len(data):
        raise ValueError('Trailing widescreen sidecar data')
    return records


def encode_sidecar(records):
    version2 = any(r.get('kind', 0) for r in records)
    data = bytearray((b'PEW2' if version2 else b'PEW1') + struct.pack('<I', len(records)))
    for record in records:
        positions = record['positions']; kind = record.get('kind', 0)
        if kind not in (0, 1):
            raise ValueError('Unknown widescreen layer kind')
        data.extend(struct.pack('<IIQiiii', record['group'], len(positions),
                                int(record['key'], 16), *record['bounds']))
        if version2: data.extend(struct.pack('<I', kind))
        for xy in positions: data.extend(struct.pack('<hh', *xy))
        if kind:
            if len(record['depths']) != len(positions):
                raise ValueError('Foreground depth count does not match cells')
            data.extend(struct.pack(f'<{len(positions)}H', *record['depths']))
    return bytes(data)


def _store_backdrop(pack, metadata, atlas):
    """Replace a shared binding atomically, preserving other groups and PEW2 depths."""
    geometry,group,key=metadata['geometry_key'],metadata['group'],metadata['key']
    pack=Path(pack);manifest_path=pack/'manifest.json';manifest=json.loads(manifest_path.read_text())
    records=[r for r in manifest.get('widescreen',[])
             if (r.get('geometry_key'),r['group'])!=(geometry,group)]
    records.append(metadata)
    # The sidecar is authoritative for foreground records; older manifests only
    # describe backdrops. Replacing one backdrop must preserve every other layer.
    sidecar=pack/(geometry+'-wide.bin')
    selected=read_sidecar(sidecar) if sidecar.exists() else []
    selected=[r for r in selected if not (r['group']==group and r.get('kind',0)==0)]
    selected.append(metadata)
    data=encode_sidecar(selected)
    atlas_path=pack/(key+'.png')
    # Candidate packs use hard links: replace files rather than mutating a source pack.
    temporary=atlas_path.with_suffix('.partial.png');atlas.save(temporary);temporary.replace(atlas_path)
    temporary=sidecar.with_suffix('.partial');temporary.write_bytes(data);temporary.replace(sidecar)
    extra_files={key+'.png':'widescreen-atlas',sidecar.name:'widescreen-index'}
    extras=[r for r in manifest.get('extras',[]) if r['file'] not in extra_files]
    extras.extend(dict(file=name,kind=kind,sha256=digest(pack/name)) for name,kind in extra_files.items())
    manifest['extras']=extras;manifest['widescreen']=records
    temporary=manifest_path.with_suffix('.partial');temporary.write_text(json.dumps(manifest,indent=2)+'\n');temporary.replace(manifest_path)
    return metadata


def package_native_extent(room, group, pack, assets=ASSETS):
    """Declare an existing wide canvas, including original black cutaway space.

    PEW1 requires a material cell. A transparent marker at a verified black
    corner supplies that record without painting over the original background.
    """
    views,rooms=catalog(assets);view=views[room,group]
    x0,y0,x1,y1=view['active_bbox']
    if x1-x0<field_view_width() or y1-y0<224:
        raise ValueError('Native extent requires an already wide full-height source')
    geometry=rooms[room]['geometry_key'];pack=Path(pack)
    installed=json.loads((pack/'manifest.json').read_text())
    if any(r['geometry_key']==geometry and r['group']==group and not r.get('native_extent')
           for r in installed.get('widescreen',[])):
        raise ValueError('Native extent must not replace existing outpainted artwork')
    root=assets/view['study'];data=json.loads((root/'tasks.json').read_text())
    tasks=[t for t in data['tasks'] if (t['room'],t['group'])==(room,group)]
    if not tasks:raise ValueError('Missing native phase references')
    sources=[]
    for task in tasks:
        path=root/'refs'/task['ref'];sha=digest(path)
        refs=np.load(path,allow_pickle=False)
        pixel_sha=hashlib.sha256(refs.tobytes()).hexdigest()
        if task.get('ref_sha256',pixel_sha)!=pixel_sha:raise ValueError('Native reference checksum mismatch')
        if refs.shape!=(len(task['tiles']),16,16):raise ValueError('Invalid native tile references')
        # Include inactive and controlled entries: every serialized phase must
        # keep this corner black. Colours elsewhere in the room are untouched.
        for ref,(x,y) in zip(refs,task['tiles']):
            l,t,r,b=max(x,x0),max(y,y0),min(x+16,x0+16),min(y+16,y0+16)
            if l<r and t<b and np.any(ref[t-y:b-y,l-x:r-x]&0x7fff):
                raise ValueError('Native corner contains colour in a serialized phase')
        sources.append(dict(entry=task['entry'],ref=task['ref'],sha256=sha,pixel_sha256=pixel_sha))
    ox,oy=view['native_origin'];left,top=x0+ox,y0+oy
    key=hashlib.sha256(f'{geometry}:native-picture-extent:{group}'.encode()).hexdigest()[:16]
    metadata=dict(room=room,group=group,geometry_key=geometry,key=key,
        bounds=[left,x1+ox,top,y1+oy],cells=1,positions=[[left,top]],native_extent=True,
        animation='original, unchanged',native_corner_references=sources,
        material='transparent compatibility marker',runtime_verified=False)
    return _store_backdrop(pack,metadata,Image.new('RGBA',(68,136),(0,0,0,0)))


def package(room, group, generated, center_path, pack, assets=ASSETS, margin=56):
    views, rooms = catalog(assets)
    view = views[room, group]
    x0,y0,x1,y1 = view['active_bbox']
    if x1-x0 >= field_view_width():
        raise ValueError('Native-wide views require camera review, not outpainting; use native extent export when needed')
    if view['controllers']:
        raise ValueError('Animated views require a reviewed source-driven phase extension; static export refused')
    entries=[e for e in rooms[room]['entries'] if e['group']==group and e['tiles']]
    if any(not (e['flags']&2) or (e['flags']&12) for e in entries):
        raise ValueError('Script-switched/scrolled layers require a phase-aware review; static export refused')
    center = Image.open(center_path).convert('RGB')
    if center.size != ((x1-x0)*4,(y1-y0)*4):
        raise ValueError('Center dimensions do not match the original active view at 4x')
    # Field rendering uses a 320x224 display, even on a taller scrolling canvas.
    # A smaller overscan is valid only when it still covers the entire 16:9 field.
    if not isinstance(margin, int) or margin <= 0 or margin > 2048 or (x1-x0)+2*margin < field_view_width():
        raise ValueError('Side margin does not cover the complete widescreen field')
    material, registration = register(center, Image.open(generated), margin*4)
    native_x,native_y = view['native_origin']
    left,right,top,bottom = x0+native_x-margin,x1+native_x+margin,y0+native_y,y1+native_y
    cells = []
    for y in range(top, bottom, 16):
        for x in list(range(left, x0+native_x, 16)) + list(range(x1+native_x, right, 16)):
            cells.append((x,y))
    cols = min(16,len(cells));rows = math.ceil(len(cells)/cols)
    atlas = Image.new('RGBA',(cols*68,rows*136))
    rgb = np.asarray(material.convert('RGB'))
    for i,(x,y) in enumerate(cells):
        # Filter gutters sample the same full RGB field as adjacent cells,
        # including the original center under transparent margin samples.
        nx=x+(np.arange(68)-2)/4;ny=y+(np.arange(68)-2)/4
        sx=(x-(x0+native_x))*4+margin*4+np.arange(68)-2
        sy=(y-top)*4+np.arange(68)-2
        color=rgb[np.clip(sy,0,rgb.shape[0]-1)[:,None],np.clip(sx,0,rgb.shape[1]-1)[None,:]]
        visible=((nx< x0+native_x)|(nx>=x1+native_x))&(nx>=left)&(nx<right)
        visible=(ny[:,None]>=top)&(ny[:,None]<bottom)&visible[None,:]
        # Alpha 1/255 is discarded by the margin shader but prevents the
        # loader from extruding unrelated edge RGB into transparent samples.
        padded=Image.fromarray(np.dstack((color,np.where(visible,255,1).astype(np.uint8))))
        atlas.paste(padded,((i%cols)*68,(i//cols)*68))
    geometry=rooms[room]['geometry_key']
    key=hashlib.sha256(f'{geometry}:{group}:widescreen-v1'.encode()).hexdigest()[:16]
    metadata = dict(room=room,group=group,key=key,geometry_key=geometry,
        bounds=[left,right,top,bottom],cells=len(cells),side_margin=margin,source=digest(Path(center_path)),
        generated=digest(Path(generated)),registration=registration,animation='static')
    metadata['positions']=cells
    _store_backdrop(pack,metadata,atlas)
    material.save(Path(generated).with_name(f'{room}-g{group:02d}-wide-registered.png'))
    Path(generated).with_name(f'{room}-g{group:02d}-registration.json').write_text(json.dumps(metadata,indent=2)+'\n')
    return metadata


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--room');p.add_argument('--group',type=int,default=0)
    p.add_argument('--native-extent',action='store_true',help='Declare an already wide native canvas after checking every serialized corner phase')
    p.add_argument('--margin',type=int,default=56,help='Side overscan in PS1 pixels; must cover the complete 16:9 field')
    p.add_argument('--audit',type=Path);p.add_argument('--assets-root',type=Path,default=ASSETS)
    p.add_argument('--published-pack',type=Path,help='Known shipped pack; working candidates alone are never called published')
    p.add_argument('--generated',type=Path);p.add_argument('--center',type=Path)
    p.add_argument('--pack',type=Path,required=True)
    a=p.parse_args()
    if a.native_extent and (a.audit or a.generated or a.center):
        p.error('--native-extent cannot be combined with audit or generated artwork')
    if a.audit:
        result=audit_catalog(a.assets_root.resolve(),a.pack.resolve(),a.audit,a.published_pack.resolve() if a.published_pack else None)
        print(json.dumps({k:result[k] for k in ('hd_views','retail_views','action_counts','shared_geometry_families','variant_binding_reviews','unique_binding_jobs','job_action_counts')},indent=2))
    elif a.native_extent:
        if not a.room:p.error('--room is required for native extent export')
        print(json.dumps(package_native_extent(a.room,a.group,a.pack,a.assets_root),indent=2))
    else:
        if not a.room or not a.generated or not a.center:p.error('--room, --generated and --center are required when packaging')
        print(json.dumps(package(a.room,a.group,a.generated,a.center,a.pack,a.assets_root,a.margin),indent=2))
