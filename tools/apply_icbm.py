#!/usr/bin/env python3
"""Apply the ICBM changes to an Allegro 5 source tree.

    python apply_icbm.py <allegro source dir> [--branch NAME]
    python apply_icbm.py <allegro source dir> --verify

--verify only runs step 3 (use it after resolving conflicts by hand).

1. Copies overlay/ into the tree (new files only - never conflicts).
2. Applies hooks.patch. In a git repo it uses `git apply --3way`, so hunks
   that upstream moved still apply and real conflicts get conflict markers.
3. Checks that every hook is present (ICBM markers per file).

With --branch NAME (git repos only) a branch is created from the current
HEAD first and the result is committed there.
Exit code 0 = applied cleanly, 1 = conflicts / missing hooks to fix by hand.
"""
import os
import re
import shutil
import subprocess
import sys

PKG = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(cmd, cwd, check=True):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and r.returncode:
        sys.exit('%s failed:\n%s%s' % (' '.join(cmd), r.stdout, r.stderr))
    return r


def expected_hooks():
    """file -> number of added lines containing 'ICBM' or an overlay include."""
    counts = {}
    cur = None
    for line in open(os.path.join(PKG, 'hooks.patch'), encoding='utf-8'):
        m = re.match(r'\+\+\+ b/(.*)', line)
        if m:
            cur = m.group(1).strip()
            continue
        if cur and line.startswith('+') and ('ICBM' in line or '_icbm' in line):
            counts[cur] = counts.get(cur, 0) + 1
    return counts


# Upstream code that must be gone after the hooks are in (edits that carry no
# "ICBM" marker on every line, e.g. the ttf border constants).
FORBIDDEN = {
    'addons/ttf/ttf.c': [
        r'region\.[wh]\s*[-+]\s*4\b',          # 2-pixel border -> 2 * BORDER_SIZE
        r'region\.[xy]\s*\+\s*2\)?',           # -> + BORDER_SIZE
        r'glyph\.[xy]\s*-\s*1,',                # render_glyph -> icbm_draw_glyph
        r'\bw \+ 4, h \+ 4\b',
        r'region\.[wh]\s*=\s*[wh]\s*\+\s*4\b',
    ],
    'src/win/d3d_disp.cpp': [
        r'PeekMessage\(&msg, NULL, 0, 0, FALSE\)',  # display thread -> icbm_d3d_pump_messages
    ],
}


def leftovers(tree):
    found = []
    for rel, pats in FORBIDDEN.items():
        path = os.path.join(tree, rel)
        if not os.path.exists(path):
            continue
        for n, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
            for p in pats:
                if re.search(p, line):
                    found.append('%s:%d: %s' % (rel, n, line.strip()))
    return found


def copy_and_patch(tree, is_git):
    # 1. overlay
    overlay = os.path.join(PKG, 'overlay')
    n = 0
    for root, _, files in os.walk(overlay):
        for name in files:
            src = os.path.join(root, name)
            rel = os.path.relpath(src, overlay)
            dst = os.path.join(tree, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            n += 1
    print('overlay: copied %d files' % n)

    # 2. hooks
    patch = os.path.join(PKG, 'hooks.patch')
    cmd = ['git', 'apply', '--whitespace=nowarn'] + (['--3way'] if is_git else []) + [patch]
    r = run(cmd, tree, check=False)
    print(r.stdout + r.stderr)
    ok = r.returncode == 0
    print('hooks.patch: %s' % ('applied' if ok else 'NEEDS MANUAL WORK (see above)'))
    return ok


def main():
    args = sys.argv[1:]
    branch = None
    verify_only = '--verify' in args
    if verify_only:
        args.remove('--verify')
    if '--branch' in args:
        i = args.index('--branch')
        branch = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    tree = os.path.abspath(args[0])
    is_git = run(['git', 'rev-parse', '--is-inside-work-tree'], tree, check=False).returncode == 0

    if branch and not verify_only:
        if not is_git:
            sys.exit('--branch needs a git repository')
        run(['git', 'switch', '-c', branch], tree)

    ok = True
    if not verify_only:
        ok = copy_and_patch(tree, is_git)

    # 3. verify markers
    missing = []
    for rel, want in sorted(expected_hooks().items()):
        path = os.path.join(tree, rel)
        text = open(path, encoding='utf-8', errors='replace').read() if os.path.exists(path) else ''
        have = sum(1 for l in text.splitlines() if 'ICBM' in l or '_icbm' in l)
        conflict = '<<<<<<<' in text
        state = 'ok' if have >= want and not conflict else ('CONFLICT' if conflict else 'MISSING')
        if state != 'ok':
            missing.append(rel)
        print('  %-8s %-55s %d/%d hook lines' % (state, rel, have, want))

    left = leftovers(tree)
    for l in left:
        print('  LEFTOVER upstream code: ' + l)
    if left:
        missing.append('leftovers')

    if branch and ok and not missing:
        run(['git', 'add', '-A'], tree)
        run(['git', 'commit', '-q', '-m', 'ICBM modifications (allegro5_icbm package)'], tree)
        print('committed on branch %s' % branch)

    if not ok or missing:
        print('\nResolve the files above (see README.md, section per topic), then re-run '
              'the build and selftest.')
        sys.exit(1)
    print('\nAll ICBM changes applied.')


if __name__ == '__main__':
    main()
