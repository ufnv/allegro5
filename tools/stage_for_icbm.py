#!/usr/bin/env python3
"""Copy a built Allegro into ICBM's own folder.

    python stage_for_icbm.py <allegro source dir> <ICBM dir> [--config RelWithDebInfo]
                             [--deps <deps dir>]

Creates (replacing any previous copy):
    <ICBM>/allegro5/include/allegro5/...  merged header tree: core headers, the
                                          generated ones from build/include
                                          (alplatf.h, *_cfg.h) and every addon's
                                          allegro5/ folder, internal headers
                                          included (ICBM uses aintern_image.h
                                          and aintern_vector.h)
    <ICBM>/allegro5/lib/*.lib, *.pdb      from <allegro>/build/lib/<config>
    <ICBM>/allegro5/deps/{include,lib}    third-party headers/libs Allegro was
                                          built with (default: the deps folder
                                          configure_release.cmd uses; lib/old
                                          is skipped)
    <ICBM>/allegro5/VERSION.txt           where it came from
"""
import os
import shutil
import subprocess
import sys

# The package's deps folder, as DEPS in configure_release.cmd.
DEFAULT_DEPS = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "deps"))


def copy_tree(src, dst, owner, seen):
    for root, _, files in os.walk(src):
        for name in files:
            if name.endswith(('.cmake', '.in')):
                continue          # templates, not headers
            s = os.path.join(root, name)
            rel = os.path.relpath(s, src)
            d = os.path.join(dst, rel)
            key = rel.replace('\\', '/').lower()
            if key in seen and seen[key] != owner:
                sys.exit('header clash: %s from %s and %s' % (rel, seen[key], owner))
            seen[key] = owner
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d)


def main():
    args = sys.argv[1:]
    config = 'RelWithDebInfo'
    deps = DEFAULT_DEPS
    if '--deps' in args:
        i = args.index('--deps')
        deps = args[i + 1]
        del args[i:i + 2]
    if '--config' in args:
        i = args.index('--config')
        config = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    src, icbm = (os.path.abspath(a) for a in args)
    build = os.path.join(src, 'build')
    libdir = os.path.join(build, 'lib', config)
    if not os.path.isfile(os.path.join(libdir, 'allegro-static.lib')):
        sys.exit('no allegro-static.lib in %s - build Allegro first' % libdir)

    dest = os.path.join(icbm, 'allegro5')
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    inc = os.path.join(dest, 'include', 'allegro5')
    seen = {}

    copy_tree(os.path.join(src, 'include', 'allegro5'), inc, 'include', seen)
    # generated headers (alplatf.h, internal/*_cfg.h) complete the core tree
    gen = os.path.join(build, 'include', 'allegro5')
    for root, _, files in os.walk(gen):
        for name in files:
            s = os.path.join(root, name)
            d = os.path.join(inc, os.path.relpath(s, gen))
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d)
    addons = os.path.join(src, 'addons')
    for addon in sorted(os.listdir(addons)):
        a = os.path.join(addons, addon, 'allegro5')
        if os.path.isdir(a):
            copy_tree(a, inc, 'addons/' + addon, seen)
    # generated addon config headers (e.g. aintern_image_cfg.h)
    gen_addons = os.path.join(build, 'addons')
    if os.path.isdir(gen_addons):
        for root, _, files in os.walk(gen_addons):
            for name in files:
                if name.endswith('.h') and os.sep + 'allegro5' + os.sep in root + os.sep:
                    s = os.path.join(root, name)
                    rel = s.split(os.sep + 'allegro5' + os.sep, 1)[1]
                    d = os.path.join(inc, rel)
                    os.makedirs(os.path.dirname(d), exist_ok=True)
                    shutil.copy2(s, d)

    lib = os.path.join(dest, 'lib')
    os.makedirs(lib)
    n = 0
    for name in sorted(os.listdir(libdir)):
        if name.endswith(('.lib', '.pdb')):
            shutil.copy2(os.path.join(libdir, name), lib)
            n += 1

    deps_dest = os.path.join(dest, 'deps')
    for part in ('include', 'lib'):
        shutil.copytree(os.path.join(deps, part), os.path.join(deps_dest, part),
                        ignore=shutil.ignore_patterns('old') if part == 'lib' else None)

    try:
        rev = subprocess.run(['git', '-C', src, 'log', '-1', '--format=%h %d %s'],
                             capture_output=True, text=True).stdout.strip()
    except OSError:
        rev = '?'
    with open(os.path.join(dest, 'VERSION.txt'), 'w') as f:
        f.write('Allegro staged for ICBM by allegro5_icbm/tools/stage_for_icbm.py\n')
        f.write('source: %s\ncommit: %s\nconfig: %s (x64, static, /MT)\n' % (src, rev, config))
        f.write('deps:   %s (copied to deps/, lib/old skipped)\n' % os.path.abspath(deps))

    headers = sum(len(f) for _, _, f in os.walk(os.path.join(dest, 'include')))
    dfiles = sum(len(f) for _, _, f in os.walk(deps_dest))
    print('staged %d Allegro headers, %d Allegro lib/pdb files and %d deps files into %s'
          % (headers, n, dfiles, dest))


if __name__ == '__main__':
    main()
