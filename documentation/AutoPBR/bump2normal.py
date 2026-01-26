import sys
import subprocess
import tempfile
from pathlib import Path
import numpy as np
from PIL import Image

# Change this path to where your nvcompress.exe actually is
NVCOMPRESS_PATH = Path("nvcompress.exe")   # ← edit this !!

def bump_to_dx9_normal(bump_path: Path, strength: float = 2.0):
    if not bump_path.is_file():
        print(f"  Not a file: {bump_path.name}")
        return

    print(f"Processing: {bump_path.name}")

    try:
        img = Image.open(bump_path).convert("L")
        h = np.array(img, dtype=np.float32) / 255.0
    except Exception as e:
        print(f"  Failed to open image: {e}")
        return

    dx = np.zeros_like(h)
    dy = np.zeros_like(h)

    dx[:, 1:-1] = h[:, :-2] - h[:, 2:]
    dy[1:-1, :] = h[:-2, :] - h[2:, :]

    nx = dx * strength
    ny = dy * strength
    nz = np.ones_like(h)

    length = np.sqrt(nx*nx + ny*ny + nz*nz + 1e-8)
    nx /= length
    ny /= length
    nz /= length

    normal = np.zeros((h.shape[0], h.shape[1], 4), dtype=np.uint8)
    normal[:,:,0] = ((nx * 0.5 + 0.5) * 255).astype(np.uint8)   # X → Red
    normal[:,:,1] = ((ny * 0.5 + 0.5) * 255).astype(np.uint8)   # Y → Green (DX style)
    normal[:,:,2] = ((nz * 0.5 + 0.5) * 255).astype(np.uint8)   # Z → Blue
    normal[:,:,3] = 255                                         # Alpha = 1

    out_img = Image.fromarray(normal, "RGBA")

    try:
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_tga = Path(tmpdir) / "temp.tga"
            out_img.save(tmp_tga)

            out_dds = bump_path.with_stem(bump_path.stem + "_dx9").with_suffix(".dds")
            subprocess.run([
                str(NVCOMPRESS_PATH),
                "-DXT5",
                "-silent",
                str(tmp_tga),
                str(out_dds)
            ], check=True, capture_output=True)

        print(f"  Saved: {out_dds.name}")
    except Exception as e:
        print(f"  nvcompress failed: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Drag & drop one or more bumpmap files onto this script.")
        sys.exit(1)

    for arg in sys.argv[1:]:
        bump_to_dx9_normal(Path(arg))

    print("\nFinished.")