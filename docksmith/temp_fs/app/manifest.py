import hashlib
import json
import os
from datetime import datetime
from typing import List, Dict, Any

IMAGES_DIR = os.path.expanduser("~/.docksmith/images/")


def _ensure_images_dir():
    os.makedirs(IMAGES_DIR, exist_ok=True)


def _normalize_layer(layer: Any) -> Dict[str, Any]:
    if isinstance(layer, str):
        digest = layer if layer.startswith("sha256:") else f"sha256:{layer}"
        return {"digest": digest, "size": 0, "createdBy": "LEGACY"}
    if isinstance(layer, dict):
        normalized = dict(layer)
        digest = normalized.get("digest", "")
        if digest and not str(digest).startswith("sha256:"):
            normalized["digest"] = f"sha256:{digest}"
        normalized.setdefault("digest", "")
        normalized.setdefault("size", 0)
        normalized.setdefault("createdBy", "")
        return normalized
    return {"digest": "", "size": 0, "createdBy": "LEGACY"}


def _normalize_manifest(raw: Dict[str, Any], name: str, tag: str) -> Dict[str, Any]:
    normalized = dict(raw)
    normalized["name"] = normalized.get("name") or name
    normalized["tag"] = normalized.get("tag") or tag
    normalized["created"] = normalized.get("created") or "1970-01-01T00:00:00Z"

    config = normalized.get("config")
    if not isinstance(config, dict):
        config = {}
    env = config.get("Env") if isinstance(config.get("Env"), list) else []
    cmd = config.get("Cmd") if isinstance(config.get("Cmd"), list) else []
    workdir = config.get("WorkingDir") if isinstance(config.get("WorkingDir"), str) else ""
    normalized["config"] = {
        "Env": env,
        "Cmd": cmd,
        "WorkingDir": workdir,
    }

    raw_layers = normalized.get("layers", [])
    if not isinstance(raw_layers, list):
        raw_layers = []
    normalized["layers"] = [_normalize_layer(layer) for layer in raw_layers]

    digest = normalized.get("digest", "")
    if not isinstance(digest, str) or not digest.startswith("sha256:"):
        normalized["digest"] = compute_manifest_digest(normalized)
    return normalized


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
    normalized = _normalize_manifest(manifest, manifest.get("name", "unnamed"), manifest.get("tag", "latest"))
    name = normalized["name"]
    tag = normalized["tag"]
    path = os.path.join(IMAGES_DIR, f"{name}_{tag}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(normalized, f, indent=2, sort_keys=True)


def load_manifest(name: str, tag: str) -> Dict[str, Any]:
    path_tagged = os.path.join(IMAGES_DIR, f"{name}_{tag}.json")
    path_legacy = os.path.join(IMAGES_DIR, f"{name}.json")

    chosen = path_tagged if os.path.exists(path_tagged) else path_legacy
    if not os.path.exists(chosen):
        raise FileNotFoundError(f"Manifest {name}:{tag} not found")

    with open(chosen, "r", encoding="utf-8") as f:
        raw = json.load(f)

    # Accept minimal legacy base image files such as {"name":"base","version":"1.0"}
    if isinstance(raw, dict) and "layers" not in raw and "config" not in raw:
        raw = {
            "name": raw.get("name", name),
            "tag": tag,
            "created": "1970-01-01T00:00:00Z",
            "config": {"Env": [], "Cmd": [], "WorkingDir": ""},
            "layers": [],
            "digest": "",
        }

    return _normalize_manifest(raw, name, tag)
