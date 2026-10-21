#!/usr/bin/env python3
"""Unprivileged macOS builder for the XZS iOS 16 Case-Sensitive Journaled HFS+ RootFS.

Usage:
  python3 tools/mkiosroot-macos.py OS.dmg SYSTEMOS.dmg APPOS.dmg OUT.img [SIZE]

How it works without sudo on macOS:
  1. Attaches each APFS DMG read-only with `-owners on` to capture the exact
     (uid, gid, mode) of every file, directory, and symlink in the IPSW DMGs.
  2. Re-attaches each APFS DMG read-only with `-owners off` so that root-owned
     0700 directories (e.g. private/var/db/diagnostics) can be read unprivileged.
  3. Creates a raw (layout NONE) Case-sensitive Journaled HFS+ image of SIZE
     (default 14g) using `hdiutil create`, attaches it R/W with `-owners off`,
     and populates:
       - OS root -> /
       - SystemOS cryptex -> /private/preboot/Cryptexes/OS
       - AppOS cryptex -> /private/preboot/Cryptexes/App
       - /System/Library/Caches/com.apple.dyld hard-links
       - Cryptex root hard-links for ignition_level=1
       - /private/var/db/{diagnostics,uuidtext,diagnostics/timesync}/.migrated
       - /private/var/keybags/systembag.kb
       - XZS root tweaks (tools/xzs-root-tweaks.py)
  4. Detaches the HFS+ image cleanly, then walks the HFS+ Catalog B-Tree leaf
     nodes directly in the raw image to restore the exact (uid, gid, mode) on
     every folder, file, symlink, and HFS+ Private Data hard-link iNode record.
  5. Verifies the resulting HFS+ image with `/sbin/fsck_hfs -fn`.
"""

import os
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)


def run_cmd(cmd, check=True, capture=False):
    if capture:
        return subprocess.run(cmd, check=check, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return subprocess.run(cmd, check=check)


def hdiutil_attach(img_path, readonly=True, owners_on=True, mount=True):
    cmd = ['hdiutil', 'attach', '-plist', '-nobrowse']
    if readonly:
        cmd.append('-readonly')
    else:
        cmd.append('-readwrite')
    if not mount:
        cmd.append('-nomount')
    else:
        cmd.extend(['-owners', 'on' if owners_on else 'off'])
    cmd.append(img_path)
    res = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    info = plistlib.loads(res.stdout)
    dev_entry = None
    mount_point = None
    for ent in info.get('system-entities', []):
        if dev_entry is None and 'dev-entry' in ent:
            dev_entry = ent['dev-entry']
        if 'mount-point' in ent:
            mount_point = ent['mount-point']
            dev_entry = ent.get('dev-entry', dev_entry)
    return dev_entry, mount_point


def hdiutil_detach(target):
    if not target:
        return
    subprocess.run(['hdiutil', 'detach', target], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(['hdiutil', 'detach', '-force', target], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def detach_existing_dmg(dmg_path):
    real = os.path.realpath(dmg_path)
    res = subprocess.run(['hdiutil', 'info', '-plist'], check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if res.returncode != 0:
        return
    try:
        info = plistlib.loads(res.stdout)
    except Exception:
        return
    for img in info.get('images', []):
        ip = img.get('image-path', '')
        if os.path.realpath(ip) == real or 'ios16-root' in ip:
            for ent in img.get('system-entities', []):
                if 'dev-entry' in ent:
                    hdiutil_detach(ent['dev-entry'])
                    break


def scan_owners(mount_point, prefix=''):
    """Walk mount_point with os.lstat and return {relpath: (uid, gid, mode)}."""
    owners = {}
    st_root = os.lstat(mount_point)
    owners[prefix.strip('/')] = (st_root.st_uid, st_root.st_gid, st_root.st_mode & 0o7777)

    def _onerror(err):
        pass

    for dirpath, dirnames, filenames in os.walk(mount_point, topdown=True, onerror=_onerror, followlinks=False):
        rel_dir = os.path.relpath(dirpath, mount_point)
        if rel_dir == '.':
            rel_dir = ''
        full_rel_dir = os.path.join(prefix, rel_dir).strip('/') if prefix else rel_dir
        try:
            st = os.lstat(dirpath)
            owners[full_rel_dir] = (st.st_uid, st.st_gid, st.st_mode & 0o7777)
        except OSError:
            pass
        for name in dirnames + filenames:
            p = os.path.join(dirpath, name)
            rel_p = os.path.join(full_rel_dir, name).strip('/') if full_rel_dir else name
            try:
                st = os.lstat(p)
                owners[rel_p] = (st.st_uid, st.st_gid, st.st_mode & 0o7777)
            except OSError:
                pass
    return owners


def patch_hfs_catalog_owners(img_path, owners_map):
    """Directly patch (uid, gid, mode) in the HFS+ Catalog B-Tree of img_path."""
    print(f'[B-TREE] Opening {img_path} for HFS+ Catalog B-Tree ownership & mode fixup...')
    nfd_owners = {}
    for k, v in owners_map.items():
        norm_k = unicodedata.normalize('NFD', k.strip('/'))
        nfd_owners[norm_k] = v
        nfd_owners[k.strip('/')] = v

    with open(img_path, 'r+b') as f:
        f.seek(0x400)
        vh = f.read(512)
        sig, version = struct.unpack('>2sH', vh[0:4])
        if sig not in (b'HX', b'H+'):
            raise RuntimeError(f'Unexpected HFS+ volume signature: {sig!r}')
        block_size = struct.unpack('>I', vh[40:44])[0]
        cat_file = vh[272:352]
        cat_logical_size = struct.unpack('>Q', cat_file[0:8])[0]
        cat_total_blocks = struct.unpack('>I', cat_file[12:16])[0]
        cat_extents = []
        covered_blocks = 0
        for i in range(8):
            sb, bc = struct.unpack('>II', cat_file[16 + i * 8: 24 + i * 8])
            if bc > 0:
                cat_extents.append((sb, bc))
                covered_blocks += bc
        print(f'[B-TREE] Volume sig={sig.decode()} blockSize={block_size} catalogBlocks={cat_total_blocks} extents={cat_extents}')
        if covered_blocks < cat_total_blocks:
            raise RuntimeError(f'Catalog file fragmented beyond 8 inline extents ({covered_blocks} < {cat_total_blocks})')

        def cat_to_disk(cat_off):
            blk = cat_off // block_size
            in_blk = cat_off % block_size
            cur = 0
            for sb, bc in cat_extents:
                if blk < cur + bc:
                    return (sb + (blk - cur)) * block_size + in_blk
                cur += bc
            raise RuntimeError(f'Catalog offset {cat_off} out of range')

        def read_cat_node(node_num, node_size):
            cat_off = node_num * node_size
            # A B-Tree node is typically 8192 bytes (2 blocks); read block by block in case it spans extents
            chunks = []
            rem = node_size
            off = cat_off
            while rem > 0:
                step = min(rem, block_size - (off % block_size))
                f.seek(cat_to_disk(off))
                chunks.append(f.read(step))
                off += step
                rem -= step
            return bytearray(b''.join(chunks))

        def write_cat_node(node_num, node_size, buf):
            cat_off = node_num * node_size
            rem = node_size
            off = cat_off
            pos = 0
            while rem > 0:
                step = min(rem, block_size - (off % block_size))
                f.seek(cat_to_disk(off))
                f.write(buf[pos:pos + step])
                off += step
                pos += step
                rem -= step

        f.seek(cat_to_disk(0))
        hdr = f.read(512)
        node_size, total_nodes, free_nodes = struct.unpack('>HII', hdr[32:42])
        first_leaf, last_leaf = struct.unpack('>II', hdr[24:32])
        print(f'[B-TREE] Catalog nodeSize={node_size} firstLeaf={first_leaf} lastLeaf={last_leaf}')

        # Pass 1: Build folder CNID -> (parent_cnid, name) map across all leaf nodes
        folders = {}
        priv_dir_cnids = set()
        node_num = first_leaf
        leaf_nodes_order = []
        while node_num != 0:
            leaf_nodes_order.append(node_num)
            buf = read_cat_node(node_num, node_size)
            f_link, b_link, kind, height, num_recs = struct.unpack('>IIbbH', buf[0:12])
            if kind != -1:
                raise RuntimeError(f'Expected leaf node (kind=-1) at node {node_num}, got {kind}')
            offsets = [struct.unpack('>H', buf[node_size - 2 * (i + 1): node_size - 2 * i])[0] for i in range(num_recs + 1)]
            for i in range(num_recs):
                roff = offsets[i]
                key_len = struct.unpack('>H', buf[roff:roff + 2])[0]
                parent_cnid, name_len = struct.unpack('>IH', buf[roff + 2:roff + 8])
                name = buf[roff + 8:roff + 8 + name_len * 2].decode('utf-16-be', errors='surrogatepass')
                doff = roff + 2 + key_len
                if doff & 1:
                    doff += 1
                rtype = struct.unpack('>h', buf[doff:doff + 2])[0]
                if rtype == 1:  # Folder record
                    cnid = struct.unpack('>I', buf[doff + 8:doff + 12])[0]
                    folders[cnid] = (parent_cnid, name)
                    if parent_cnid == 2 and ('HFS+ Private Data' in name or 'HFS+ Private Directory Data' in name):
                        priv_dir_cnids.add(cnid)
            node_num = f_link

        path_cache = {2: ''}

        def get_folder_path(cnid):
            if cnid in path_cache:
                return path_cache[cnid]
            if cnid not in folders:
                return None
            pcnid, name = folders[cnid]
            if pcnid == 1:
                path_cache[cnid] = ''
                return ''
            ppath = get_folder_path(pcnid)
            if ppath is None:
                return None
            res = f'{ppath}/{name}' if ppath else name
            path_cache[cnid] = res
            return res

        for cid in folders:
            get_folder_path(cid)

        # Pass 2a: Patch all normal folders & files, and collect hardlink iNode<N> -> (uid, gid, mode)
        hardlink_inodes = {}
        priv_leaf_nodes = set()
        patched_folders = 0
        patched_files = 0
        hardlink_entries = 0
        fallback_entries = 0

        for node_num in leaf_nodes_order:
            buf = read_cat_node(node_num, node_size)
            f_link, b_link, kind, height, num_recs = struct.unpack('>IIbbH', buf[0:12])
            offsets = [struct.unpack('>H', buf[node_size - 2 * (i + 1): node_size - 2 * i])[0] for i in range(num_recs + 1)]
            dirty = False
            for i in range(num_recs):
                roff = offsets[i]
                key_len = struct.unpack('>H', buf[roff:roff + 2])[0]
                parent_cnid, name_len = struct.unpack('>IH', buf[roff + 2:roff + 8])
                name = buf[roff + 8:roff + 8 + name_len * 2].decode('utf-16-be', errors='surrogatepass')
                doff = roff + 2 + key_len
                if doff & 1:
                    doff += 1
                rtype = struct.unpack('>h', buf[doff:doff + 2])[0]
                if rtype not in (1, 2):
                    continue
                flags = struct.unpack('>H', buf[doff + 2:doff + 4])[0]
                cnid = struct.unpack('>I', buf[doff + 8:doff + 12])[0]
                if cnid in priv_dir_cnids:
                    continue
                if parent_cnid in priv_dir_cnids:
                    priv_leaf_nodes.add(node_num)
                    continue
                if parent_cnid == 1 and cnid == 2:
                    rel_path = ''
                else:
                    ppath = get_folder_path(parent_cnid)
                    if ppath is None:
                        continue
                    rel_path = f'{ppath}/{name}' if ppath else name

                norm_rel = unicodedata.normalize('NFD', rel_path)
                cur_uid, cur_gid, admin_flags, owner_flags, cur_mode, special = struct.unpack(
                    '>IIBBHI', buf[doff + 32:doff + 48]
                )
                if rel_path in nfd_owners:
                    uid, gid, mode = nfd_owners[rel_path]
                elif norm_rel in nfd_owners:
                    uid, gid, mode = nfd_owners[norm_rel]
                else:
                    uid, gid, mode = 0, 0, cur_mode & 0o7777
                    fallback_entries += 1

                if rtype == 2 and (flags & 0x20):
                    # Hard-link directory entry: ownerID/groupID store prevlink/nextlink!
                    # Record the target iNode<special> permissions instead of clobbering link chain.
                    hardlink_inodes[f'iNode{special}'] = (uid, gid, mode)
                    hardlink_entries += 1
                    continue

                new_mode = (cur_mode & 0o170000) | (mode & 0o7777)
                if cur_uid != uid or cur_gid != gid or cur_mode != new_mode:
                    buf[doff + 32:doff + 44] = struct.pack('>IIBBH', uid, gid, admin_flags, owner_flags, new_mode)
                    dirty = True
                if rtype == 1:
                    patched_folders += 1
                else:
                    patched_files += 1

            if dirty:
                write_cat_node(node_num, node_size, buf)

        # Pass 2b: Patch iNode<N> backing records inside HFS+ Private Data
        patched_inodes = 0
        for node_num in sorted(priv_leaf_nodes):
            buf = read_cat_node(node_num, node_size)
            f_link, b_link, kind, height, num_recs = struct.unpack('>IIbbH', buf[0:12])
            offsets = [struct.unpack('>H', buf[node_size - 2 * (i + 1): node_size - 2 * i])[0] for i in range(num_recs + 1)]
            dirty = False
            for i in range(num_recs):
                roff = offsets[i]
                key_len = struct.unpack('>H', buf[roff:roff + 2])[0]
                parent_cnid, name_len = struct.unpack('>IH', buf[roff + 2:roff + 8])
                if parent_cnid not in priv_dir_cnids:
                    continue
                name = buf[roff + 8:roff + 8 + name_len * 2].decode('utf-16-be', errors='surrogatepass')
                doff = roff + 2 + key_len
                if doff & 1:
                    doff += 1
                rtype = struct.unpack('>h', buf[doff:doff + 2])[0]
                if rtype != 2:
                    continue
                cur_uid, cur_gid, admin_flags, owner_flags, cur_mode = struct.unpack(
                    '>IIBBH', buf[doff + 32:doff + 44]
                )
                uid, gid, mode = hardlink_inodes.get(name, (0, 0, cur_mode & 0o7777))
                new_mode = (cur_mode & 0o170000) | (mode & 0o7777)
                if cur_uid != uid or cur_gid != gid or cur_mode != new_mode:
                    buf[doff + 32:doff + 44] = struct.pack('>IIBBH', uid, gid, admin_flags, owner_flags, new_mode)
                    dirty = True
                patched_inodes += 1
            if dirty:
                write_cat_node(node_num, node_size, buf)

        f.flush()
        os.fsync(f.fileno())
        print(
            f'[B-TREE] Patched {patched_folders} folders, {patched_files} files, '
            f'{patched_inodes} hardlink iNodes ({hardlink_entries} link entries, {fallback_entries} defaults)'
        )


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    os_dmg = os.path.realpath(sys.argv[1])
    sysos_dmg = os.path.realpath(sys.argv[2])
    appos_dmg = os.path.realpath(sys.argv[3])
    out_img = os.path.abspath(sys.argv[4])
    size = sys.argv[5] if len(sys.argv) >= 6 else '14g'

    if os.path.exists(out_img):
        sys.exit(f'Error: refusing to overwrite existing {out_img}')

    os.makedirs(os.path.dirname(out_img), exist_ok=True)
    rsync_bin = '/opt/homebrew/bin/rsync' if os.path.exists('/opt/homebrew/bin/rsync') else 'rsync'

    active_devs = []
    try:
        for dmg in (os_dmg, sysos_dmg, appos_dmg):
            detach_existing_dmg(dmg)

        print('=== Step 1/6: Scanning authentic (uid, gid, mode) from APFS DMGs (-owners on) ===')
        owners_map = {'': (0, 0, 0o755)}
        for label, dmg, prefix in (
            ('OS', os_dmg, ''),
            ('SystemOS', sysos_dmg, 'private/preboot/Cryptexes/OS'),
            ('AppOS', appos_dmg, 'private/preboot/Cryptexes/App'),
        ):
            dev, mnt = hdiutil_attach(dmg, readonly=True, owners_on=True)
            try:
                scanned = scan_owners(mnt, prefix=prefix)
                print(f'  [{label}] scanned {len(scanned)} entries from {mnt}')
                owners_map.update(scanned)
                if prefix:
                    # Also record root-relative fallback for cryptex files that get linked into /
                    for k, v in scanned.items():
                        if k.startswith(prefix + '/'):
                            sub = k[len(prefix) + 1:]
                            if sub not in owners_map:
                                owners_map[sub] = v
            finally:
                hdiutil_detach(dev)

        for p in ('private', 'private/preboot', 'private/preboot/Cryptexes',
                  'private/preboot/Cryptexes/OS', 'private/preboot/Cryptexes/App',
                  'System/Library/Caches/com.apple.dyld'):
            owners_map.setdefault(p, (0, 0, 0o755))

        print('=== Step 2/6: Attaching APFS DMGs with -owners off for full unprivileged read ===')
        os_dev, os_mnt = hdiutil_attach(os_dmg, readonly=True, owners_on=False)
        active_devs.append(os_dev)
        sys_dev, sys_mnt = hdiutil_attach(sysos_dmg, readonly=True, owners_on=False)
        active_devs.append(sys_dev)
        app_dev, app_mnt = hdiutil_attach(appos_dmg, readonly=True, owners_on=False)
        active_devs.append(app_dev)

        print(f'=== Step 3/6: Creating {size} Case-sensitive Journaled HFS+ raw image ===')
        tmp_dmg = os.path.join(tempfile.gettempdir(), f'xzs_ios16_root_{os.getpid()}.dmg')
        tmp_img = os.path.join(tempfile.gettempdir(), f'xzs_ios16_root_{os.getpid()}.img')
        for p in (tmp_dmg, tmp_img):
            if os.path.exists(p):
                os.remove(p)
        run_cmd([
            'hdiutil', 'create', '-size', size,
            '-fs', 'Case-sensitive Journaled HFS+',
            '-volname', 'iOSRoot',
            '-layout', 'NONE',
            tmp_dmg
        ])
        os.rename(tmp_dmg, tmp_img)

        root_dev, root_mnt = hdiutil_attach(tmp_img, readonly=False, owners_on=False)
        active_devs.append(root_dev)
        print(f'  Mounted HFS+ image at {root_mnt} ({root_dev})')

        print('=== Step 4/6: Populating OS root + SystemOS & AppOS Cryptexes ===')
        for src, dst_rel in (
            (os_mnt, ''),
            (sys_mnt, 'private/preboot/Cryptexes/OS'),
            (app_mnt, 'private/preboot/Cryptexes/App'),
        ):
            dst = os.path.join(root_mnt, dst_rel) if dst_rel else root_mnt
            os.makedirs(dst, exist_ok=True)
            print(f'  rsync {src}/ -> /{dst_rel}')
            run_cmd([rsync_bin, '-aH', '--exclude=/.file', f'{src}/', f'{dst}/'])

        # Create /.file (0-byte mode 0000 volfs stub excluded above because mode 0000 is unreadable unprivileged)
        dot_file = os.path.join(root_mnt, '.file')
        with open(dot_file, 'wb'):
            pass
        owners_map['.file'] = (0, 0, 0)

        # Shared cache hard-links for ignition_level=1
        dsc_rel = 'private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld'
        dsc_src_dir = os.path.join(root_mnt, dsc_rel)
        dsc_dst_dir = os.path.join(root_mnt, 'System/Library/Caches/com.apple.dyld')
        os.makedirs(dsc_dst_dir, exist_ok=True)
        for fn in os.listdir(dsc_src_dir):
            src_f = os.path.join(dsc_src_dir, fn)
            dst_f = os.path.join(dsc_dst_dir, fn)
            if os.path.exists(dst_f) or os.path.islink(dst_f):
                os.remove(dst_f)
            os.link(src_f, dst_f)
            owners_map[f'System/Library/Caches/com.apple.dyld/{fn}'] = owners_map.get(
                f'{dsc_rel}/{fn}', (0, 0, 0o644)
            )

        # Hard-link cryptex files that have no counterpart in OS root
        linked_count = 0
        for c in ('OS', 'App'):
            c_base = os.path.join(root_mnt, 'private/preboot/Cryptexes', c)
            for dirpath, dirnames, filenames in os.walk(c_base, followlinks=False):
                rel_d = os.path.relpath(dirpath, c_base)
                if rel_d == '.':
                    rel_d = ''
                if rel_d.startswith('System/Library/Caches/com.apple.dyld'):
                    continue
                for name in dirnames + filenames:
                    src_p = os.path.join(dirpath, name)
                    rel_p = os.path.join(rel_d, name) if rel_d else name
                    if rel_p.startswith('System/Library/Caches/com.apple.dyld/'):
                        continue
                    if os.path.isdir(src_p) and not os.path.islink(src_p):
                        continue
                    dst_p = os.path.join(root_mnt, rel_p)
                    if not os.path.exists(dst_p) and not os.path.islink(dst_p):
                        parent_rel = os.path.dirname(rel_p)
                        if parent_rel:
                            parts = parent_rel.split('/')
                            for idx in range(len(parts)):
                                sub_d = '/'.join(parts[:idx + 1])
                                owners_map.setdefault(sub_d, (0, 0, 0o755))
                            os.makedirs(os.path.dirname(dst_p), exist_ok=True)
                        if os.path.islink(src_p):
                            target = os.readlink(src_p)
                            os.symlink(target, dst_p)
                        else:
                            os.link(src_p, dst_p)
                        c_rel = f'private/preboot/Cryptexes/{c}/{rel_p}'
                        owners_map[rel_p] = owners_map.get(c_rel, (0, 0, 0o755))
                        linked_count += 1
        print(f'  Hard-linked/copied {linked_count} cryptex files into /')

        # logd directories & .migrated markers (uid=272, gid=272)
        for rel_d, mode in (
            ('private/var/db/diagnostics', 0o751),
            ('private/var/db/uuidtext', 0o755),
            ('private/var/db/diagnostics/timesync', 0o755),
        ):
            abs_d = os.path.join(root_mnt, rel_d)
            os.makedirs(abs_d, exist_ok=True)
            owners_map[rel_d] = (272, 272, mode)
            mig_rel = f'{rel_d}/.migrated'
            with open(os.path.join(root_mnt, mig_rel), 'wb') as mf:
                pass
            owners_map[mig_rel] = (272, 272, 0o644)

        # System keybag for built-in AppleKeyStore
        kb_dir = os.path.join(root_mnt, 'private/var/keybags')
        os.makedirs(kb_dir, exist_ok=True)
        owners_map['private/var/keybags'] = (0, 0, 0o700)
        kb_path = os.path.join(kb_dir, 'systembag.kb')
        with open(kb_path, 'wb') as kf:
            kf.write(
                b'<?xml version="1.0" encoding="UTF-8"?>\n'
                b'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
                b'<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>\n'
            )
        owners_map['private/var/keybags/systembag.kb'] = (0, 0, 0o600)

        # Run XZS bring-up tweaks directly
        import importlib.util
        spec = importlib.util.spec_from_file_location('xzs_root_tweaks', os.path.join(SCRIPT_DIR, 'xzs-root-tweaks.py'))
        tweaks_mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tweaks_mod)
        tweaked_owners = tweaks_mod.apply_all_tweaks(root_mnt, owners_map)
        for k, v in tweaked_owners.items():
            owners_map[k] = tuple(v)

        run_cmd(['df', '-h', root_mnt])

        print('=== Step 5/6: Detaching DMGs and patching HFS+ Catalog B-Tree owners/modes ===')
        for dev in reversed(active_devs):
            hdiutil_detach(dev)
        active_devs.clear()

        patch_hfs_catalog_owners(tmp_img, owners_map)

        print('=== Step 6/6: Verifying HFS+ filesystem integrity with fsck_hfs ===')
        chk_dev, _ = hdiutil_attach(tmp_img, readonly=False, owners_on=False, mount=False)
        active_devs.append(chk_dev)
        # First let fsck_hfs repair any minor catalog sort/count differences if any, then verify read-only
        subprocess.run(['/sbin/fsck_hfs', '-fy', chk_dev], check=False)
        run_cmd(['/sbin/fsck_hfs', '-fn', chk_dev], check=True)
        hdiutil_detach(chk_dev)
        active_devs.clear()
        shutil.move(tmp_img, out_img)
        print(f'=== SUCCESS: {out_img} built and verified cleanly! ===')

    finally:
        for dev in reversed(active_devs):
            hdiutil_detach(dev)


if __name__ == '__main__':
    main()
