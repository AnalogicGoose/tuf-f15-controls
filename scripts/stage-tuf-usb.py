#!/usr/bin/env python3
"""Stage the opt-in TufWmi test only on the identified rescue USB; never mounts/writes internal EFI."""
import hashlib,json,os,plistlib,shutil,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent.parent
usb=Path('/Volumes/ESD-USB')
uuid='F6765325-775B-3954-A864-D3E975CA3CEB'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
info=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',str(usb)]))
assert info['VolumeUUID']==uuid and not info['Internal'],'Unexpected volume; no changes made'
cfg=usb/'efi/OC/config.plist'
backup=usb/'Support/TUF-Control-Test'
kext=usb/'efi/OC/Kexts/TufWmi.kext'
if '--restore' in sys.argv:
 assert (backup/'config.before.plist').is_file(),'No test backup'
 state=json.loads((backup/'state.json').read_text())
 assert digest(cfg)==state['testConfigSHA256'],'Config changed since staging; inspect manually'
 if kext.exists():
  assert digest(kext/'Contents/MacOS/TufWmi')==state['kextSHA256'],'Kext changed since staging'
  target=backup/'TufWmi-disabled.kext'
  assert not target.exists(),'Previous disabled artifact exists'
  shutil.move(str(kext),str(target))
 shutil.copyfile(backup/'config.before.plist',cfg)
 print('Original rescue config restored; test kext archived on USB.')
 sys.exit(0)
assert not kext.exists() and not backup.exists(),'Test files already exist; inspect before staging'
src=root/'build/TufWmi.kext'
want=digest(src/'Contents/MacOS/TufWmi')
assert Path('/private/tmp/tuf-preflight-sha256').read_text().strip()==want,'Matching kernel-collection preflight must pass first'
p=plistlib.loads(cfg.read_bytes())
guid='7C436110-AB2A-4BBB-A880-FE41995C9F82'
args=p['NVRAM']['Add'][guid]['boot-args'].split()
assert '-nvoff' in args,'Expected rescue bypass configuration'
assert not any(x['Enabled'] for x in p['Kernel']['Block'] if x['Identifier']=='com.apple.iokit.IONDRVSupport')
assert not any(x['BundlePath']=='TufWmi.kext' for x in p['Kernel']['Add'])
args=[x for x in args if not x.startswith('tufwmi=')]+['tufwmi=1']
p['NVRAM']['Add'][guid]['boot-args']=' '.join(args)
p['Kernel']['Add'].append({'Arch':'x86_64','BundlePath':'TufWmi.kext','Comment':'FX517ZM TUF Control opt-in test; no startup firmware writes','Enabled':True,'ExecutablePath':'Contents/MacOS/TufWmi','MaxKernel':'24.99.99','MinKernel':'24.0.0','PlistPath':'Contents/Info.plist'})
candidate=root/'build/config-tuf-usb-test.plist'
candidate.write_bytes(plistlib.dumps(p,sort_keys=False))
validator=Path('/Users/bbtov/Documents/Codex/2026-10-13/hey-x20/work/driver-setup/validator/Utilities/ocvalidate/ocvalidate')
subprocess.run([str(validator),str(candidate)],check=True)
backup.mkdir(parents=True)
shutil.copyfile(cfg,backup/'config.before.plist')
shutil.copytree(src,kext,copy_function=shutil.copyfile)
for f in src.rglob('*'):
 if f.is_file():assert digest(f)==digest(kext/f.relative_to(src)),str(f)
shutil.copyfile(candidate,backup/'config.test.plist')
state={'testConfigSHA256':digest(candidate),'kextSHA256':want,'volumeUUID':uuid}
(backup/'state.json').write_text(json.dumps(state,indent=2))
shutil.copyfile(candidate,cfg.with_suffix('.codex-new'))
os.replace(cfg.with_suffix('.codex-new'),cfg)
assert digest(cfg)==state['testConfigSHA256']
print('USB test staged and verified. Internal EFI untouched. Boot F8 -> UEFI Kingston -> Macintosh HD.')
