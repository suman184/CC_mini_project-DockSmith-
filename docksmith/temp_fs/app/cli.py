import argparse
import os
import sys
import json
import hashlib
import shutil
from pathlib import Path
from cache import compute_cache_key, load_cache, save_cache
from manifest import generate_manifest, save_manifest, load_manifest

CACHE_INDEX_PATH = os.path.expanduser("~/.docksmith/cache/index.json")
IMAGES_DIR = os.path.expanduser("~/.docksmith/images/")
LAYERS_DIR = os.path.expanduser("~/.docksmith/layers/")

# Helper to print table

def print_table(rows, headers):
    col_widths = [max(len(str(x)) for x in col) for col in zip(*([headers] + rows))]
    fmt = "  ".join([f"{{:<{w}}}" for w in col_widths])
    print(fmt.format(*headers))
    print("  ".join(["-" * w for w in col_widths]))
    for row in rows:
        print(fmt.format(*row))


def _ensure_runtime_dirs():
    os.makedirs(IMAGES_DIR, exist_ok=True)
    os.makedirs(LAYERS_DIR, exist_ok=True)


def _compute_layer_digest(cache_key: str, prev_layer_digest: str) -> str:
    h = hashlib.sha256()
    h.update((prev_layer_digest or "").encode("utf-8"))
    h.update(cache_key.encode("utf-8"))
    return h.hexdigest()


def _layer_artifact_path(layer_digest: str) -> str:
    return os.path.join(LAYERS_DIR, f"{layer_digest}.tar")


def _write_layer_artifact(layer_digest: str, created_by: str, cache_key: str) -> int:
    path = _layer_artifact_path(layer_digest)
    payload = {
        "digest": f"sha256:{layer_digest}",
        "createdBy": created_by,
        "cacheKey": cache_key,
    }
    with open(path, "wb") as f:
        f.write(json.dumps(payload, sort_keys=True).encode("utf-8"))
    return os.path.getsize(path)


def _copy_sources_into_temp_fs(context: str, src: str, dest: str, temp_root: str):
    def _is_ignored(path_obj: Path) -> bool:
        rel = path_obj.relative_to(context)
        rel_parts = set(rel.parts)
        return "temp_fs" in rel_parts or "__pycache__" in rel_parts

    src_path = Path(context) / src
    if any(char in src for char in "*?[]"):
        matches = [p for p in Path(context).glob(src) if p.is_file() and not _is_ignored(p)]
    elif src_path.is_file():
        matches = [] if _is_ignored(src_path) else [src_path]
    elif src_path.is_dir():
        matches = [p for p in src_path.rglob("*") if p.is_file() and not _is_ignored(p)]
    else:
        matches = []

    if not matches:
        return []

    destination_root = Path(temp_root) / dest.lstrip("/")
    destination_root.mkdir(parents=True, exist_ok=True)

    copied_files = []
    for file_path in sorted(matches):
        if src_path.is_dir() and not any(char in src for char in "*?[]"):
            rel_part = file_path.relative_to(src_path)
        else:
            rel_part = file_path.relative_to(context)
        target = destination_root / rel_part
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file_path, target)
        copied_files.append(str(file_path))
    return copied_files


def build_command(args):
    _ensure_runtime_dirs()
    docksmithfile_path = os.path.join(args.context, "Docksmithfile")
    if not os.path.exists(docksmithfile_path):
        print(f"Docksmithfile not found in {args.context}")
        sys.exit(1)

    with open(docksmithfile_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    # State
    instructions = []
    for idx, line in enumerate(lines):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split(maxsplit=1)
        if not parts:
            continue
        instr = parts[0].upper()
        arg = parts[1] if len(parts) > 1 else ""
        instructions.append((instr, arg, idx+1))

    print(f"Parsed {len(instructions)} instructions from Docksmithfile:")
    for i, (instr, arg, lineno) in enumerate(instructions, 1):
        print(f"Step {i}: {instr} {arg} (line {lineno})")

    # --- Build state ---
    cache = load_cache() if not args.no_cache else {}
    cache_miss = False
    prev_layer_digest = None

    if ':' in args.tag:
        target_name, target_tag = args.tag.split(':', 1)
    else:
        target_name, target_tag = args.tag, 'latest'

    previous_target_created = None
    try:
        previous_manifest = load_manifest(target_name, target_tag)
        previous_target_created = previous_manifest.get("created")
    except FileNotFoundError:
        previous_target_created = None

    workdir = ""
    env = {}
    cmd = []
    config_env = []
    config_workdir = ""
    config_cmd = []
    temp_root = os.path.join(args.context, "temp_fs")

    # Track layers for manifest
    manifest_layers = []
    base_layers = []
    image_name, image_tag = None, None

    for step_idx, (instr, arg, lineno) in enumerate(instructions, 1):
        print(f"\nStep {step_idx}/{len(instructions)} : {instr} {arg}")
        if instr == "FROM":
            # Parse image name and tag
            if ':' in arg:
                base_name, base_tag = arg.split(':', 1)
            else:
                base_name, base_tag = arg, 'latest'
            try:
                base_manifest = load_manifest(base_name, base_tag)
            except FileNotFoundError:
                print(f"\n[ERROR] Base image '{base_name}:{base_tag}' not found (line {lineno})")
                sys.exit(1)
            prev_layer_digest = base_manifest["digest"]
            print("(FROM: no cache status)")
            # Collect base layers for manifest
            base_layers = base_manifest["layers"]
            image_name = base_name
            image_tag = base_tag
            if os.path.exists(temp_root):
                shutil.rmtree(temp_root)
            os.makedirs(temp_root, exist_ok=True)
        elif instr == "COPY":
            # Parse src and dest
            try:
                src, dest = arg.split(maxsplit=1)
            except Exception:
                print(f"\n[ERROR] Invalid COPY syntax at line {lineno}: '{arg}'")
                sys.exit(1)
            # Resolve source files relative to context
            src_files = _copy_sources_into_temp_fs(args.context, src, dest, temp_root)
            if not src_files:
                print(f"\n[ERROR] No files matched for COPY at line {lineno}: '{src}'")
                sys.exit(1)
            # Collect hashes for cache key
            copy_src_files = src_files
            cache_key = compute_cache_key(
                prev_layer_digest or "",
                f"{instr} {arg}",
                workdir,
                env,
                copy_src_files
            )
            layer_digest = cache.get(cache_key)
            layer_file_path = _layer_artifact_path(layer_digest) if layer_digest else None
            layer_file_exists = layer_digest and os.path.exists(layer_file_path)
            if not args.no_cache and not cache_miss and layer_digest and layer_file_exists:
                print("[CACHE HIT]")
                prev_layer_digest = layer_digest
                manifest_layers.append({
                    "digest": f"sha256:{layer_digest}",
                    "size": os.path.getsize(layer_file_path),
                    "createdBy": f"COPY {arg}"
                })
            else:
                print("[CACHE MISS]")
                cache_miss = True
                layer_digest = _compute_layer_digest(cache_key, prev_layer_digest or "")
                layer_size = _write_layer_artifact(layer_digest, f"COPY {arg}", cache_key)
                prev_layer_digest = layer_digest
                manifest_layers.append({
                    "digest": f"sha256:{layer_digest}",
                    "size": layer_size,
                    "createdBy": f"COPY {arg}"
                })
                if not args.no_cache:
                    cache[cache_key] = layer_digest
        elif instr == "RUN":
            # Compute cache key
            cache_key = compute_cache_key(
                prev_layer_digest or "",
                f"{instr} {arg}",
                workdir,
                env
            )
            layer_digest = cache.get(cache_key)
            layer_file_path = _layer_artifact_path(layer_digest) if layer_digest else None
            layer_file_exists = layer_digest and os.path.exists(layer_file_path)
            if not args.no_cache and not cache_miss and layer_digest and layer_file_exists:
                print("[CACHE HIT]")
                prev_layer_digest = layer_digest
                manifest_layers.append({
                    "digest": f"sha256:{layer_digest}",
                    "size": os.path.getsize(layer_file_path),
                    "createdBy": f"RUN {arg}"
                })
            else:
                print("[CACHE MISS]")
                cache_miss = True
                # Actually run command in temp_fs (simulate, not real isolation yet)
                import subprocess
                run_env = os.environ.copy()
                run_env.update(env)
                run_cwd = os.path.join(temp_root, workdir.lstrip("/")) if workdir else temp_root
                os.makedirs(run_cwd, exist_ok=True)
                try:
                    result = subprocess.run(arg, shell=True, cwd=run_cwd, env=run_env, capture_output=True, text=True)
                    print(result.stdout)
                    if result.returncode != 0:
                        print(result.stderr)
                        print(f"\n[ERROR] RUN failed with exit code {result.returncode} at line {lineno}")
                        sys.exit(result.returncode)
                except Exception as e:
                    print(f"\n[ERROR] Exception running command at line {lineno}: {e}")
                    sys.exit(1)
                layer_digest = _compute_layer_digest(cache_key, prev_layer_digest or "")
                layer_size = _write_layer_artifact(layer_digest, f"RUN {arg}", cache_key)
                prev_layer_digest = layer_digest
                manifest_layers.append({
                    "digest": f"sha256:{layer_digest}",
                    "size": layer_size,
                    "createdBy": f"RUN {arg}"
                })
                if not args.no_cache:
                    cache[cache_key] = layer_digest
        elif instr == "WORKDIR":
            workdir = arg
            config_workdir = arg
        elif instr == "ENV":
            if '=' in arg:
                k, v = arg.split('=', 1)
                env[k] = v
                # For manifest config
                config_env = [f"{k}={v}" for k, v in sorted(env.items())]
        elif instr == "CMD":
            # Expect JSON array form
            import json as _json
            try:
                config_cmd = _json.loads(arg)
            except Exception:
                print(f"\n[ERROR] CMD must be a JSON array at line {lineno}: '{arg}'")
                sys.exit(1)
        else:
            print(f"\n[ERROR] Unknown instruction '{instr}' at line {lineno}")
            sys.exit(1)
    if not args.no_cache:
        save_cache(cache)

    # Manifest assembly
    from manifest import generate_manifest, save_manifest
    # Compose all layers: base + new
    all_layers = []
    if base_layers:
        all_layers.extend(base_layers)
    all_layers.extend(manifest_layers)

    # Name/tag from -t argument
    if args.tag:
        if ':' in args.tag:
            image_name, image_tag = args.tag.split(':', 1)
        else:
            image_name, image_tag = args.tag, 'latest'

    # Timestamp reuse: if all steps were cache hits, preserve original image created time.
    created = previous_target_created if not cache_miss and previous_target_created else None

    manifest = generate_manifest(
        name=image_name,
        tag=image_tag,
        env=config_env,
        cmd=config_cmd,
        workdir=config_workdir,
        layers=all_layers,
        created=created
    )
    save_manifest(manifest)
    print(f"\nSuccessfully built {manifest['digest']} {image_name}:{image_tag}")


def images_command(args):
    if not os.path.exists(IMAGES_DIR):
        print("No images found.")
        return
    rows = []
    for fname in os.listdir(IMAGES_DIR):
        if not fname.endswith(".json"):
            continue
        with open(os.path.join(IMAGES_DIR, fname), "r", encoding="utf-8") as f:
            m = json.load(f)

        name = m.get("name") or fname[:-5]
        tag = m.get("tag") or "latest"
        digest = m.get("digest")
        if not digest:
            legacy_hash = hashlib.sha256(json.dumps(m, sort_keys=True).encode("utf-8")).hexdigest()
            digest = f"sha256:{legacy_hash}"
        created = m.get("created") or "N/A"

        rows.append([
            name,
            tag,
            digest[7:19],
            created,
        ])
    if not rows:
        print("No images found.")
        return
    print_table(rows, ["NAME", "TAG", "ID", "CREATED"])


def rmi_command(args):
    if ":" in args.image:
        name, tag = args.image.split(":", 1)
    else:
        name, tag = args.image, "latest"
    try:
        manifest = load_manifest(name, tag)
    except FileNotFoundError:
        print(f"Error: Image {name}:{tag} not found.")
        return
    # Delete manifest file
    path_tagged = os.path.join(IMAGES_DIR, f"{name}_{tag}.json")
    path_legacy = os.path.join(IMAGES_DIR, f"{name}.json")
    if os.path.exists(path_tagged):
        os.remove(path_tagged)
    elif os.path.exists(path_legacy):
        os.remove(path_legacy)
    # Delete all layer files referenced in manifest
    for layer in manifest["layers"]:
        digest = layer.get("digest", "")
        if digest.startswith("sha256:"):
            digest = digest[7:]
        if digest:
            layer_path = _layer_artifact_path(digest)
            if os.path.exists(layer_path):
                os.remove(layer_path)
    print(f"Deleted image {name}:{tag}")


def main():
    parser = argparse.ArgumentParser(prog="docksmith")
    subparsers = parser.add_subparsers(dest="command")

    build = subparsers.add_parser("build")
    build.add_argument("-t", dest="tag", required=True, help="name:tag")
    build.add_argument("context", help="Build context directory")
    build.add_argument("--no-cache", action="store_true")
    build.set_defaults(func=build_command)

    images = subparsers.add_parser("images")
    images.set_defaults(func=images_command)

    rmi = subparsers.add_parser("rmi")
    rmi.add_argument("image", help="name:tag")
    rmi.set_defaults(func=rmi_command)

    args = parser.parse_args()
    if hasattr(args, "func"):
        args.func(args)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
