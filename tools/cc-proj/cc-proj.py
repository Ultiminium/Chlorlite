#!/usr/bin/env python3
"""
cc-proj — Chlorlite asset library tool
Manages a .ccproj project database (single file, all assets indexed).

Usage:
  cc-proj create  <project.ccproj> [name]           Create new project
  cc-proj info    <project.ccproj>                  Project stats
  cc-proj add     <project.ccproj> <file> [name]    Add asset from file
  cc-proj get     <project.ccproj> <name> [outpath] Extract asset
  cc-proj list    <project.ccproj> [type]           List all assets
  cc-proj remove  <project.ccproj> <name>           Remove asset
  cc-proj update  <project.ccproj> <name> <file>    Update existing asset
  cc-proj diff    <project.ccproj> <name>           Show change history
  cc-proj verify  <project.ccproj>                  Check integrity
  cc-proj pack    <project.ccproj>                  Defragment + optimize
  cc-proj export  <project.ccproj> <outdir>         Extract all assets

Asset types (auto-detected by extension):
  .ccmodel → model
  .ccanim  → animation
  .cctex   → texture
  .ccmat   → material
  .ccscene → scene
  .ccsnd   → sound
  .png/.jpg/.bmp → texture (raw)
  .obj/.gltf → model (imported)
  .wav/.ogg  → sound
"""

import sys
import os
import json
import struct
import hashlib
import time
import shutil
import zlib

CCPROJ_MAGIC = 0x4A4F5243   # "COJR" reversed = "CROJ" — "CCProj"
CCPROJ_VERSION = (1, 0)
MANIFEST_SENTINEL = b"CCPROJ_MANIFEST_END\x00"

# ── Detect asset type ──────────────────────────────────────────────────────

EXT_TO_TYPE = {
    '.ccmodel': 'model',   '.obj': 'model',   '.gltf': 'model',  '.glb': 'model',
    '.ccanim':  'animation',
    '.cctex':   'texture', '.png': 'texture', '.jpg': 'texture', '.bmp': 'texture',
    '.ccmat':   'material',
    '.ccscene': 'scene',
    '.ccsnd':   'sound',   '.wav': 'sound',   '.ogg': 'sound',   '.mp3': 'sound',
    '.ccscript':'script',
}

def detect_type(path):
    ext = os.path.splitext(path)[1].lower()
    return EXT_TO_TYPE.get(ext, 'raw')

# ── Checksum ───────────────────────────────────────────────────────────────

def checksum(data):
    return zlib.adler32(data) & 0xFFFFFFFF

# ── Project class ──────────────────────────────────────────────────────────

class CCProject:
    def __init__(self):
        self.name = "project"
        self.uuid = self._gen_uuid()
        self.created_at = int(time.time())
        self.assets = {}   # name → record dict
        self._data_blocks = {}  # name → bytes (compressed)

    def _gen_uuid(self):
        h = hashlib.md5(f"{time.time()}{os.getpid()}".encode()).hexdigest()
        return f"{h[:8]}-{h[8:12]}-4{h[13:16]}-{h[16:20]}-{h[20:32]}"

    @classmethod
    def load(cls, path):
        proj = cls()
        with open(path, 'rb') as f:
            magic = struct.unpack('<I', f.read(4))[0]
            if magic != CCPROJ_MAGIC:
                raise ValueError(f"Not a .ccproj file: {path}")
            vmaj, vmin = struct.unpack('<HH', f.read(4))
            manifest_offset = struct.unpack('<Q', f.read(8))[0]
            f.seek(manifest_offset)
            manifest_raw = b""
            while True:
                chunk = f.read(4096)
                if not chunk: break
                manifest_raw += chunk
                if MANIFEST_SENTINEL in manifest_raw:
                    manifest_raw = manifest_raw[:manifest_raw.index(MANIFEST_SENTINEL)]
                    break
            manifest = json.loads(manifest_raw.decode())
            proj.name = manifest.get('name', 'project')
            proj.uuid = manifest.get('uuid', proj.uuid)
            proj.created_at = manifest.get('created_at', 0)
            # Load asset records
            for name, rec in manifest.get('assets', {}).items():
                proj.assets[name] = rec
            # Load data blocks
            for name, rec in proj.assets.items():
                offset = rec.get('offset', 0)
                size = rec.get('size', 0)
                if offset and size:
                    f.seek(offset)
                    proj._data_blocks[name] = f.read(size)
        return proj

    def save(self, path):
        # Write header + data blocks, then manifest at end
        with open(path, 'wb') as f:
            f.write(struct.pack('<I', CCPROJ_MAGIC))
            f.write(struct.pack('<HH', *CCPROJ_VERSION))
            manifest_offset_pos = f.tell()
            f.write(struct.pack('<Q', 0))  # placeholder

            # Write data blocks
            for name, data in self._data_blocks.items():
                if name in self.assets:
                    self.assets[name]['offset'] = f.tell()
                    self.assets[name]['size'] = len(data)
                    f.write(data)
                    # 8-byte align
                    pad = (8 - f.tell() % 8) % 8
                    if pad: f.write(b'\x00' * pad)

            # Write manifest
            manifest_offset = f.tell()
            manifest = {
                'name': self.name,
                'uuid': self.uuid,
                'created_at': self.created_at,
                'saved_at': int(time.time()),
                'version': list(CCPROJ_VERSION),
                'assets': self.assets,
            }
            manifest_bytes = json.dumps(manifest, indent=2).encode()
            f.write(manifest_bytes)
            f.write(MANIFEST_SENTINEL)

            # Patch manifest offset
            f.seek(manifest_offset_pos)
            f.write(struct.pack('<Q', manifest_offset))

    def add(self, name, data, asset_type, author="claude", note=""):
        compressed = zlib.compress(data, level=6)
        record = {
            'name': name,
            'type': asset_type,
            'uuid': self._gen_uuid(),
            'size': len(compressed),
            'size_uncompressed': len(data),
            'checksum': checksum(data),
            'created_at': int(time.time()),
            'modified_at': int(time.time()),
            'author': author,
            'version': 1,
            'offset': 0,  # filled in on save
            'deps': [],
            'tags': [],
            'history': [{'version':1,'timestamp':int(time.time()),'author':author,'note':note}]
        }
        if name in self.assets:
            # Update — increment version
            record['version'] = self.assets[name].get('version', 1) + 1
            record['created_at'] = self.assets[name].get('created_at', record['created_at'])
            history = self.assets[name].get('history', [])
            history.append({'version':record['version'],'timestamp':int(time.time()),'author':author,'note':note})
            record['history'] = history[-CCPROJ_VERSION[0]*50:]  # keep last 50 versions
        self.assets[name] = record
        self._data_blocks[name] = compressed

    def get(self, name_or_uuid):
        name = self._resolve(name_or_uuid)
        if not name or name not in self._data_blocks:
            return None, None
        compressed = self._data_blocks[name]
        data = zlib.decompress(compressed)
        return data, self.assets[name]

    def remove(self, name_or_uuid):
        name = self._resolve(name_or_uuid)
        if not name: return False
        del self.assets[name]
        self._data_blocks.pop(name, None)
        return True

    def _resolve(self, name_or_uuid):
        if name_or_uuid in self.assets:
            return name_or_uuid
        for n, rec in self.assets.items():
            if rec.get('uuid') == name_or_uuid:
                return n
        return None

    def stats(self):
        total_compressed = sum(r.get('size',0) for r in self.assets.values())
        total_raw = sum(r.get('size_uncompressed',0) for r in self.assets.values())
        by_type = {}
        for r in self.assets.values():
            t = r.get('type','raw')
            by_type[t] = by_type.get(t, 0) + 1
        return {
            'name': self.name, 'uuid': self.uuid,
            'asset_count': len(self.assets),
            'total_compressed_bytes': total_compressed,
            'total_raw_bytes': total_raw,
            'compression_ratio': round(total_raw/max(total_compressed,1),2),
            'by_type': by_type,
        }

# ── Commands ───────────────────────────────────────────────────────────────

def cmd_create(args):
    path = args[0] if args else 'project.ccproj'
    name = args[1] if len(args)>1 else os.path.splitext(os.path.basename(path))[0]
    proj = CCProject(); proj.name = name
    proj.save(path)
    print(f"Created: {path}")
    print(f"  Name: {name}")
    print(f"  UUID: {proj.uuid}")

def cmd_info(args):
    path = args[0] if args else 'project.ccproj'
    proj = CCProject.load(path)
    s = proj.stats()
    print(json.dumps(s, indent=2))

def cmd_add(args):
    if len(args) < 2:
        print("Usage: cc-proj add <project.ccproj> <file> [name] [author]"); return
    path, file = args[0], args[1]
    name = args[2] if len(args)>2 else os.path.splitext(os.path.basename(file))[0]
    author = args[3] if len(args)>3 else 'claude'
    asset_type = detect_type(file)
    with open(file,'rb') as f: data = f.read()
    proj = CCProject.load(path)
    proj.add(name, data, asset_type, author)
    proj.save(path)
    rec = proj.assets[name]
    print(f"Added '{name}' ({asset_type})")
    print(f"  Size: {len(data):,} bytes → {rec['size']:,} compressed ({rec['size_uncompressed']//max(rec['size'],1)}x ratio)")
    print(f"  UUID: {rec['uuid']}")
    print(f"  Total assets: {len(proj.assets)}")

def cmd_add_raw(proj_path, name, data, asset_type='raw', author='claude', note=''):
    """Programmatic add — for use by other tools."""
    proj = CCProject.load(proj_path) if os.path.exists(proj_path) else CCProject()
    proj.add(name, data, asset_type, author, note)
    proj.save(proj_path)
    return proj.assets[name]

def cmd_get(args):
    if len(args) < 2:
        print("Usage: cc-proj get <project.ccproj> <name> [outpath]"); return
    path, name = args[0], args[1]
    out = args[2] if len(args)>2 else None
    proj = CCProject.load(path)
    data, rec = proj.get(name)
    if data is None:
        print(f"Asset '{name}' not found"); return
    if not out:
        ext_map = {'model':'.ccmodel','animation':'.ccanim','texture':'.cctex',
                   'material':'.ccmat','scene':'.ccscene','sound':'.ccsnd','script':'.ccscript'}
        out = name + ext_map.get(rec.get('type','raw'), '.bin')
    with open(out,'wb') as f: f.write(data)
    print(f"Extracted '{name}' → {out} ({len(data):,} bytes)")

def cmd_list(args):
    if not args:
        print("Usage: cc-proj list <project.ccproj> [type]"); return
    path = args[0]
    type_filter = args[1] if len(args)>1 else None
    proj = CCProject.load(path)
    assets = [(n,r) for n,r in proj.assets.items()
              if not type_filter or r.get('type')==type_filter]
    if not assets:
        print("No assets" + (f" of type '{type_filter}'" if type_filter else "")); return
    print(f"{'NAME':<32} {'TYPE':<12} {'SIZE':>10} {'VER':>4}  UUID")
    print("─"*80)
    for name, rec in sorted(assets, key=lambda x: x[1].get('type','')):
        sz = rec.get('size_uncompressed',0)
        sz_str = f"{sz//1024}K" if sz>1024 else f"{sz}B"
        print(f"{name:<32} {rec.get('type','raw'):<12} {sz_str:>10} {rec.get('version',1):>4}  {rec.get('uuid','')[:18]}")

def cmd_remove(args):
    if len(args) < 2:
        print("Usage: cc-proj remove <project.ccproj> <name>"); return
    path, name = args[0], args[1]
    proj = CCProject.load(path)
    if proj.remove(name):
        proj.save(path)
        print(f"Removed '{name}' from {path}")
    else:
        print(f"Asset '{name}' not found")

def cmd_update(args):
    if len(args) < 3:
        print("Usage: cc-proj update <project.ccproj> <name> <file>"); return
    path, name, file = args[0], args[1], args[2]
    with open(file,'rb') as f: data=f.read()
    asset_type = detect_type(file)
    proj = CCProject.load(path)
    old_ver = proj.assets.get(name,{}).get('version',0)
    proj.add(name, data, asset_type, note=f"Updated from {file}")
    proj.save(path)
    new_ver = proj.assets[name]['version']
    print(f"Updated '{name}': v{old_ver} → v{new_ver}")

def cmd_verify(args):
    if not args:
        print("Usage: cc-proj verify <project.ccproj>"); return
    path = args[0]
    proj = CCProject.load(path)
    errors = 0
    for name, rec in proj.assets.items():
        data, _ = proj.get(name)
        if data is None:
            print(f"  MISSING: {name}"); errors+=1; continue
        cs = checksum(data)
        if cs != rec.get('checksum', cs):
            print(f"  CORRUPT: {name} (checksum mismatch)"); errors+=1
        else:
            print(f"  OK: {name} ({rec.get('type','?')}, {len(data):,} bytes)")
    print(f"\n{'OK' if not errors else 'ERRORS'}: {len(proj.assets)} assets, {errors} errors")

def cmd_export(args):
    if len(args) < 2:
        print("Usage: cc-proj export <project.ccproj> <outdir>"); return
    path, outdir = args[0], args[1]
    os.makedirs(outdir, exist_ok=True)
    proj = CCProject.load(path)
    ext_map = {'model':'.ccmodel','animation':'.ccanim','texture':'.cctex',
               'material':'.ccmat','scene':'.ccscene','sound':'.ccsnd','script':'.ccscript'}
    for name, rec in proj.assets.items():
        data, _ = proj.get(name)
        if data is None: continue
        ext = ext_map.get(rec.get('type','raw'),'.bin')
        out = os.path.join(outdir, name+ext)
        with open(out,'wb') as f: f.write(data)
        print(f"  {name} → {out}")
    print(f"\nExported {len(proj.assets)} assets to {outdir}")

def cmd_diff(args):
    if len(args) < 2:
        print("Usage: cc-proj diff <project.ccproj> <name>"); return
    path, name = args[0], args[1]
    proj = CCProject.load(path)
    rec = proj.assets.get(name)
    if not rec: print(f"Asset '{name}' not found"); return
    print(f"History for '{name}':")
    for h in rec.get('history',[]):
        ts = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(h.get('timestamp',0)))
        print(f"  v{h['version']}  {ts}  author={h['author']}  note={h.get('note','')}")

def cmd_pack(args):
    if not args: print("Usage: cc-proj pack <project.ccproj>"); return
    path = args[0]
    proj = CCProject.load(path)
    # Re-compress at highest level
    for name in proj._data_blocks:
        data, _ = proj.get(name)
        if data: proj._data_blocks[name] = zlib.compress(data, level=9)
    proj.save(path)
    s = proj.stats()
    print(f"Packed: {s['total_compressed_bytes']:,} bytes, ratio={s['compression_ratio']}x")

# ── Main ───────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(__doc__); return
    cmd = sys.argv[1]
    args = sys.argv[2:]
    dispatch = {
        'create': cmd_create, 'info': cmd_info, 'add': cmd_add,
        'get': cmd_get, 'list': cmd_list, 'remove': cmd_remove,
        'update': cmd_update, 'verify': cmd_verify, 'export': cmd_export,
        'diff': cmd_diff, 'pack': cmd_pack,
    }
    fn = dispatch.get(cmd)
    if fn: fn(args)
    else: print(f"Unknown command: {cmd}\n"); print(__doc__)

if __name__ == '__main__':
    main()
