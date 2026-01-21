from pathlib import Path
import argparse
import numpy as np
from PIL import Image


def extract_channel_from_name(name: str):
    name = name.lower()
    if name.endswith("_chr.dds"):
        return 0
    if name.endswith("_chg.dds"):
        return 1
    if name.endswith("_chb.dds"):
        return 2
    return None


def batch_convert_specular(brightness: float):
    input_dir = Path("specular")
    output_dir = Path("roughness")

    print("Starting specular -> roughness conversion")

    if not input_dir.exists():
        print("ERROR: 'specular' folder not found")
        return

    output_dir.mkdir(parents=True, exist_ok=True)

    dds_files = [p for p in input_dir.iterdir() if p.suffix.lower() == ".dds"]

    if not dds_files:
        print("No DDS files found in 'specular' folder")
        return

    for dds_path in dds_files:
        print(f"Processing {dds_path.name}")

        try:
            with Image.open(dds_path) as img:
                img = img.convert("RGB")
                img_np = np.array(img).astype("float32") / 255.0
        except Exception as e:
            print(f"  Failed to load DDS: {e}")
            continue

        # Channel extraction if suffix is present
        channel = extract_channel_from_name(dds_path.name)
        if channel is not None:
            print(f"  Extracting channel {['R','G','B'][channel]}")
            ch = img_np[:, :, channel]
            img_np = np.stack((ch, ch, ch), axis=2)

        # Invert
        img_np = 1.0 - img_np

        # Brighten
        # Photoshop-style brightness scaling
        # Range: -150 .. +150
        ps_scale = brightness / 150.0 * 0.25
        img_np = np.clip(img_np + ps_scale, 0.0, 1.0)

        # Save
        out_path = output_dir / (dds_path.stem + "_rough.png")
        out_img = (img_np * 255.0 + 0.5).astype("uint8")
        Image.fromarray(out_img, "RGB").save(out_path)

        print(f"  Saved {out_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert specular DDS maps to roughness PNGs")
    parser.add_argument(
        "--brightness",
        type=float,
        default=0.3,
        help="Brightness added after inversion (default: 0.3)",
    )
    args = parser.parse_args()

    batch_convert_specular(args.brightness)
