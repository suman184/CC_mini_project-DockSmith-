import hashlib
import json
import os
from datetime import datetime
from typing import List, Dict, Any

IMAGES_DIR = os.path.expanduser("~/.docksmith/images/")


def _ensure_images_dir():
    os.makedirs(IMAGES_DIR, exist_ok=True)


def compute_manifest_digest(manifest: Dict[str, Any]) -> str:
    # Set digest to empty string for hashing
    manifest_copy = json.loads(json.dumps(manifest))
    manifest_copy["digest"] = ""
    data = json.dumps(manifest_copy, separators=(",", ":"), sort_keys=True).encode()
    return "sha256:" + hashlib.sha256(data).hexdigest()


def generate_manifest(
    name: str,
    tag: str,
    env: List[str],
    cmd: List[str],
    workdir: str,
    layers: List[Dict[str, Any]],
    created: str = None
) -> Dict[str, Any]:
    manifest = {
        "name": name,
        "tag": tag,
        "digest": "",
        "created": created or datetime.utcnow().isoformat() + "Z",
        "config": {
            "Env": env,
            "Cmd": cmd,
            "WorkingDir": workdir
        },
        "layers": layers
    }
    manifest["digest"] = compute_manifest_digest(manifest)
    return manifest


def save_manifest(manifest: Dict[str, Any]):
    _ensure_images_dir()
    name = manifest["name"]
    tag = manifest["tag"]
    path = os.path.join(IMAGES_DIR, f"{name}_{tag}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)


def load_manifest(name: str, tag: str) -> Dict[str, Any]:
    path = os.path.join(IMAGES_DIR, f"{name}_{tag}.json")
    if not os.path.exists(path):
        raise FileNotFoundError(f"Manifest {name}:{tag} not found")
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)
