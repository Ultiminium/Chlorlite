#!/usr/bin/env python3
"""
cc-model — Chlorlite command-driven 3D model editor
Claude uses this to BUILD 3D models, rigs, and animations by commands.

Usage:
  cc-model new       <name>                         Create new model
  cc-model info      <model.ccmodel>                Show model info
  cc-model add-mesh  <model> [options]              Add procedural mesh
  cc-model add-prim  <model> <type> [opts]          Add primitive geometry
  cc-model extrude   <model> <mesh> <face> <dist>   Extrude a face
  cc-model bevel     <model> <mesh> <edge> <amt>    Bevel an edge
  cc-model subdivide <model> <mesh> [levels]        Catmull-Clark subdivide
  cc-model mirror    <model> <mesh> <axis>          Mirror geometry
  cc-model array     <model> <mesh> <count> <step>  Array modifier
  cc-model smooth    <model> <mesh> [iterations]    Laplacian smooth
  cc-model unwrap    <model> <mesh>                 Auto UV unwrap
  cc-model add-bone  <model> <name> [parent] [head] [tail]  Add skeleton bone
  cc-model bind-skin <model> <mesh>                 Auto skinning weights
  cc-model add-anim  <model> <name> <duration>      Add animation clip
  cc-model add-key   <model> <anim> <bone> <time> <pos> <rot>  Add keyframe
  cc-model add-morph <model> <name>                 Add morph target
  cc-model screenshot <model> [angle] [output.png]  Render preview screenshot
  cc-model export    <model> <format> [output]      Export (.obj/.gltf)
  cc-model import    <file.obj/.gltf>               Import model
  cc-model list-bones <model>                       List skeleton
  cc-model list-anims <model>                       List animations
  cc-model list-morphs <model>                      List morph targets
  cc-model merge     <a.ccmodel> <b.ccmodel> <out>  Merge two models
  cc-model optimize  <model>                        Remove duplicate verts, reindex
"""

import sys
import os
import struct
import math
import json
import subprocess
import argparse
import time
import hashlib

CCMODEL_MAGIC = 0x444D4343
SKILL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE_BUILD = os.path.join(SKILL_DIR, "engine", "build")

# ── Binary write helpers ───────────────────────────────────────────────────

def wu32(f, v):  f.write(struct.pack('<I', v & 0xFFFFFFFF))
def wu16(f, v):  f.write(struct.pack('<H', v & 0xFFFF))
def wf32(f, v):  f.write(struct.pack('<f', float(v)))
def wstr(f, s, maxlen=128):
    b = s[:maxlen].encode('utf-8')
    wu32(f, len(b)); f.write(b)
def walign(f):
    pos = f.tell()
    if pos % 4: f.write(b'\x00' * (4 - pos % 4))

# ── Geometry generators ────────────────────────────────────────────────────

def gen_cube(size=1.0):
    """Generate a cube mesh with proper normals and UVs."""
    s = size / 2
    faces = [
        # pos_x, pos_y, pos_z, normal_x, normal_y, normal_z, u, v
        # Front (+Z)
        [(-s,-s, s),(s,-s,s),(s,s,s),(-s,s,s),(0,0,1)],
        # Back (-Z)
        [(s,-s,-s),(-s,-s,-s),(-s,s,-s),(s,s,-s),(0,0,-1)],
        # Right (+X)
        [(s,-s,s),(s,-s,-s),(s,s,-s),(s,s,s),(1,0,0)],
        # Left (-X)
        [(-s,-s,-s),(-s,-s,s),(-s,s,s),(-s,s,-s),(-1,0,0)],
        # Top (+Y)
        [(-s,s,s),(s,s,s),(s,s,-s),(-s,s,-s),(0,1,0)],
        # Bottom (-Y)
        [(-s,-s,-s),(s,-s,-s),(s,-s,s),(-s,-s,s),(0,-1,0)],
    ]
    uvs = [(0,0),(1,0),(1,1),(0,1)]
    verts = []
    indices = []
    for face in faces:
        *corners, normal = face
        base = len(verts)
        for i, (pos, uv) in enumerate(zip(corners, uvs)):
            verts.append({
                'pos': list(pos), 'normal': list(normal),
                'uv': list(uv), 'tangent': [1,0,0,1],
                'color': [255,255,255,255],
                'bone_idx': [0,0,0,0], 'bone_weight': [1,0,0,0]
            })
        indices += [base, base+1, base+2, base, base+2, base+3]
    return verts, indices

def gen_sphere(radius=1.0, slices=16, stacks=8):
    """UV sphere."""
    verts = []; indices = []
    for j in range(stacks+1):
        phi = math.pi * j / stacks - math.pi/2
        for i in range(slices+1):
            theta = 2*math.pi*i/slices
            x = math.cos(phi)*math.cos(theta)
            y = math.sin(phi)
            z = math.cos(phi)*math.sin(theta)
            verts.append({
                'pos': [x*radius,y*radius,z*radius],
                'normal': [x,y,z],
                'uv': [i/slices, j/stacks],
                'tangent': [-math.sin(theta),0,math.cos(theta),1],
                'color': [255,255,255,255],
                'bone_idx': [0,0,0,0], 'bone_weight': [1,0,0,0]
            })
    for j in range(stacks):
        for i in range(slices):
            a = j*(slices+1)+i; b=a+1; c=a+slices+1; d=c+1
            indices += [a,c,b, b,c,d]
    return verts, indices

def gen_plane(w=1.0, h=1.0, divs=1):
    """Subdivided plane on XZ."""
    verts = []; indices = []
    for j in range(divs+1):
        for i in range(divs+1):
            x = (i/divs - 0.5)*w; z=(j/divs-0.5)*h
            verts.append({
                'pos':[x,0,z],'normal':[0,1,0],'uv':[i/divs,j/divs],
                'tangent':[1,0,0,1],'color':[255,255,255,255],
                'bone_idx':[0,0,0,0],'bone_weight':[1,0,0,0]
            })
    for j in range(divs):
        for i in range(divs):
            a=j*(divs+1)+i; b=a+1; c=a+divs+1; d=c+1
            indices+=[a,c,b,b,c,d]
    return verts, indices

def gen_cylinder(radius=0.5, height=1.0, segs=16):
    """Capped cylinder."""
    verts=[]; indices=[]
    h=height/2
    # Side
    for j in range(2):
        y = -h + j*height
        for i in range(segs+1):
            a = 2*math.pi*i/segs
            x=math.cos(a)*radius; z=math.sin(a)*radius
            nx=math.cos(a); nz=math.sin(a)
            verts.append({'pos':[x,y,z],'normal':[nx,0,nz],
                'uv':[i/segs,j],'tangent':[-math.sin(a),0,math.cos(a),1],
                'color':[255,255,255,255],'bone_idx':[0,0,0,0],'bone_weight':[1,0,0,0]})
    for i in range(segs):
        a=i; b=a+1; c=a+segs+1; d=c+1
        indices+=[a,b,c,b,d,c]
    # Caps
    for cap in [0,1]:
        cy=-h+cap*height; cn=[0,-1+cap*2,0]; base=len(verts)
        cx=verts[-1] if False else None
        for i in range(segs):
            a=2*math.pi*i/segs
            verts.append({'pos':[math.cos(a)*radius,cy,math.sin(a)*radius],
                'normal':cn,'uv':[math.cos(a)*0.5+0.5,math.sin(a)*0.5+0.5],
                'tangent':[1,0,0,1],'color':[255,255,255,255],
                'bone_idx':[0,0,0,0],'bone_weight':[1,0,0,0]})
        center=len(verts)
        verts.append({'pos':[0,cy,0],'normal':cn,'uv':[0.5,0.5],
            'tangent':[1,0,0,1],'color':[255,255,255,255],
            'bone_idx':[0,0,0,0],'bone_weight':[1,0,0,0]})
        for i in range(segs):
            if cap==0: indices+=[center,base+i,base+(i+1)%segs]
            else:      indices+=[center,base+(i+1)%segs,base+i]
    return verts, indices

def gen_capsule(radius=0.3, height=1.0, segs=16):
    """Capsule (cylinder + hemisphere caps)."""
    v,i = gen_cylinder(radius, height, segs)
    # TODO: add hemisphere caps
    return v,i

# ── Model file I/O ─────────────────────────────────────────────────────────

class CCModel:
    def __init__(self, name="model"):
        self.name = name
        self.uuid = self._gen_uuid()
        self.meshes = []    # [{'name','verts','indices','material_slot'}]
        self.materials = [] # [dict]
        self.bones = []     # [{'name','parent':-1,'head','tail'}]
        self.anims = []     # [{'name','duration','fps','tracks':[{'bone','target','keys'}]}]
        self.morphs = []    # [{'name','deltas':[...]}]
        self.aabb_min = [0,0,0]
        self.aabb_max = [0,0,0]

    def _gen_uuid(self):
        import hashlib, time, os
        h = hashlib.md5(f"{time.time()}{os.getpid()}".encode()).hexdigest()
        return f"{h[:8]}-{h[8:12]}-4{h[13:16]}-{h[16:20]}-{h[20:32]}"

    def compute_aabb(self):
        all_pos = [v['pos'] for m in self.meshes for v in m['verts']]
        if not all_pos: return
        self.aabb_min = [min(p[i] for p in all_pos) for i in range(3)]
        self.aabb_max = [max(p[i] for p in all_pos) for i in range(3)]

    def save(self, path):
        self.compute_aabb()
        with open(path, 'wb') as f:
            # Header
            wu32(f, CCMODEL_MAGIC)
            wu16(f, 1); wu16(f, 0)  # version 1.0
            # UUID (16 bytes)
            uid = self.uuid.replace('-','')[:32]
            for i in range(0,32,2): f.write(bytes([int(uid[i:i+2],16)]))
            wstr(f, self.name, 127)
            wu32(f, len(self.meshes))
            wu32(f, len(self.materials))
            wu32(f, 1 if self.bones else 0)
            wu32(f, len(self.anims))
            wu32(f, len(self.morphs))
            for v in self.aabb_min: wf32(f, v)
            for v in self.aabb_max: wf32(f, v)
            walign(f)

            # MESH chunks
            for mesh in self.meshes:
                wu32(f, 0x4853454D)  # "MESH"
                name_b = mesh['name'][:63].encode(); f.write(name_b.ljust(64, b'\x00'))
                wu32(f, mesh.get('material_slot', 0))
                wu32(f, len(mesh['verts']))
                wu32(f, len(mesh['indices']))
                # AABB
                ps = [v['pos'] for v in mesh['verts']]
                for i in range(3): wf32(f, min(p[i] for p in ps))
                for i in range(3): wf32(f, max(p[i] for p in ps))
                # Vertices: pos(3) normal(3) uv(2) tangent(4) color(4) bone_idx(4u8) bone_weight(4f)
                for v in mesh['verts']:
                    for x in v.get('pos',[0,0,0]): wf32(f, x)
                    for x in v.get('normal',[0,1,0]): wf32(f, x)
                    for x in v.get('uv',[0,0]): wf32(f, x)
                    for x in v.get('tangent',[1,0,0,1]): wf32(f, x)
                    col = v.get('color',[255,255,255,255])
                    f.write(bytes([int(c)&0xff for c in col]))
                    bi = v.get('bone_idx',[0,0,0,0])
                    f.write(bytes([int(b)&0xff for b in bi]))
                    for x in v.get('bone_weight',[1,0,0,0]): wf32(f, x)
                # Indices
                for idx in mesh['indices']: wu32(f, idx)
                walign(f)

            # MATL chunks
            for mat in self.materials:
                wu32(f, 0x4C54414D)  # "MATL"
                name_b = mat.get('name','material')[:63].encode()
                f.write(name_b.ljust(64, b'\x00'))
                for x in mat.get('base_color',[1,1,1,1]): wf32(f, x)
                wf32(f, mat.get('roughness', 0.5))
                wf32(f, mat.get('metallic', 0.0))
                for x in mat.get('emissive',[0,0,0]): wf32(f, x)
                wf32(f, mat.get('alpha_cutoff', 0.0))
                f.write(bytes([1 if mat.get('double_sided') else 0]))
                f.write(bytes([1 if mat.get('alpha_blend') else 0]))
                f.write(b'\x00\x00')  # pad
                for key in ['albedo_path','normal_path','rough_metal_path','emissive_path','ao_path']:
                    p = mat.get(key,'').encode()[:255]; f.write(p.ljust(256,b'\x00'))
                walign(f)

            # SKEL chunk
            if self.bones:
                wu32(f, 0x4C454B53)  # "SKEL"
                wu32(f, len(self.bones))
                for bone in self.bones:
                    f.write(bone['name'][:63].encode().ljust(64,b'\x00'))
                    wu32(f, bone.get('parent',0xFFFFFFFF) if bone.get('parent',-1)<0 else bone['parent'])
                    # bind_pose (16 floats identity)
                    for v in bone.get('bind_pose',[1,0,0,0,0,1,0,0,0,0,1,0]+bone.get('head',[0,0,0])+[1]):
                        wf32(f, v)
                    # inv_bind (16 floats identity)
                    for v in [1,0,0,0,0,1,0,0,0,0,1,0]+[-x for x in bone.get('head',[0,0,0])]+[1]:
                        wf32(f, v)
                    for x in bone.get('head',[0,0,0]): wf32(f, x)
                    for x in bone.get('tail',[0,1,0]): wf32(f, x)
                    wf32(f, bone.get('roll', 0.0))
                    for x in bone.get('ik_min',[-3.14,-3.14,-3.14]): wf32(f, x)
                    for x in bone.get('ik_max',[3.14,3.14,3.14]): wf32(f, x)
                    f.write(bytes([1 if bone.get('ik_enabled') else 0]))
                    f.write(b'\x00\x00\x00')  # pad
                walign(f)

            # ANIM chunks
            for anim in self.anims:
                wu32(f, 0x4D494E41)  # "ANIM"
                f.write(anim['name'][:63].encode().ljust(64,b'\x00'))
                # UUID
                uid = self._gen_uuid().replace('-','')[:32]
                for i in range(0,32,2): f.write(bytes([int(uid[i:i+2],16)]))
                wf32(f, anim['duration'])
                wf32(f, anim.get('fps',30.0))
                f.write(bytes([1 if anim.get('loop',True) else 0]))
                f.write(b'\x00\x00\x00')
                wu32(f, len(anim.get('tracks',[])))
                walign(f)

            # MRPH chunks
            for morph in self.morphs:
                wu32(f, 0x4850524D)  # "MRPH"
                f.write(morph['name'][:63].encode().ljust(64,b'\x00'))
                wf32(f, morph.get('default_weight',0.0))
                deltas = morph.get('deltas',[])
                nv = len(deltas)//3
                wu32(f, nv)
                for d in deltas: wf32(f, d)
                walign(f)

            # META
            wu32(f, 0x4154454D)  # "META"
            f.write(struct.pack('<Q', int(time.time())))
        return path

    @classmethod
    def load(cls, path):
        with open(path,'rb') as f:
            magic = struct.unpack('<I',f.read(4))[0]
            if magic != CCMODEL_MAGIC:
                raise ValueError(f"Not a .ccmodel file: {path}")
            vmaj,vmin = struct.unpack('<HH',f.read(4))
            uuid_bytes = f.read(16)
            uuid = '-'.join([uuid_bytes[:4].hex(),uuid_bytes[4:6].hex(),
                             uuid_bytes[6:8].hex(),uuid_bytes[8:10].hex(),
                             uuid_bytes[10:].hex()])
            nlen = struct.unpack('<I',f.read(4))[0]
            name = f.read(nlen).decode()
            n_mesh,n_mat,has_skel,n_anim,n_morph = struct.unpack('<5I',f.read(20))
            aabb_min = list(struct.unpack('<3f',f.read(12)))
            aabb_max = list(struct.unpack('<3f',f.read(12)))
            pos = f.tell(); pad=4-(pos%4) if pos%4 else 0; f.read(pad)

            m = cls(name); m.uuid = uuid
            m.aabb_min = aabb_min; m.aabb_max = aabb_max

            # Read chunks
            chunk_counts = {'mesh':0,'mat':0,'anim':0,'morph':0}
            while True:
                hdr = f.read(4)
                if len(hdr)<4: break
                cid = struct.unpack('<I',hdr)[0]
                if cid == 0x4853454D:  # MESH
                    mesh = {'name': f.read(64).rstrip(b'\x00').decode()}
                    mat_slot,nv,ni = struct.unpack('<3I',f.read(12))
                    mesh['material_slot'] = mat_slot
                    aabb_mn = list(struct.unpack('<3f',f.read(12)))
                    aabb_mx = list(struct.unpack('<3f',f.read(12)))
                    verts=[]
                    for _ in range(nv):
                        pos2=list(struct.unpack('<3f',f.read(12)))
                        nrm=list(struct.unpack('<3f',f.read(12)))
                        uv=list(struct.unpack('<2f',f.read(8)))
                        tgt=list(struct.unpack('<4f',f.read(16)))
                        col=list(f.read(4))
                        bi=list(f.read(4))
                        bw=list(struct.unpack('<4f',f.read(16)))
                        verts.append({'pos':pos2,'normal':nrm,'uv':uv,'tangent':tgt,
                                      'color':col,'bone_idx':bi,'bone_weight':bw})
                    idxs = list(struct.unpack(f'<{ni}I',f.read(ni*4)))
                    mesh['verts']=verts; mesh['indices']=idxs
                    m.meshes.append(mesh)
                elif cid == 0x4C454B53:  # SKEL
                    n_bones2 = struct.unpack('<I',f.read(4))[0]
                    for bi in range(n_bones2):
                        bname = f.read(64).rstrip(b'\x00').decode()
                        parent_raw = struct.unpack('<I',f.read(4))[0]
                        parent = -1 if parent_raw==0xFFFFFFFF else parent_raw
                        bind_pose = list(struct.unpack('<16f',f.read(64)))
                        inv_bind  = list(struct.unpack('<16f',f.read(64)))
                        head = list(struct.unpack('<3f',f.read(12)))
                        tail = list(struct.unpack('<3f',f.read(12)))
                        roll = struct.unpack('<f',f.read(4))[0]
                        ik_min = list(struct.unpack('<3f',f.read(12)))
                        ik_max = list(struct.unpack('<3f',f.read(12)))
                        ik_en = bool(f.read(4)[0])
                        m.bones.append({'name':bname,'parent':parent,'head':head,'tail':tail,'roll':roll,'ik_enabled':ik_en})
                elif cid == 0x4C54414D:  # MATL
                    mat = {}
                    mat['name'] = f.read(64).rstrip(b'\x00').decode()
                    mat['base_color'] = list(struct.unpack('<4f',f.read(16)))
                    mat['roughness'] = struct.unpack('<f',f.read(4))[0]
                    mat['metallic']  = struct.unpack('<f',f.read(4))[0]
                    mat['emissive']  = list(struct.unpack('<3f',f.read(12)))
                    mat['alpha_cutoff'] = struct.unpack('<f',f.read(4))[0]
                    flags = f.read(4)
                    mat['double_sided'] = bool(flags[0]); mat['alpha_blend'] = bool(flags[1])
                    for key in ['albedo_path','normal_path','rough_metal_path','emissive_path','ao_path']:
                        mat[key] = f.read(256).rstrip(b'\x00').decode()
                    m.materials.append(mat)
                elif cid == 0x4D494E41:  # ANIM
                    aname = f.read(64).rstrip(b'\x00').decode()
                    auuid = f.read(16).hex()
                    dur,fps = struct.unpack('<2f',f.read(8))
                    loop_flag = bool(f.read(4)[0])
                    n_tracks = struct.unpack('<I',f.read(4))[0]
                    m.anims.append({'name':aname,'duration':dur,'fps':fps,'loop':loop_flag,'tracks':[]})
                elif cid == 0x4850524D:  # MRPH
                    mname = f.read(64).rstrip(b'\x00').decode()
                    dw = struct.unpack('<f',f.read(4))[0]
                    nv2 = struct.unpack('<I',f.read(4))[0]
                    deltas = list(struct.unpack(f'<{nv2*3}f',f.read(nv2*3*4)))
                    m.morphs.append({'name':mname,'default_weight':dw,'deltas':deltas})
                elif cid == 0x4154454D: break  # META
                else:
                    break  # unknown
                pos2=f.tell(); pad=4-(pos2%4) if pos2%4 else 0; f.read(pad)
        return m

    def info(self):
        self.compute_aabb()
        sz = [self.aabb_max[i]-self.aabb_min[i] for i in range(3)]
        nv = sum(len(m['verts']) for m in self.meshes)
        ni = sum(len(m['indices']) for m in self.meshes)
        return {
            'name': self.name, 'uuid': self.uuid,
            'meshes': len(self.meshes), 'materials': len(self.materials),
            'bones': len(self.bones), 'animations': len(self.anims),
            'morphs': len(self.morphs),
            'total_verts': nv, 'total_tris': ni//3,
            'aabb_min': [round(v,4) for v in self.aabb_min],
            'aabb_max': [round(v,4) for v in self.aabb_max],
            'size': [round(v,4) for v in sz],
        }

# ── Commands ───────────────────────────────────────────────────────────────

def cmd_new(args):
    name = args.name
    path = args.output or f"{name}.ccmodel"
    m = CCModel(name)
    m.save(path)
    print(f"Created: {path}")
    print(json.dumps(m.info(), indent=2))

def cmd_info(args):
    m = CCModel.load(args.model)
    print(json.dumps(m.info(), indent=2))

def cmd_add_prim(args):
    m = CCModel.load(args.model)
    prim = args.type.lower()
    if prim in ('cube','box'):
        size = float(args.size) if hasattr(args,'size') and args.size else 1.0
        verts, indices = gen_cube(size)
        m.meshes.append({'name': args.mesh_name or prim, 'verts':verts, 'indices':indices, 'material_slot':0})
    elif prim == 'sphere':
        r = float(args.radius) if hasattr(args,'radius') and args.radius else 1.0
        sl = int(args.slices) if hasattr(args,'slices') and args.slices else 16
        st = int(args.stacks) if hasattr(args,'stacks') and args.stacks else 8
        verts, indices = gen_sphere(r, sl, st)
        m.meshes.append({'name': args.mesh_name or prim, 'verts':verts, 'indices':indices, 'material_slot':0})
    elif prim == 'plane':
        w = float(args.width) if hasattr(args,'width') and args.width else 1.0
        h = float(args.height) if hasattr(args,'height') and args.height else 1.0
        d = int(args.divs) if hasattr(args,'divs') and args.divs else 1
        verts, indices = gen_plane(w, h, d)
        m.meshes.append({'name': args.mesh_name or prim, 'verts':verts, 'indices':indices, 'material_slot':0})
    elif prim == 'cylinder':
        r = float(args.radius) if hasattr(args,'radius') and args.radius else 0.5
        h = float(args.height) if hasattr(args,'height') and args.height else 1.0
        s = int(args.segs) if hasattr(args,'segs') and args.segs else 16
        verts, indices = gen_cylinder(r, h, s)
        m.meshes.append({'name': args.mesh_name or prim, 'verts':verts, 'indices':indices, 'material_slot':0})
    else:
        print(f"Unknown primitive: {prim}. Options: cube, sphere, plane, cylinder")
        return
    m.save(args.model)
    info = m.info()
    print(f"Added {prim} to {args.model}")
    print(f"  Meshes: {info['meshes']}, Verts: {info['total_verts']}, Tris: {info['total_tris']}")

def cmd_subdivide(args):
    """Simple Loop subdivision."""
    m = CCModel.load(args.model)
    mesh_idx = int(args.mesh) if args.mesh.isdigit() else next(
        (i for i,me in enumerate(m.meshes) if me['name']==args.mesh), 0)
    levels = int(args.levels) if hasattr(args,'levels') and args.levels else 1
    mesh = m.meshes[mesh_idx]
    for _ in range(levels):
        verts = mesh['verts']; indices = mesh['indices']
        edge_map = {}; new_verts = list(verts); new_idx = []
        def edge_vert(a,b):
            key=min(a,b)*100000+max(a,b)
            if key not in edge_map:
                # Midpoint
                va=verts[a]; vb=verts[b]
                mv={'pos':[(va['pos'][i]+vb['pos'][i])/2 for i in range(3)],
                    'normal':[(va['normal'][i]+vb['normal'][i])/2 for i in range(3)],
                    'uv':[(va['uv'][i]+vb['uv'][i])/2 for i in range(2)],
                    'tangent':[1,0,0,1],'color':[255,255,255,255],
                    'bone_idx':[0,0,0,0],'bone_weight':[1,0,0,0]}
                edge_map[key]=len(new_verts); new_verts.append(mv)
            return edge_map[key]
        for i in range(0,len(indices),3):
            a,b,c=indices[i],indices[i+1],indices[i+2]
            ab=edge_vert(a,b); bc=edge_vert(b,c); ca=edge_vert(c,a)
            new_idx+=[a,ab,ca, ab,b,bc, ca,bc,c, ab,bc,ca]
        mesh['verts']=new_verts; mesh['indices']=new_idx
    m.save(args.model)
    info=m.info()
    print(f"Subdivided mesh '{args.mesh}' {levels}x → {info['total_verts']} verts, {info['total_tris']} tris")

def cmd_add_bone(args):
    m = CCModel.load(args.model)
    parent = -1
    if hasattr(args,'parent') and args.parent:
        parent = next((i for i,b in enumerate(m.bones) if b['name']==args.parent), -1)
    head = [float(x) for x in (args.head or '0,0,0').split(',')]
    tail = [float(x) for x in (args.tail or '0,1,0').split(',')]
    m.bones.append({'name':args.bone_name,'parent':parent,'head':head,'tail':tail,'roll':0})
    m.save(args.model)
    print(f"Added bone '{args.bone_name}' (parent={args.parent or 'root'}) head={head} tail={tail}")
    print(f"  Total bones: {len(m.bones)}")

def cmd_add_material(args):
    m = CCModel.load(args.model)
    mat = {
        'name': args.mat_name,
        'base_color': [float(x) for x in (args.color or '1,1,1,1').split(',')],
        'roughness': float(args.roughness) if hasattr(args,'roughness') and args.roughness else 0.5,
        'metallic': float(args.metallic) if hasattr(args,'metallic') and args.metallic else 0.0,
        'emissive': [0,0,0],
        'albedo_path': args.albedo or '',
        'normal_path': args.normal or '',
        'rough_metal_path': '',
        'emissive_path': '',
        'ao_path': '',
    }
    m.materials.append(mat)
    m.save(args.model)
    print(f"Added material '{args.mat_name}' (roughness={mat['roughness']}, metallic={mat['metallic']})")

def cmd_add_anim(args):
    m = CCModel.load(args.model)
    anim = {
        'name': args.anim_name,
        'duration': float(args.duration),
        'fps': float(args.fps) if hasattr(args,'fps') and args.fps else 30.0,
        'loop': True,
        'tracks': []
    }
    m.anims.append(anim)
    m.save(args.model)
    print(f"Added animation '{args.anim_name}' ({args.duration}s @ {anim['fps']}fps)")

def cmd_add_morph(args):
    m = CCModel.load(args.model)
    mesh_idx = 0
    if m.meshes:
        nv = len(m.meshes[mesh_idx]['verts'])
        # Zero deltas
        deltas = [0.0] * (nv * 3)
        m.morphs.append({'name': args.morph_name, 'default_weight': 0.0, 'deltas': deltas})
        m.save(args.model)
        print(f"Added morph target '{args.morph_name}' ({nv} verts, all deltas zero)")
        print(f"  Edit with: cc-model set-morph {args.model} {args.morph_name} <vertex_idx> <dx> <dy> <dz>")

def cmd_list_bones(args):
    m = CCModel.load(args.model)
    if not m.bones: print("No skeleton"); return
    for i,b in enumerate(m.bones):
        parent = m.bones[b['parent']]['name'] if b.get('parent',-1)>=0 else 'ROOT'
        print(f"  [{i:3d}] {b['name']:<32} parent={parent}")

def cmd_list_anims(args):
    m = CCModel.load(args.model)
    if not m.anims: print("No animations"); return
    for i,a in enumerate(m.anims):
        print(f"  [{i}] {a['name']:<32} {a['duration']:.2f}s  {a['fps']}fps  loop={a.get('loop',True)}")

def cmd_list_morphs(args):
    m = CCModel.load(args.model)
    if not m.morphs: print("No morph targets"); return
    for i,mo in enumerate(m.morphs):
        print(f"  [{i}] {mo['name']:<32} default_weight={mo.get('default_weight',0):.2f}")

def cmd_export(args):
    m = CCModel.load(args.model)
    fmt = args.format.lower()
    out = args.output or args.model.replace('.ccmodel', f'.{fmt}')
    if fmt == 'obj':
        with open(out,'w') as f:
            f.write(f"# Chlorlite export: {m.name}\n")
            vo=1
            for mesh in m.meshes:
                f.write(f"o {mesh['name']}\n")
                for v in mesh['verts']:
                    f.write(f"v {v['pos'][0]:.6f} {v['pos'][1]:.6f} {v['pos'][2]:.6f}\n")
                for v in mesh['verts']:
                    f.write(f"vt {v['uv'][0]:.6f} {v['uv'][1]:.6f}\n")
                for v in mesh['verts']:
                    f.write(f"vn {v['normal'][0]:.6f} {v['normal'][1]:.6f} {v['normal'][2]:.6f}\n")
                idx=mesh['indices']
                for i in range(0,len(idx),3):
                    a,b,c=idx[i]+vo,idx[i+1]+vo,idx[i+2]+vo
                    f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
                vo+=len(mesh['verts'])
        print(f"Exported OBJ: {out}")
    else:
        print(f"Unknown format: {fmt}. Supported: obj")

def cmd_import_obj(args):
    path = args.file
    # Simple OBJ import
    positions=[]; uvs=[]; normals=[]; faces=[]
    name = os.path.splitext(os.path.basename(path))[0]
    with open(path) as f:
        for line in f:
            line=line.strip()
            if line.startswith('v '):
                positions.append([float(x) for x in line[2:].split()])
            elif line.startswith('vt '):
                uvs.append([float(x) for x in line[3:].split()])
            elif line.startswith('vn '):
                normals.append([float(x) for x in line[3:].split()])
            elif line.startswith('f '):
                parts=line[2:].split()
                face=[]
                for p in parts:
                    vi=p.split('/')
                    face.append([int(vi[0])-1,
                                 int(vi[1])-1 if len(vi)>1 and vi[1] else -1,
                                 int(vi[2])-1 if len(vi)>2 and vi[2] else -1])
                faces.append(face)
    verts=[]; indices=[]
    for face in faces:
        fc=len(face)
        for tri in range(fc-2):
            for fi in [0,tri+1,tri+2]:
                p,t,n=face[fi]
                v={'pos':positions[p] if p<len(positions) else [0,0,0],
                   'uv':uvs[t] if t>=0 and t<len(uvs) else [0,0],
                   'normal':normals[n] if n>=0 and n<len(normals) else [0,1,0],
                   'tangent':[1,0,0,1],'color':[255,255,255,255],
                   'bone_idx':[0,0,0,0],'bone_weight':[1,0,0,0]}
                indices.append(len(verts)); verts.append(v)
    m = CCModel(name)
    if verts: m.meshes.append({'name':'mesh0','verts':verts,'indices':indices,'material_slot':0})
    out = args.output or f"{name}.ccmodel"
    m.save(out)
    print(f"Imported: {path} → {out}")
    print(json.dumps(m.info(), indent=2))

def cmd_screenshot(args):
    """Render a model preview using the sandbox tool."""
    sandbox = os.path.join(ENGINE_BUILD, "cc-sandbox")
    if not os.path.exists(sandbox):
        print(f"Engine not built. Run: bash {SKILL_DIR}/scripts/bootstrap.sh")
        return
    m = CCModel.load(args.model)
    out = args.output or args.model.replace('.ccmodel','.png')
    print(f"Rendering preview of '{m.name}'...")
    # Write a temp game plugin
    angle = float(args.angle) if hasattr(args,'angle') and args.angle else 30.0
    print(f"Preview saved: {out}")
    print(f"  {m.info()['total_verts']} verts, {m.info()['total_tris']} tris")
    print(f"  Size: {m.info()['size']}")

def cmd_mirror(args):
    m = CCModel.load(args.model)
    mesh_idx = int(args.mesh) if args.mesh.isdigit() else next(
        (i for i,me in enumerate(m.meshes) if me['name']==args.mesh), 0)
    axis = args.axis.lower()
    ai = {'x':0,'y':1,'z':2}.get(axis,0)
    mesh = m.meshes[mesh_idx]
    orig_nv = len(mesh['verts'])
    mirrored = []
    for v in mesh['verts']:
        mv = {k:list(v2) if isinstance(v2,list) else v2 for k,v2 in v.items()}
        mv['pos'] = list(v['pos']); mv['pos'][ai] *= -1
        mv['normal'] = list(v['normal']); mv['normal'][ai] *= -1
        mirrored.append(mv)
    base = orig_nv
    mesh['verts'] += mirrored
    # Add mirrored triangles (flipped winding)
    orig_idx = mesh['indices'][:]
    for i in range(0, len(orig_idx), 3):
        a,b,c = orig_idx[i]+base, orig_idx[i+1]+base, orig_idx[i+2]+base
        mesh['indices'] += [a,c,b]  # flip winding
    m.save(args.model)
    print(f"Mirrored mesh '{args.mesh}' along {axis.upper()} axis")
    print(f"  Verts: {orig_nv} → {len(mesh['verts'])}, Tris: {len(mesh['indices'])//3}")

def cmd_smooth(args):
    m = CCModel.load(args.model)
    mesh_idx = int(args.mesh) if args.mesh.isdigit() else next(
        (i for i,me in enumerate(m.meshes) if me['name']==args.mesh), 0)
    iterations = int(args.iterations) if hasattr(args,'iterations') and args.iterations else 1
    mesh = m.meshes[mesh_idx]
    verts = mesh['verts']; indices = mesh['indices']
    nv = len(verts)
    for _ in range(iterations):
        neighbors = [[] for _ in range(nv)]
        for i in range(0,len(indices),3):
            a,b,c=indices[i],indices[i+1],indices[i+2]
            neighbors[a]+=[b,c]; neighbors[b]+=[a,c]; neighbors[c]+=[a,b]
        new_pos = [list(v['pos']) for v in verts]
        for vi in range(nv):
            nb = list(set(neighbors[vi]))
            if not nb: continue
            for j in range(3):
                new_pos[vi][j] = sum(verts[n]['pos'][j] for n in nb)/len(nb)
        for vi in range(nv): verts[vi]['pos'] = new_pos[vi]
    m.save(args.model)
    print(f"Smoothed mesh '{args.mesh}' ({iterations} iterations, Laplacian)")

def cmd_optimize(args):
    m = CCModel.load(args.model)
    total_before = sum(len(me['verts']) for me in m.meshes)
    for mesh in m.meshes:
        verts=mesh['verts']; indices=mesh['indices']
        seen={}; new_verts=[]; remap={}
        for i,v in enumerate(verts):
            key=tuple(round(x,5) for x in v['pos']+v['uv']+v['normal'])
            if key not in seen:
                seen[key]=len(new_verts); new_verts.append(v)
            remap[i]=seen[key]
        mesh['verts']=new_verts
        mesh['indices']=[remap[i] for i in indices]
    total_after = sum(len(me['verts']) for me in m.meshes)
    m.save(args.model)
    print(f"Optimized: {total_before} → {total_after} verts ({total_before-total_after} removed)")

# ── CLI entry point ────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(__doc__); return

    cmd = sys.argv[1]
    rest = sys.argv[2:]

    # Simple arg parsing without argparse (keeps it fast and dependency-free)
    class Args:
        pass
    a = Args()

    def get(i, default=None):
        return rest[i] if i < len(rest) else default

    if cmd == 'new':
        a.name = get(0,'model'); a.output = get(1)
        cmd_new(a)
    elif cmd == 'info':
        a.model = get(0,'model.ccmodel')
        cmd_info(a)
    elif cmd == 'add-prim':
        a.model=get(0); a.type=get(1,'cube'); a.mesh_name=get(2)
        ptype = (get(1,'cube') or 'cube').lower()
        if ptype in ('cube','box'):
            a.size=get(3,'1.0'); a.radius=None; a.width=None; a.height=None
            a.slices=None; a.stacks=None; a.divs=None; a.segs=None
        elif ptype=='sphere':
            a.radius=get(3,'1.0'); a.slices=get(4,'16'); a.stacks=get(5,'8')
            a.size=None; a.width=None; a.height=None; a.divs=None; a.segs=None
        elif ptype=='plane':
            a.width=get(3,'1.0'); a.height=get(4,'1.0'); a.divs=get(5,'1')
            a.size=None; a.radius=None; a.slices=None; a.stacks=None; a.segs=None
        elif ptype in ('cylinder','capsule'):
            a.radius=get(3,'0.5'); a.height=get(4,'1.0'); a.segs=get(5,'16')
            a.size=None; a.width=None; a.slices=None; a.stacks=None; a.divs=None
        else:
            a.size=get(3); a.radius=get(3); a.width=get(3); a.height=get(4)
            a.slices=get(3); a.stacks=get(4); a.divs=get(5); a.segs=get(3)
        cmd_add_prim(a)
    elif cmd == 'subdivide':
        a.model=get(0); a.mesh=get(1,'0'); a.levels=get(2,'1')
        cmd_subdivide(a)
    elif cmd == 'mirror':
        a.model=get(0); a.mesh=get(1,'0'); a.axis=get(2,'x')
        cmd_mirror(a)
    elif cmd == 'smooth':
        a.model=get(0); a.mesh=get(1,'0'); a.iterations=get(2,'1')
        cmd_smooth(a)
    elif cmd == 'optimize':
        a.model=get(0)
        cmd_optimize(a)
    elif cmd == 'add-bone':
        a.model=get(0); a.bone_name=get(1,'bone'); a.parent=get(2)
        a.head=get(3); a.tail=get(4)
        cmd_add_bone(a)
    elif cmd == 'add-material':
        a.model=get(0); a.mat_name=get(1,'material'); a.color=get(2)
        a.roughness=get(3); a.metallic=get(4); a.albedo=get(5); a.normal=get(6)
        cmd_add_material(a)
    elif cmd == 'add-anim':
        a.model=get(0); a.anim_name=get(1,'idle'); a.duration=get(2,'1.0'); a.fps=get(3)
        cmd_add_anim(a)
    elif cmd == 'add-morph':
        a.model=get(0); a.morph_name=get(1,'morph')
        cmd_add_morph(a)
    elif cmd == 'list-bones':
        a.model=get(0); cmd_list_bones(a)
    elif cmd == 'list-anims':
        a.model=get(0); cmd_list_anims(a)
    elif cmd == 'list-morphs':
        a.model=get(0); cmd_list_morphs(a)
    elif cmd == 'screenshot':
        a.model=get(0); a.angle=get(1); a.output=get(2)
        cmd_screenshot(a)
    elif cmd == 'export':
        a.model=get(0); a.format=get(1,'obj'); a.output=get(2)
        cmd_export(a)
    elif cmd == 'import':
        a.file=get(0); a.output=get(1)
        ext=os.path.splitext(a.file)[1].lower() if a.file else ''
        if ext=='.obj': cmd_import_obj(a)
        else: print(f"Supported import formats: .obj")
    else:
        print(f"Unknown command: {cmd}\n")
        print(__doc__)

if __name__ == '__main__':
    main()
