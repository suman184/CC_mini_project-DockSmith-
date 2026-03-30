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
    # This is a stub for the build logic, which would parse the Docksmithfile,
    # execute instructions, handle cache, and generate the manifest.
    # The actual build logic should be implemented here, using the cache and manifest modules.
    print("Build logic not implemented in this stub.")


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
