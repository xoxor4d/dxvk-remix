# Remix AutoPBR API

AutoPBR collects which game normal / specular textures belong to which
material, dumps them and writes the inputs for the offline conversion
scripts. The runtime computes every hash; the game only names its textures.
It is controlled from the developer menu (Game Setup → Step 1: Categorize
Textures → AutoPBR): start / stop collecting, save / load / clear
associations, write the usda.

Implementation: [`rtx_fork_autopbr.cpp`](../src/dxvk/rtx_render/rtx_fork_autopbr.cpp),
[`rtx_fork_game_textures.cpp`](../src/dxvk/rtx_render/rtx_fork_game_textures.cpp).

## Feeding it

- **API materials:** chain `remixapi_MaterialInfoGameTexturesEXT` into
  `CreateMaterial` (see [`RemixApi.md`](RemixApi.md#materials)). The COLOR
  texture must hold its data (be filled) before `CreateMaterial`; it is
  resolved immediately. NORMAL / SPECULAR are resolved only while collecting:
  for materials created before collecting starts, the runtime holds the D3D9
  textures until the material is destroyed and resolves them at the next D3D9
  end of frame after collecting starts. Textures still without data are
  retried on the following frames for a while (the UI shows how many wait).
- **D3D9 draws:** `SetDrawGameTextures(&info)` before the draw(s),
  `SetDrawGameTextures(NULL)` after. The association key is the draw's
  material hash (the `mat_<hash>` a replacement targets). Without a `COLOR`
  entry the draw's bound colormap is used.

Only send the D3D9 draw info while collecting (see below); the runtime drops
it otherwise.

## Game state (`GetGameValue`)

| Key | Values | Notes |
|---|---|---|
| `__autopbr.collecting` | `"1"` / `"0"` | Written by the runtime when collecting starts / stops. Missing until the first change. Plugins poll it (e.g. once per frame) and treat it as read-only. |

## Options (`SetConfigVariable` / `rtx.conf`)

| Key | Type | Default | Notes |
|---|---|---|---|
| `rtx.autopbr.exportsPerFrame` | int | `8` | Texture dumps issued per frame. |
| `rtx.autopbr.autosaveInterval` | int | `32` | Save `associations.json` after this many new or updated associations; `0` disables. |

## Output

Fixed folder `<game directory>/rtx-remix/imgdump/`:

- `color/`, `normal/`, `specular/` — `<HASH>.dds` (16 uppercase hex digits),
  one per image hash, mips included. Files already present are skipped.
  Formats with a D3D9 view swizzle (L8, A8L8, A8, X8R8G8B8) are written as
  RGBA8 the way the shader sees them; other formats are written as stored.
  Normal maps are not reconstructed or re-swizzled.
- `associations.json` — schema version 1 (GTAIV AutoPBR layout) plus
  `material_name` and `material_hash`. Written atomically off the render
  thread. Starting a collection merges an existing file first, so a crashed
  session resumes; a file that cannot be parsed is renamed to
  `associations.json.bak` (or `.bak1`, `.bak2`, ...) first, and collecting does
  not start if that rename fails. Unsaved associations are saved when the
  device or the API shuts down. Reading the file and scanning the dump folders
  on Start / Load happens on the UI thread and can cause a short hitch.
- `comp_autoconvert.usda` — one `over "mat_<MATERIAL_HASH>"` per material with
  a normal and/or specular texture, pointing at
  `./assets/autoconv/<HASH>_normal_oth.dds` / `<HASH>_rough.dds`, with the game material name as `nickname`.
