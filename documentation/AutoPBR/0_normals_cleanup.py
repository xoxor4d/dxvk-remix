import shutil
import subprocess
import tempfile
import logging
from pathlib import Path

import numpy as np
from PIL import Image

# ----------------------------------------------------------------------
# logging
# ----------------------------------------------------------------------

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)

log = logging.getLogger("normals_cleanup")

# ----------------------------------------------------------------------
# paths
# ----------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).resolve().parent
NORMAL_DIR = SCRIPT_DIR / "normal"
PROCESSED_DIR = SCRIPT_DIR / "processed_normals"
LIST_FILE = SCRIPT_DIR / "0_normals_cleanup_list.txt"

SECTION_FOLDER_MAP = {
    "heightmaps_normal": "height",
}

# ----------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------

def parse_sections(path):
    sections = {}
    current = None

    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue

        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1]
            sections[current] = []
        elif current:
            sections[current].append(line.upper())

    return sections

def nvcompress_dxt5(src_tga, dst_dds):
    log.debug(f"nvcompress: {src_tga.name} -> {dst_dds.name}")
    subprocess.run(
        ["nvcompress", "-bc3", str(src_tga), str(dst_dds)],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

# ----------------------------------------------------------------------
# file moving
# ----------------------------------------------------------------------

def move_files(sections):
    log.info("Sorting files into section folders")

    files_by_stem = {}
    for f in NORMAL_DIR.iterdir():
        if f.is_file():
            files_by_stem.setdefault(f.stem.upper(), []).append(f)

    for section, names in sections.items():
        dest_name = SECTION_FOLDER_MAP.get(section, f"_{section}")
        dest_dir = SCRIPT_DIR / dest_name
        dest_dir.mkdir(exist_ok=True)

        is_heightmap = (section == "heightmaps_normal")

        for name in names:
            files = files_by_stem.get(name, [])
            for f in files[:]:
                if not f.exists():
                    files.remove(f)
                    continue

                if is_heightmap:
                    log.info(f"Copy (heightmap): {f.name} -> {dest_dir.name}")
                    shutil.copy2(str(f), dest_dir / f.name)
                else:
                    log.info(f"Move: {f.name} -> {dest_dir.name}")
                    shutil.move(str(f), dest_dir / f.name)
                    files.remove(f)

# ----------------------------------------------------------------------
# image processing
# ----------------------------------------------------------------------

def process_alpha_red_channel(folder):
    log.info("Processing _alpha_red_channel")

    for dds in folder.glob("*.dds"):
        log.info(f"Alpha→Red: {dds.name}")

        img = Image.open(dds).convert("RGBA")
        arr = np.array(img, copy=True)

        alpha = arr[:, :, 3].copy()
        arr[:, :, 0] = alpha   # R = A
        arr[:, :, 2] = 255     # B = white
        arr[:, :, 3] = 255     # remove alpha

        out = Image.fromarray(arr, "RGBA")

        with tempfile.TemporaryDirectory() as tmp:
            tga = Path(tmp) / "tmp.tga"
            out.save(tga)
            nvcompress_dxt5(tga, dds)

def bump_to_normal(folder, strength=2.0):
    log.info("Processing _bumpmaps → normal maps")

    for dds in folder.glob("*.dds"):
        log.info(f"Bump→Normal: {dds.name}")

        img = Image.open(dds).convert("L")
        h = np.array(img, dtype=np.float32, copy=True) / 255.0

        dx = np.zeros_like(h)
        dy = np.zeros_like(h)

        dx[:, 1:-1] = h[:, :-2] - h[:, 2:]
        dy[1:-1, :] = h[:-2, :] - h[2:, :]

        nx = dx * strength
        ny = dy * strength
        nz = np.ones_like(h)

        length = np.sqrt(nx * nx + ny * ny + nz * nz)
        nx /= length
        ny /= length
        nz /= length

        normal = np.zeros((h.shape[0], h.shape[1], 4), dtype=np.uint8)
        normal[:, :, 0] = ((nx * 0.5 + 0.5) * 255).astype(np.uint8)
        normal[:, :, 1] = ((ny * 0.5 + 0.5) * 255).astype(np.uint8)
        normal[:, :, 2] = ((nz * 0.5 + 0.5) * 255).astype(np.uint8)
        normal[:, :, 3] = 255

        out = Image.fromarray(normal, "RGBA")

        with tempfile.TemporaryDirectory() as tmp:
            tga = Path(tmp) / "tmp.tga"
            out.save(tga)
            nvcompress_dxt5(tga, dds)

# ----------------------------------------------------------------------
# cleanup
# ----------------------------------------------------------------------

def move_to_processed_and_remove(folder):
    if not folder.exists():
        return

    PROCESSED_DIR.mkdir(exist_ok=True)

    log.info(f"Moving processed files from {folder.name} -> processed_normals/")

    for f in folder.iterdir():
        if f.is_file():
            log.info(f"Processed: {f.name}")
            shutil.move(str(f), PROCESSED_DIR / f.name)

    folder.rmdir()
    log.info(f"Removed folder: {folder.name}")

# ----------------------------------------------------------------------
# main
# ----------------------------------------------------------------------

def main():
    log.info("Starting normals cleanup pipeline")

    sections = parse_sections(LIST_FILE)
    move_files(sections)

    alpha_dir = SCRIPT_DIR / "_alpha_red_channel"
    bump_dir = SCRIPT_DIR / "_bumpmaps"

    if alpha_dir.exists():
        process_alpha_red_channel(alpha_dir)

    if bump_dir.exists():
        bump_to_normal(bump_dir)

    move_to_processed_and_remove(alpha_dir)
    move_to_processed_and_remove(bump_dir)

    log.info("Pipeline finished successfully")

if __name__ == "__main__":
    main()
