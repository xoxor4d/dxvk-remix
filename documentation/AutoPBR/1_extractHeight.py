from pathlib import Path
import numpy as np
from PIL import Image


def batch_convert_height():
    input_dir = Path("height")
    output_dir = Path("height_out")

    print("Starting height map conversion")

    if not input_dir.exists():
        print("ERROR: 'height' folder not found")
        return

    output_dir.mkdir(parents=True, exist_ok=True)

    dds_files = [p for p in input_dir.iterdir() if p.suffix.lower() == ".dds"]

    if not dds_files:
        print("No DDS files found in 'height' folder")
        return

    for dds_path in dds_files:
        print(f"Processing {dds_path.name}")

        try:
            with Image.open(dds_path) as img:
                img = img.convert("RGBA")
                img_np = np.array(img)
        except Exception as e:
            print(f"  Failed to load DDS: {e}")
            continue

        # Extract alpha channel
        alpha = img_np[:, :, 3]

        # Replicate alpha into RGB
        rgb = np.stack((alpha, alpha, alpha), axis=2)

        out_path = output_dir / f"{dds_path.stem}_height.png"
        Image.fromarray(rgb, "RGB").save(out_path)

        print(f"  Saved {out_path}")


if __name__ == "__main__":
    batch_convert_height()
