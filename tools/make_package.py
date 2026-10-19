#!/usr/bin/env python3
"""Regenerate the ICBM Allegro package from a git branch.

    python make_package.py <allegro git repo> <base ref> [<icbm ref>]

Example (after porting to a new Allegro release on branch icbm-5.2.12):
    python make_package.py ../../allegro5_latest/allegro5 5.2.12.0 icbm-5.2.12

Writes, next to this tools/ folder:
    overlay/      every file the ICBM branch ADDS (copied verbatim)
    hooks.patch   the small in-place edits to upstream files (git diff)
    BASE.txt      the upstream ref/commit the package was made against
"""
import os
import shutil
import subprocess
import sys


def git(repo, *args):
    return subprocess.run(['git', '-C', repo] + list(args), check=True,
                          capture_output=True).stdout


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    repo, base = sys.argv[1], sys.argv[2]
    icbm = sys.argv[3] if len(sys.argv) == 4 else 'icbm'
    pkg = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    added = git(repo, 'diff', '--name-only', '--diff-filter=A', base, icbm).decode().split()
    changed = git(repo, 'diff', '--name-only', '--diff-filter=M', base, icbm).decode().split()
    other = git(repo, 'diff', '--name-only', '--diff-filter=DRT', base, icbm).decode().split()
    if other:
        sys.exit('Deleted/renamed files are not supported by the package: %s' % other)

    overlay = os.path.join(pkg, 'overlay')
    if os.path.isdir(overlay):
        shutil.rmtree(overlay)
    for path in added:
        data = git(repo, 'show', '%s:%s' % (icbm, path))
        out = os.path.join(overlay, *path.split('/'))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'wb') as f:
            f.write(data)

    patch = git(repo, 'diff', '--no-color', '--full-index', base, icbm, '--', *changed)
    with open(os.path.join(pkg, 'hooks.patch'), 'wb') as f:
        f.write(patch)

    commit = git(repo, 'rev-parse', base + '^{commit}').decode().strip()
    with open(os.path.join(pkg, 'BASE.txt'), 'w') as f:
        f.write('%s %s\n' % (base, commit))

    print('overlay: %d new files' % len(added))
    for p in added:
        print('   + ' + p)
    print('hooks.patch: %d upstream files, %d lines' % (len(changed), patch.count(b'\n')))
    for p in changed:
        print('   ~ ' + p)


if __name__ == '__main__':
    main()
