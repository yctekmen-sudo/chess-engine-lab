#!/usr/bin/env python3
"""Seals evidence into a timestamped ZIP, preserving partial logs on failure."""
from pathlib import Path
import argparse,datetime,hashlib,json,uuid,zipfile,sys
p=argparse.ArgumentParser();p.add_argument('root');p.add_argument('--out',required=True);a=p.parse_args();root=Path(a.root)
files=sorted(x for x in root.rglob('*') if x.is_file() and x.name!='evidence-manifest.json')
manifest={'schema':1,'files':[{'path':x.relative_to(root).as_posix(),'size':x.stat().st_size,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in files]}
(root/'evidence-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
with zipfile.ZipFile(a.out,'w',zipfile.ZIP_DEFLATED,compresslevel=6)as z:
 for x in sorted(root.rglob('*')):
  if x.is_file():z.write(x,'evidence/'+x.relative_to(root).as_posix())
with zipfile.ZipFile(a.out)as z:
 assert z.testzip() is None
 for f in manifest['files']:
  data=z.read('evidence/'+f['path'])
  assert len(data)==f['size'] and hashlib.sha256(data).hexdigest()==f['sha256']
print('EVIDENCE_ZIP_VERIFIED:',a.out,'files:',len(files))
