#!/usr/bin/env python3
"""Validate kas schemas/includes and local source metadata, without fetching."""
from pathlib import Path
import hashlib
import re
from kas.includehandler import IncludeHandler
from kas.context import create_global_context
from types import SimpleNamespace

create_global_context(SimpleNamespace())

root = Path(__file__).resolve().parents[1]
count = 0
for path in sorted((root / 'yocto/kas').glob('*.yml')):
    config, missing = IncludeHandler([str(path)]).get_config()
    assert not missing, (path, missing)
    for name, repo in config.get('repos', {}).items():
        if 'url' in repo:
            assert re.fullmatch(r'[a-f0-9]{40}', repo.get('commit', '')), (path, name)
        else:
            for layer in repo.get('layers', {}):
                assert (root / layer / 'conf/layer.conf').is_file(), layer
    if path.name not in ('base.yml', 'rpi-base.yml'):
        assert config.get('machine'), path
    count += 1
license_hash = hashlib.md5((root / 'LICENSE').read_bytes()).hexdigest()
for recipe in (root / 'yocto').rglob('*.bb'):
    for expected in re.findall(r'md5=([a-f0-9]{32})', recipe.read_text()):
        assert expected == license_hash, recipe
assert (root / 'native/CMakeLists.txt').exists()
assert not (root / 'index.html').exists(), 'Web preview should not be shipped'
print(f'PASS: {count} kas files, official schema/include resolution, pinned revisions, local layers, license checksums')
print('This is metadata validation, not a BitBake build or a boot test.')
