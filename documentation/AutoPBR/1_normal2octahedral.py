"""
* SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
* SPDX-License-Identifier: MIT
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
"""
from pathlib import Path

import numpy as np
from PIL import Image


# Converts either OpenGL or DirectX style normal maps to RTX Remix compatible Hemispherical Octahedral maps.
#
# Note that normals pointing in to the surface are not physically possible, and are not supported by RTX Remix.
#   Any images with inward pointing normals will generate a warning and will be flipped to point outwards.
#
# There is a good explanation of DirectX vs OpenGL normal maps at
#   https://www.texturecan.com/post/3/DirectX-vs-OpenGL-Normal-Map/
#
# To use, call this from python as
# `LightspeedOctahedralConverter.convert_dx_file_to_octahedral("input_dx_normal_map.png", "output_octahedral_map.png")`
#
# To then load these into RTX Remix, you can convert it to a DDS file using
#   https://developer.nvidia.com/nvidia-texture-tools-exporter
#   Use BC5 compression, and the flag --no-mip-gamma-correct


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
            print(
                f"{path} contains {count} inward-pointing normals (z < 0). "
                "Flipping them outward for hemispherical support."
            )
        image[mask, 2] = 255 - image[mask, 2]

    @staticmethod
    def _convert_to_octahedral(image):
        abs_values = np.abs(image)
        snorm = image[:, :, 0:2] / np.expand_dims(abs_values.sum(2), axis=2)

        result = snorm.copy()
        result[:, :, 0] = snorm[:, :, 0] + snorm[:, :, 1]
        result[:, :, 1] = snorm[:, :, 0] - snorm[:, :, 1]

        return result * 0.5 + 0.5


def batch_convert_normals():
    input_dirs = [Path("normal"), Path("processed_normals")]
    output_dir = Path("octahedral")

    print("Starting conversion...")

    output_dir.mkdir(parents=True, exist_ok=True)

    dds_files = []
    for dir_path in input_dirs:
        if not dir_path.exists():
            print(f"  Skipping: '{dir_path}' not found")
            continue
        dds_files.extend(
            p for p in dir_path.iterdir() if p.suffix.lower() == ".dds"
        )

    if not dds_files:
        print("No DDS files found in any input folder")
        return

    for dds_path in dds_files:
        print(f"Processing {dds_path.name} ({dds_path.parent.name})")

        try:
            with Image.open(dds_path) as img:
                img = img.convert("RGB")
                img_np = np.array(img)
        except Exception as e:
            print(f"  Failed to load: {e}")
            continue

        LightspeedOctahedralConverter._check_for_spherical_normals(
            dds_path.name, img_np
        )

        oct_img = LightspeedOctahedralConverter.convert_dx_to_octahedral(img_np)
        out_path = output_dir / (dds_path.stem + "_normal_oth.png")
        Image.fromarray(oct_img, "RGB").save(out_path)

        print(f"  Saved {out_path}")

if __name__ == "__main__":
    batch_convert_normals()