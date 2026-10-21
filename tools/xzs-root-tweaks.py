#!/usr/bin/env python3
"""Apply the XZS bring-up tweaks to an authentic iOS root made by tools/mkiosroot.sh.

Usage:
  python3 tools/xzs-root-tweaks.py MOUNTED_ROOT [OWNERS_JSON_OUT]

MOUNTED_ROOT is the HFS+ image mounted read-write.
When run as root, applies os.chown/os.chmod directly.
When run unprivileged on macOS (with -owners off), applies os.chmod and writes
desired {relpath: [uid, gid, mode]} to OWNERS_JSON_OUT for the HFS+ Catalog
B-Tree patcher in tools/mkiosroot-macos.py.
"""

import glob
import json
import os
import plistlib
import shutil
import sys

BUILD = '20H392'
VERSION = '16.7.16'
MOBILE = 501

IS_ROOT = (os.geteuid() == 0)
OWNERS_LOG = {}


def set_owner_mode(root, path, uid, gid, mode):
    rel = os.path.relpath(path, root)
    if rel == '.':
        rel = ''
    OWNERS_LOG[rel] = [int(uid), int(gid), int(mode) & 0o7777]
    if IS_ROOT:
        os.chown(path, uid, gid)
        os.chmod(path, mode)
    else:
        try:
            os.chmod(path, mode & 0o0777)
        except OSError:
            pass


def ensure_dir(root, rel_dir, uid, gid, mode):
    parts = [p for p in rel_dir.strip('/').split('/') if p]
    cur = root
    for i, part in enumerate(parts):
        cur = os.path.join(cur, part)
        is_last = (i == len(parts) - 1)
        if not os.path.isdir(cur):
            os.makedirs(cur, exist_ok=True)
            set_owner_mode(root, cur, uid if is_last else 0, gid if is_last else 0, mode if is_last else 0o755)
        elif is_last:
            set_owner_mode(root, cur, uid, gid, mode)
    return cur


def load(path):
    with open(path, 'rb') as f:
        return plistlib.load(f)


def save(root, path, data, uid, gid, mode, fmt=plistlib.FMT_BINARY):
    with open(path, 'wb') as f:
        plistlib.dump(data, f, fmt=fmt)
    set_owner_mode(root, path, uid, gid, mode)


def merge_pref(root, name, values):
    """Merge keys into /var/mobile/Library/Preferences/NAME.plist (mobile, 0600)."""
    ensure_dir(root, 'private/var/mobile', MOBILE, MOBILE, 0o755)
    ensure_dir(root, 'private/var/mobile/Library', MOBILE, MOBILE, 0o755)
    pref_dir = ensure_dir(root, 'private/var/mobile/Library/Preferences', MOBILE, MOBILE, 0o755)
    path = os.path.join(pref_dir, name + '.plist')
    data = load(path) if os.path.exists(path) else {}
    data.update(values)
    save(root, path, data, MOBILE, MOBILE, 0o600)
    print('  [PREF] updated', name, sorted(values))


def tweak_launchd(root, orig_owners=None):
    rel = 'System/Library/xpc/launchd.plist'
    path = os.path.join(root, rel)
    st = os.stat(path)
    uid, gid, mode = st.st_uid, st.st_gid, st.st_mode & 0o7777
    if orig_owners and rel in orig_owners:
        uid, gid, mode = orig_owners[rel]
    else:
        uid, gid = 0, 0
    plist = load(path)
    jobs = plist['LaunchDaemons']
    renamed = 0
    for job in jobs.values():
        if '_PanicOnCrash' in job:
            job['_PanicOnCrasX'] = job.pop('_PanicOnCrash')
            renamed += 1
    removed = [k for k, j in jobs.items() if j.get('Label') == 'com.apple.CommCenter']
    for k in removed:
        del jobs[k]
    save(root, path, plist, uid, gid, mode)
    print(f'  [LAUNCHD] {renamed} _PanicOnCrash renamed, {len(removed)} CommCenter jobs removed')


def create_containers(root):
    ensure_dir(root, 'private/var/containers', 0, 0, 0o755)
    ensure_dir(root, 'private/var/containers/Data', 0, 0, 0o755)
    ensure_dir(root, 'private/var/containers/Shared', 0, 0, 0o755)
    for sub, uid in (('Bundle', 33), ('Data/System', 0), ('Shared/SystemGroup', 0), ('Temp', 0)):
        rel = os.path.join('private/var/containers', sub)
        d = os.path.join(root, rel)
        existed = os.path.isdir(d)
        ensure_dir(root, rel, uid, uid, 0o755)
        if not existed:
            print('  [CONTAINERS] created', d)


def find_container(root, kind, identifier):
    for meta in glob.glob(os.path.join(root, 'private/var/containers', kind, '*',
                                       '.com.apple.mobile_container_manager.metadata.plist')):
        if load(meta).get('MCMMetadataIdentifier') == identifier:
            return os.path.dirname(meta)
    return None


def seed_activation(root):
    c = find_container(root, 'Data/System', 'com.apple.mobileactivationd')
    if c is None:
        print('  [ACTIVATION] warning: no mobileactivationd system container in the image; skipped data ark (Hactivation via ADT /chosen handles first boot)')
        return
    rel_c = os.path.relpath(c, root)
    ensure_dir(root, os.path.join(rel_c, 'Library'), MOBILE, MOBILE, 0o755)
    d = ensure_dir(root, os.path.join(rel_c, 'Library/internal'), MOBILE, MOBILE, 0o755)
    save(root, os.path.join(d, 'data_ark.plist'), {
        '-ActivationState': 'Activated',
        '-BrickState': False,
        '-BuildVersion': BUILD,
        '-ProductVersion': VERSION,
        '-DataMigrationCompleted': True,
        '-PreSydroDataMigrationCompleted': True,
    }, MOBILE, MOBILE, 0o600)
    print('  [ACTIVATION] seeded data ark in', d)


def disable_audio_and_camera_loops(root, orig_owners=None):
    src = os.path.join(root, 'Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin')
    dst_dir = os.path.join(root, 'Library/Audio/Plug-Ins/HAL.disabled')
    if os.path.exists(src):
        ensure_dir(root, 'Library/Audio/Plug-Ins/HAL.disabled', 0, 0, 0o755)
        shutil.move(src, dst_dir)
        for dirpath, dirnames, filenames in os.walk(os.path.join(dst_dir, 'VirtualAudio.plugin')):
            rel_d = os.path.relpath(dirpath, root)
            old_d = rel_d.replace('Library/Audio/Plug-Ins/HAL.disabled/', 'Library/Audio/Plug-Ins/HAL/', 1)
            if orig_owners and old_d in orig_owners:
                OWNERS_LOG[rel_d] = orig_owners[old_d]
            else:
                OWNERS_LOG[rel_d] = [0, 0, 0o755]
            for fn in filenames:
                p = os.path.join(dirpath, fn)
                rel_f = os.path.relpath(p, root)
                old_f = rel_f.replace('Library/Audio/Plug-Ins/HAL.disabled/', 'Library/Audio/Plug-Ins/HAL/', 1)
                if orig_owners and old_f in orig_owners:
                    OWNERS_LOG[rel_f] = orig_owners[old_f]
                else:
                    OWNERS_LOG[rel_f] = [0, 0, os.lstat(p).st_mode & 0o7777]
        print('  [AUDIO] VirtualAudio.plugin moved to disabled')
    for conf in glob.glob(os.path.join(root, 'System/Library/Frameworks/MediaToolbox.framework/*/SubServerConfiguration.plist')):
        rel = os.path.relpath(conf, root)
        st = os.stat(conf)
        uid, gid, mode = 0, 0, st.st_mode & 0o7777
        if orig_owners and rel in orig_owners:
            uid, gid, mode = orig_owners[rel]
        data = load(conf)
        data['StartRecorderServer'] = False
        save(root, conf, data, uid, gid, mode, plistlib.FMT_XML)
        print('  [MEDIA] StartRecorderServer set false in', rel)


def apply_all_tweaks(root, orig_owners=None):
    print(f'=== Applying XZS iOS Root Tweaks to {root} ===')
    tweak_launchd(root, orig_owners)
    create_containers(root)
    seed_activation(root)
    disable_audio_and_camera_loops(root, orig_owners)
    merge_pref(root, 'com.apple.migration', {
        'LastSystemVersion': BUILD,
        'DMUserDataDisposition': {'SyntheticDidUpgrade': False, 'buildVersion': BUILD}
    })
    merge_pref(root, 'com.apple.springboard', {
        'BKNoWatchdogs': True,
        'FBLaunchWatchdogExceptions': {'com.apple.purplebuddy': 100.0},
        'MCForceShowCloudConfigurationUI': False
    })
    merge_pref(root, 'com.apple.purplebuddy', {
        'SetupDone': True,
        'SetupFinishedAllSteps': True,
        'SetupVersion': 11,
        'daemonizeNeedsToRun': False
    })
    merge_pref(root, 'com.apple.purplebuddy.notbackedup', {
        k: False for k in (
            'ShouldRunDiagnosticsMiniBuddy',
            'ShouldRunHSA2MiniBuddy',
            'ShouldRunKeychainSyncMiniBuddy',
            'ShouldRunMesaMiniBuddy',
            'ShouldRunPaymentMiniBuddy',
            'ShouldRunStandardMiniBuddy',
            'SilentICDPUpgrade'
        )
    })
    merge_pref(root, 'com.apple.managedconfiguration.notbackedup', {
        'LockdownActivationIndicatesCloudConfigurationAvailable': False
    })
    print('=== All XZS iOS Root Tweaks Applied Successfully ===')
    return OWNERS_LOG


def main():
    if len(sys.argv) < 2 or not os.path.isdir(os.path.join(sys.argv[1], 'System/Library/xpc')):
        sys.exit(__doc__)
    root = sys.argv[1]
    owners_out = sys.argv[2] if len(sys.argv) >= 3 else None
    apply_all_tweaks(root)
    if owners_out:
        with open(owners_out, 'w') as f:
            json.dump(OWNERS_LOG, f)


if __name__ == '__main__':
    main()
