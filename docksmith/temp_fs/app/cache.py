import hashlib
import json
import os
from typing import Dict, Any, List, Optional

CACHE_INDEX_PATH = os.path.expanduser("~/.docksmith/cache/index.json")


def _ensure_cache_dir():
    os.makedirs(os.path.dirname(CACHE_INDEX_PATH), exist_ok=True)


def load_cache() -> Dict[str, str]:
    _ensure_cache_dir()
    if not os.path.exists(CACHE_INDEX_PATH):
        return {}
    with open(CACHE_INDEX_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def save_cache(cache: Dict[str, str]):
    _ensure_cache_dir()
    with open(CACHE_INDEX_PATH, "w", encoding="utf-8") as f:
        json.dump(cache, f, indent=2, sort_keys=True)


def compute_cache_key(
    prev_layer_digest: str,
    instruction: str,
    workdir: str,
    env: Dict[str, str],
    copy_src_files: Optional[List[str]] = None
) -> str:
    m = hashlib.sha256()
    m.update(prev_layer_digest.encode())
    m.update(instruction.encode())
    m.update(workdir.encode())
    # ENV: sort keys, format as KEY=value
    env_items = [f"{k}={env[k]}" for k in sorted(env)]
    for item in env_items:
        m.update(item.encode())
    if copy_src_files is not None:
        # Sort file paths lexicographically
        sorted_files = sorted(copy_src_files)
        file_hashes = b""
        for path in sorted_files:
            with open(path, "rb") as f:
                file_hash = hashlib.sha256(f.read()).digest()
                file_hashes += file_hash
        m.update(hashlib.sha256(file_hashes).digest())
    return m.hexdigest()
