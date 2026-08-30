# dxvk-remix

- **Fork-touchpoint pattern** — fork logic is extracted into
  dedicated `rtx_fork_*.cpp` modules, with one-line dispatches in
  upstream files. Reduces NVIDIA-rebase pain by ~54% (measured) and
  makes the fork's surface area auditable. See
  [`docs/fork-touchpoints.md`](docs/fork-touchpoints.md) for the
  authoritative inventory.
- **PR template fridge-list reminder** keeps the discipline honest.

## Contributing

Contributions are welcome. Whether you write Remix plugins, ship a
game integration, or want to make this fork better — start with the
contribution guide:

**[`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md)** covers setup,
build, fork-touchpoint discipline, code style, and PR submission.

The short version:

1. Fork [`RemixProjGroup/dxvk-remix`](https://github.com/RemixProjGroup/dxvk-remix).
2. Branch on your fork — any name is fine.
3. Keep PRs small and focused.
4. Build clean (release flavor, exit code 0, zero errors).
5. Open a PR against canonical's `main` branch.
6. Add yourself to `src/dxvk/imgui/dxvk_imgui_about.cpp` under
   "Github Contributors".

### Requirements:
1. Windows 10 or 11
2. [Git](https://git-scm.com/download/win)
3. [Visual Studio ](https://visualstudio.microsoft.com/vs/older-downloads/)
    - VS 2019 is tested
    - VS 2022 may also work, but it is not actively tested
    - Note that our build system will always use the most recent version available on the system
4. [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/sdk-archive/)
    - 10.0.19041.0 is tested
5. [Meson](https://mesonbuild.com/)
    - 1.8.2 has been tested
    - Follow [instructions](https://mesonbuild.com/SimpleStart.html#installing-meson) on how to install and reboot the PC before moving on (Meson will indicate as much)
6. [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows)
    - 1.4.313.2 or newer
    - You may need to uninstall previous SDK if you have an old version
7. [Python](https://www.python.org/downloads/)
    - 3.9 or newer
    - Ensure you are using python installed from the link above and not from the Microsoft Store
    - Python is required by developer build tooling; the packaged RTX Remix Runtime does not link against Python.
8. [DirectX Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=35)
    - Latest version should work.
    - This includes d3d9x*.dll which are required to run the game
    - May already be installed if you have D3D9 games installed

#### Additional notes:
- If dependency paths change (for example, after installing a new Vulkan SDK), reconfigure the affected build from the repository root, such as `meson setup --reconfigure _Comp64Release`.

## Quick build

Detailed requirements and walkthrough live in
[`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md). The compressed
version, assuming you have Visual Studio 2019 (with the v142
toolchain), the Windows SDK, Meson 1.8.2+, the Vulkan SDK
1.4.313.2+, and Python 3.9+:

```powershell
git clone --recursive https://github.com/<your-fork>/dxvk-remix.git
cd dxvk-remix
.\scripts\build.ps1
```

3. Make sure PowerShell scripts are enabled
    - One-time system setup: run `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned` in an elevated PowerShell prompt, then close and reopen any existing PowerShell prompts
	
4. To generate and build dxvk-remix project:
    - Right Click on `dxvk-remix\build_dxvk_all_ninja.ps1` and select "Run with Powershell"
    - If that fails or has problems, run the build manually in a way you can read the errors:
        - open a windows file explorer to the `dxvk-remix` folder
        - remove only the generated configuration that failed, such as `_Comp64Debug/`; remove `_vs/` as well only if the generated Visual Studio solution must be recreated
        - type `cmd` in the address bar to open a command line window in that folder.
        - copy and paste `powershell -command "& .\build_dxvk_all_ninja.ps1"` into the command line, then press enter
    - Optional flags:
        - `-SkipApics` — skip downloading game test captures (requires auth token)
    - Examples:
        ```powershell
        .\build_dxvk_all_ninja.ps1
        .\build_dxvk_all_ninja.ps1 -SkipApics
        ```
    - This will build all 3 configurations of dxvk-remix project inside subdirectories of the build tree:
        - **_Comp64Debug** - full debug instrumentation, runtime speed may be slow
        - **_Comp64DebugOptimized** - partial debug instrumentation (i.e. asserts), runtime speed is generally comparable to that of release configuration
        - **_Comp64Release** - fastest runtime
    - This will generate a project in the **_vs** subdirectory
    - This script builds the officially supported x64 targets. ARM64 and ARM64EC configurations are compile-tested in CI but are not part of this local build workflow.

To build the 32-bit-to-64-bit bridge separately, use the fork-side
bridge wrapper `.\bridge\scripts\build.ps1` (builds the x64 server
and x86 client + launcher into `bridge/_output/`).

## Remix API

If you're integrating Remix into a game with available source, you
can either use the D3D9 surface directly (Remix's `d3d9.dll`
implements D3D9) or program against the Remix C API to push game
data into the renderer. Start with
[`docs/RemixSDK.md`](docs/RemixSDK.md) for setup and the mental
model, then see [`docs/RemixApi.md`](docs/RemixApi.md) for the full
API reference. The C header is
[`public/include/remix/remix_c.h`](public/include/remix/remix_c.h),
with a type-safe C++ wrapper at
[`public/include/remix/remix.h`](public/include/remix/remix.h).

3. Reconfigure and rebuild each configuration you use so Meson reloads **gametargets.conf**. For example:
    ```powershell
    meson setup --reconfigure _Comp64Release
    meson compile -C _Comp64Release
    ```
    The build deploys binaries to the game directories specified in **gametargets.conf**.

- [Anti-Culling System](docs/AntiCullingSystem.md)
- [Cloud System](docs/CloudSystem.md)
- [Contributing Guide](docs/CONTRIBUTING.md)
- [Contributing Style Guide](docs/CONTRIBUTING-style-guide.md)
- [Foliage System](docs/FoliageSystem.md)
- [Fork Touchpoints](docs/fork-touchpoints.md)
- [GPU Print](docs/GpuPrint.md)
- [Opacity Micromap](docs/OpacityMicromap.md)
- [Remix API (hub reference)](docs/RemixApi.md)
- [Remix API Changelog](docs/RemixApiChangelog.md)
- [Remix API Surface (auto-generated)](RemixApiSurface.md)
- [Remix Config](docs/RemixConfig.md)
- [Remix Logic](docs/RemixLogic.md)
- [Remix SDK Setup](docs/RemixSDK.md)
- [Remix Sky API](docs/RemixSkyAPI.md)
- [Rtx Options](RtxOptions.md)
- [Terrain System](docs/TerrainSystem.md)
- [Unit Test](docs/UnitTest.md)

## Team

- [Kim2091](https://github.com/Kim2091) — project lead and lead maintainer
- [CR](https://github.com/sambow23) — maintainer
- [TheGreatHMMMM](https://github.com/TheGreatHMMMM) — contributor
- [Gokuwashere](https://github.com/BrunchyChineapple) — contributor

## Credits

Remix Plus stands on the work of:

- [DXVK](https://github.com/doitsujin/dxvk) — D3D9 → Vulkan
  translation layer.
- [NVIDIA `dxvk-remix`](https://github.com/NVIDIAGameWorks/dxvk-remix) —
  path-traced remastering fork of DXVK.
- The **gmod-rtx community fork** — origin of most of the SDK
  extensions Remix Plus carries.

Thanks to all the contributors whose work makes this possible.
