#!/usr/bin/env python3
"""Validate Tier B simulated-VR test save directories.

Usage: check_test_saves.py DIR...   (exit 1 on any problem)
"""
import pathlib
import sys


def check_save_dir(path):
    """Return problem strings for a test save dir; empty when usable."""
    path = pathlib.Path(path)
    if not path.is_dir():
        return [f'{path}: not a directory (save missing)']
    problems = []
    if not any(p.is_dir() for p in path.glob('SLOT*')):
        problems.append(f'{path}: no SLOT* subfolder')
    if not (path / 'profile.bin').is_file():
        problems.append(f'{path}: no profile.bin')
    return problems


def main(argv):
    if not argv:
        print(__doc__.strip())
        return 2
    problems = []
    for arg in argv:
        found = check_save_dir(pathlib.Path(arg).expanduser())
        print(f'{"FAIL" if found else "ok  "} {arg}')
        problems += found
    for line in problems:
        print(f'  {line}')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
