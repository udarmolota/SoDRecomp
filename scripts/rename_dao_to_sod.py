"""One-pass rename of a fresh copy of DAORecomp's sources into SoDRecomp's names
(docs/porting-playbook.md: "Rename the dao namespace, the DAO_ environment variables
and DAORecomp paths in one pass when copying, with a script, before changing
anything else").

Identifiers only: DAORecomp -> SoDRecomp, DAO_x -> SOD_x, Dao<Upper>x -> Sod..., dao_x ->
sod_x, dao<Upper>x -> sod..., the dao namespace, dao-<target> CMake targets, and file and
folder names built the same way. A bare "DAO" in prose (comments saying where code came
from) is left alone.

usage: rename_dao_to_sod.py <SoDRecomp folder>      (run once, on the fresh copy)
"""
import os
import re
import sys

TEXT = {'.c', '.cc', '.cpp', '.h', '.hpp', '.inl', '.hlsl', '.hlsli', '.xesl', '.xesli', '.txt', '.cmake',
        '.py', '.rc', '.md', '.in', '.json', '.toml', '.manifest'}

RULES = [
    (re.compile(r'DAORecomp'), 'SoDRecomp'),
    (re.compile(r'daorecomp'), 'sodrecomp'),
    (re.compile(r'\bDAO_(?=\w)'), 'SOD_'),
    (re.compile(r'\bDao(?=[A-Z])'), 'Sod'),
    (re.compile(r'\bdao_(?=\w)'), 'sod_'),
    (re.compile(r'\bdao(?=[A-Z])'), 'sod'),
    (re.compile(r'\bdao-(?=[a-z])'), 'sod-'),
    (re.compile(r'\bdao\b'), 'sod'),
]


def rename(text):
    for pattern, repl in RULES:
        text = pattern.sub(repl, text)
    return text


def main():
    root = sys.argv[1]
    changed_files = renamed = 0
    # Contents first, then names (deepest paths first so parents rename last).
    for dirpath, _, files in os.walk(root):
        for name in files:
            if os.path.splitext(name)[1].lower() not in TEXT and name != 'CMakeLists.txt':
                continue
            path = os.path.join(dirpath, name)
            if os.sep + 'thirdparty' + os.sep in path:
                continue  # SDL, antilag2, lmxxf-nr: not ours
            raw = open(path, 'rb').read()
            try:
                text = raw.decode('utf-8')
            except UnicodeDecodeError:
                continue
            new = rename(text)
            if new != text:
                open(path, 'wb').write(new.encode('utf-8'))
                changed_files += 1
    for dirpath, dirs, files in os.walk(root, topdown=False):
        if os.sep + 'thirdparty' in dirpath:
            continue
        for name in files + dirs:
            new = rename(name)
            if new != name:
                os.rename(os.path.join(dirpath, name), os.path.join(dirpath, new))
                renamed += 1
    print(f'{changed_files} files rewritten, {renamed} files/folders renamed')


if __name__ == '__main__':
    main()
