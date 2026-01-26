# drag_to_octahedral.py
import sys
from pathlib import Path
import numpy as np
from PIL import Image


class LightspeedOctahedralConverter:
    @staticmethod
    def convert_dx_to_octahedral(image):
        normals = LightspeedOctahedralConverter._pixels_to_normals(image)
        octahedrals = LightspeedOctahedralConverter._convert_to_octahedral(normals)
        return LightspeedOctahedralConverter._octahedrals_to_pixels(octahedrals)

    @staticmethod
    def _pixels_to_normals(image):
        image = image[:, :, 0:3].astype("float32") / 255.0
        image = image * 2.0 - 1.0
        return image / np.linalg.norm(image, axis=2)[:, :, np.newaxis]

    @staticmethod
    def _octahedrals_to_pixels(octahedrals):
        image = np.floor(octahedrals * 255 + 0.5).astype("uint8")
        return np.pad(image, ((0, 0), (0, 0), (0, 1)), mode="constant")

    @staticmethod
    def _check_for_spherical_normals(path, image):
        mask = image[:, :, 2] < 128
        count = image[mask].shape[0]
        if count > 0:
            print(f"{path} contains {count} inward normals (z < 0). Flipping outward.")
        image[mask, 2] = 255 - image[mask, 2]

    @staticmethod
    def _convert_to_octahedral(image):
        abs_values = np.abs(image)
        snorm = image[:, :, 0:2] / np.expand_dims(abs_values.sum(2), axis=2)

        result = snorm.copy()
        result[:, :, 0] = snorm[:, :, 0] + snorm[:, :, 1]
        result[:, :, 1] = snorm[:, :, 0] - snorm[:, :, 1]

        return result * 0.5 + 0.5


def convert_dds_to_octahedral(dds_path: Path):
    if not dds_path.is_file() or dds_path.suffix.lower() != ".dds":
        print(f"Skip: {dds_path.name} (not a .dds file)")
        return

    print(f"Processing: {dds_path.name}")

    try:
        with Image.open(dds_path) as img:
            img = img.convert("RGB")
            img_np = np.array(img)
    except Exception as e:
        print(f"  Failed to load DDS: {e}")
        return

    LightspeedOctahedralConverter._check_for_spherical_normals(dds_path.name, img_np)

    oct_array = LightspeedOctahedralConverter.convert_dx_to_octahedral(img_np)

    out_path = dds_path.with_stem(dds_path.stem + "_normal_oth").with_suffix(".png")
    Image.fromarray(oct_array, "RGB").save(out_path)

    print(f"  Saved: {out_path.name}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Drag one or more .dds normal maps onto this .py file.")
        sys.exit(1)

    for arg in sys.argv[1:]:
        convert_dds_to_octahedral(Path(arg))

    print("Done.")