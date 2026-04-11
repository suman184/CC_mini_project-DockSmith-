import argparse
import os
import sys
import json
from cache import compute_cache_key, load_cache, save_cache
from manifest import generate_manifest, save_manifest, load_manifest
from datetime import datetime

CACHE_INDEX_PATH = os.path.expanduser("~/.docksmith/cache/index.json")
IMAGES_DIR = os.path.expanduser("~/.docksmith/images/")

# Helper to print table

def print_table(rows, headers):
    col_widths = [max(len(str(x)) for x in col) for col in zip(*([headers] + rows))]
    fmt = "  ".join([f"{{:<{w}}}" for w in col_widths])
    print(fmt.format(*headers))
    print("  ".join(["-" * w for w in col_widths]))
    for row in rows:
        print(fmt.format(*row))


def build_command(args):
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

    workdir = ""
    env = {}
    cmd = []
    config_env = []
    config_workdir = ""
    config_cmd = []
    cache_keys = []

    import glob
    import shutil

    # Track layers for manifest
    manifest_layers = []
    base_layers = []
    base_created = None
    image_name, image_tag = None, None

    for step_idx, (instr, arg, lineno) in enumerate(instructions, 1):
        print(f"\nStep {step_idx}/{len(instructions)} : {instr} {arg}")
        if instr == "FROM":
            # Parse image name and tag
            if ':' in arg:
                base_name, base_tag = arg.split(':', 1)
            else:
                base_name, base_tag = arg, 'latest'
            from manifest import load_manifest
            try:
                base_manifest = load_manifest(base_name, base_tag)
            except FileNotFoundError:
                print(f"\n[ERROR] Base image '{base_name}:{base_tag}' not found (line {lineno})")
                sys.exit(1)
            prev_layer_digest = base_manifest["digest"]
            print("(FROM: no cache status)")
            # Collect base layers for manifest
            base_layers = base_manifest["layers"]
            base_created = base_manifest["created"]
            image_name = base_name
            image_tag = base_tag
        elif instr == "COPY":
            # Parse src and dest
            try:
                src, dest = arg.split(maxsplit=1)
            except Exception:
                print(f"\n[ERROR] Invalid COPY syntax at line {lineno}: '{arg}'")
                sys.exit(1)
            # Expand globs in src (relative to context)
            src_glob = os.path.join(args.context, src)
            src_files = sorted([f for f in glob.glob(src_glob, recursive=True) if os.path.isfile(f)])
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
            cache_keys.append(cache_key)
            layer_digest = cache.get(cache_key)
            layer_file_path = os.path.expanduser(f"~/.docksmith/layers/{layer_digest}.tar") if layer_digest else None
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
                # Actually copy files into temp_fs (simulate, not real layer yet)
                temp_dest = os.path.join("temp_fs", dest.lstrip("/"))
                os.makedirs(temp_dest, exist_ok=True)
                for f in src_files:
                    rel = os.path.relpath(f, args.context)
                    target = os.path.join(temp_dest, os.path.basename(rel))
                    shutil.copy2(f, target)
                prev_layer_digest = "new_layer_digest_placeholder"
                # Placeholder for new layer (real digest/size to be set after layer creation)
                manifest_layers.append({
                    "digest": f"sha256:{prev_layer_digest}",
                    "size": 0,
                    "createdBy": f"COPY {arg}"
                })
                if not args.no_cache:
                    cache[cache_key] = prev_layer_digest
        elif instr == "RUN":
            # Compute cache key
            cache_key = compute_cache_key(
                prev_layer_digest or "",
                f"{instr} {arg}",
                workdir,
                env
            )
            cache_keys.append(cache_key)
            layer_digest = cache.get(cache_key)
            layer_file_path = os.path.expanduser(f"~/.docksmith/layers/{layer_digest}.tar") if layer_digest else None
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
                run_cwd = os.path.join(os.getcwd(), "temp_fs", workdir.lstrip("/")) if workdir else os.path.join(os.getcwd(), "temp_fs")
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
                prev_layer_digest = "new_layer_digest_placeholder"
                manifest_layers.append({
                    "digest": f"sha256:{prev_layer_digest}",
                    "size": 0,
                    "createdBy": f"RUN {arg}"
                })
                if not args.no_cache:
                    cache[cache_key] = prev_layer_digest
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

    # Timestamp reuse: if all steps were cache hits, reuse base_created
    created = base_created if not cache_miss and base_created else None

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
        if not fname.endswith(".json"): continue
        with open(os.path.join(IMAGES_DIR, fname), "r", encoding="utf-8") as f:
            m = json.load(f)
        rows.append([
            m["name"],
            m["tag"],
            m["digest"][7:19],
            m["created"]
        ])
    if not rows:
        print("No images found.")
        return
    print_table(rows, ["NAME", "TAG", "ID", "CREATED"])


def rmi_command(args):
    name, tag = args.image.split(":", 1)
    try:
        manifest = load_manifest(name, tag)
    except FileNotFoundError:
        print(f"Error: Image {name}:{tag} not found.")
        return
    # Delete manifest file
    path = os.path.join(IMAGES_DIR, f"{name}_{tag}.json")
    os.remove(path)
    # Delete all layer files referenced in manifest
    for layer in manifest["layers"]:
        layer_path = layer.get("path")
        if layer_path and os.path.exists(layer_path):
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
