# Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
"""Reject private/retail artifacts and obvious credentials in the staged tree."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {'.iso', '.xex', '.rpf', '.ybn', '.ydr', '.ytd', '.ydd', '.yft',
             '.ymap', '.ytyp', '.exe', '.dll', '.asi', '.addon64', '.dmp',
             '.mp4', '.rar', '.7z', '.zip', '.svwc', '.svsd', '.svgj', '.bin'}
SECRETS = [re.compile(rb'gh[pousr]_[A-Za-z0-9]{30,}'),
           re.compile(rb'github_pat_[A-Za-z0-9_]{40,}'),
           re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
           re.compile(rb'AKIA[A-Z0-9]{16}')]
WORKSTATION = re.compile(rb'(?i)[cd]:[\\/]+AI[-]Test|Users[\\/]+Administra(?:tor)')

def main():
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    errors, count = [], 0
    for name in filter(None, names):
        p = ROOT / name
        if not p.is_file():
            errors.append(name + ': missing file'); continue
        count += 1
        if p.is_symlink() or name.startswith(('local/', 'port-evidence/', 'build/', 'upstream/')):
            errors.append(name + ': private/generated path')
        if p.suffix.lower() in FORBIDDEN and name != 'tools/board-wheel-icon.bin':
            errors.append(name + ': forbidden artifact')
        data = p.read_bytes()
        if any(pattern.search(data) for pattern in SECRETS):
            errors.append(name + ': possible credential (content suppressed)')
        if WORKSTATION.search(data):
            errors.append(name + ': workstation path')
    if errors:
        print('\n'.join(errors)); return 1
    print(f'Public tree audit PASS: {count} files; no private/retail artifacts or detected credentials.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
