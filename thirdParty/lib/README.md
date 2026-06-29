# Third-party binary libraries

Large prebuilt `.lib` files are intentionally not committed to this repository.
GitHub blocks normal pushes that contain files larger than 100 MB, and the local
Assimp debug library is larger than that limit.

For normal users, download the Windows installer from GitHub Releases.

For local development with the current CMake setup, place compatible MSVC x64
libraries here:

- `assimp-vc143-mtd.lib`
- `glfw3.lib`
- `zlibstaticd.lib`

The standalone Release package also expects static dependency builds under
`out/deps`, which is treated as generated local build output and is not tracked.
