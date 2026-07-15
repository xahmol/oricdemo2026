# 3D vector/matrix math (vector3d.h)

Copied verbatim from Oscar64's own `include/gfx/vector3d.h`/`.c`
(`~/oscar64/include/gfx/`) — pure float and 4.12 fixed-point 2D/3D/4D
vector and matrix math (`Vector2/3/4`, `Matrix2/3/4`, `F12Vector3`/
`F12Matrix3`), with no bitmap/pixel-format assumptions, so it needs no
Oric-specific porting. Include `vector3d.h`; it auto-compiles `vector3d.c`
via `#pragma compile`.

Confirmed to compile cleanly under this project's `-dNOFLOAT` build flag —
that flag only disables `printf`/`scanf`'s `%f` format support (see
`~/oscar64/include/stdio.c`), not float arithmetic itself, and
`include/crt_math.c` already provides the float runtime this project's
default build needs regardless.

Used by `src/section_func3d.c` for its rotating 3D wireframe height-field
mesh: `mat4_make_perspective` builds the camera projection once at init,
`mat4_set_rotate_x`/`mat4_set_rotate_y`/`mat4_rmmul`/`mat4_mmul` combine
world/rotation/perspective transforms every rotation step, and
`vec3_project` turns each mesh vertex into 2D screen coordinates fed to
[hires.md](hires.md)'s `hb_line`-based drawing (via that section's own
local Bresenham helper — see its header comment for why not `hb_line`
directly). See Oscar64's own source (`~/oscar64/include/gfx/vector3d.h`)
for the full function list — every function is a straightforward, self-
documenting one-liner (`vec3_sum`, `mat4_mmul`, `mat4_set_rotate_x`, etc.).
