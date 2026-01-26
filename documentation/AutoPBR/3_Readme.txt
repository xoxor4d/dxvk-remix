Remix AutoPBR
===============================

How to use (game):

Remix:
- Use this Branch of the remix runtime paired with the AutoPBR branch of the GTAIV CompMod
- Open the Remix menu, go to "Game Setup" -> Step 1 -> Auto PBR Conversation
- Enable All 3 Checkboxes
- ! If you have previous associations and want to continue from there, MAKE SURE to "Load Associations"

CompMod:
- Open the F4 menu and go to the Dev tab -> Other Settings
- Enable "Provide AutoPBR Information

The runtime will start to dump textures and build associations as they come in. 
Only things that are rendered will be dumped.

---------------------------------------------

Requirements:
- Python 3.8+
- NVTT (for converting PNG -> BC5 DDS)
- Python dependencies:
    `pip install -r 3_requirements.txt`

Short description:
- copy all the following mentioned files to: "rtx-remix\imgdump"
- run `python 1_extractHeight.py`
- run `python 1_normal2octahedral.py`
- run `python 1_specular2roughness.py --brightness 30`
- run `2_height_to_remix_dds_conv.bat`
- run `2_octa_to_remix_dds_conv.bat`
- run `2_rough_to_remix_dds_conv.bat`
- grab the "autoconv" folder and put it into "YOUR_REMIX_MOD/assets/"
- grab "comp_autoconvert.usda" and place it into your "YOUR_REMIX_MOD" directory
- add `@./comp_autoconvert.usda@` as the lowest sublayer to your mod.usda 

--------------------------------------------------------

1_extractHeight.py:
    This tool converts DirectX (DX9) normal maps stored as DDS files 
    with a heightmap stored in the alpha channel into height maps saved as PNGs.

    Folder structure:
    - Put input normal maps (.dds) in the "height" folder
    - Converted PNGs will be written to the "height_out" folder

    Usage:
    - Run the script:
      `python 1_extractHeight.py`

--------------------------------------------------------

1_normal2octahedral.py:
    This tool converts DirectX (DX9) normal maps stored as DDS files into
    hemispherical octahedral normal maps saved as PNGs.

    Folder structure:
    - Put input normal maps (.dds) in the "normal" folder
    - Converted PNGs will be written to the "octahedral" folder

    Usage:
    - Run the script:
      `python 1_normal2octahedral.py`

--------------------------------------------------------

1_specular2roughness.py:
    This tool converts DirectX (DX9) specular maps stored as DDS files into
    roughness maps saved as PNGs. The script will extract single grayscale textures 
    when the input texture names end on "_chR", "_chG", "_chB".

    Folder structure:
    - Put input specular maps (.dds) in the "specular" folder
    - Converted PNGs will be written to the "roughness" folder

    Usage:
    - Run the script:
      `python 1_specular2roughness.py --brightness 30`
    
    - Use --brightness 30 (-100 to +100) to automatically darken or brighten the image

--------------------------------------------------------

Batch files [2_height_to_remix_dds_conv, 2_octa_to_remix_dds_conv, 2_rough_to_remix_dds_conv]:
    Use NVTT to convert the png files to remix compatible formats.
    They place the output into the "autoconv" folder.
