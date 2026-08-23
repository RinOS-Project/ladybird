# RinOS WebGL 1 backend

On `AK_OS_RINOS`, LibWeb builds its WebGL 1 implementation without ANGLE or
GLES. `WebGLRenderingContextImplRinGL` dispatches the supported WebGL 1 calls
to RinGL, and the private OS-Core bridge maps RinGL's versioned operations to
RinGPU and the caller-owned BGRA `Gfx::PaintingSurface` backing store.

The bridge owns no display device or backing pixels. A canvas resize creates a
fallible BGRA surface, optional D32 and S8 sidecars, then creates a
`RinWebGLRingPUSurfaceContext` and a RinGL context. The first submission
transitions the color image from `UNDEFINED`; present and browser-maintenance
clears synchronize the observed color/depth state back to the surface. This
prevents a failed allocation or device loss from becoming an infallible
WebContent abort.

RinGPU carries D32, D32S8, and S8 images and both combined and separate
depth/stencil render passes. WebGL-visible validation remains in LibWeb: a
distinct depth/stencil pair is rejected before submission, while a shared
D24S8 attachment is accepted. `getSupportedExtensions()` is empty on this
path, so an ANGLE/GLES extension is not exposed until RinGL implements it
natively. The integration does not introduce a GLES implementation.

The current RSH1 shader profile exposes linked `sampler2D`, scalar `float`,
and `vec2`, `vec3`, `vec4`, and vertex-stage `mat4` uniforms. The WebGL bridge
validates a uniform location against RinGL reflection before updating it,
forwards `uniform1f`/`uniform1fv`, `uniform2f`/`uniform2fv`,
`uniform3f`/`uniform3fv`, `uniform4f`/`uniform4fv`, and
`uniformMatrix4fv` to per-program RinGL state, and returns numbers or
`Float32Array` values from `getUniform` as required by the uniform type. The
matrix profile accepts WebGL column-major values with `transpose == false` for
`mat4 * vec4 attribute` vertex position transforms. The direct profile also
executes no-varying `vec2`/`vec3`/`vec4` locals, same-width `+`/`-`, unary
`-`, and vector/scalar `*`/`/` as RSH1 instructions, so uniform tinting and a
matrix-transformed position plus a vector offset do not rely on a browser-side
fallback. Common global `precision lowp`/`mediump`/`highp` declarations for
`float`, `int`, and `sampler2D` are grammar-validated and execute in RSH1
binary32; malformed declarations still fail shader compilation. Uniform arrays,
other matrix expressions, and other uniform types remain unavailable until
their native RinGL representation exists; they are not emulated by the browser
layer.

RinGL owns mutable numeric uniforms per stage. Consequently a vertex matrix
update preserves the independently validated fragment `sampler2D` module and
its typed RinGPU resources instead of re-lowering texture source through a
scalar-only path. The direct profile therefore executes a transformed textured
quad: `mat4 * attribute vec4` (or `mat4 * vec4(attribute vec2, 0, 1)`) writes
clip position, one `attribute vec2` is copied to one `varying vec2`, and the
fragment's `texture2D` reads that varying through its typed RinGPU image and
sampler resources. The matrix product and interpolation inputs are RSH1 work,
not an embedding-side pre-transform or a CPU texture fallback. Coordinate
arithmetic, multiple transformed varyings, and other matrix expressions remain
outside this bounded GLSL profile.

WebGL 2, ANGLE-specific extensions, complete WebGL conformance, and
product/QEMU browser evidence remain outside this enabled WebGL 1 slice. They
must not be advertised merely because the backend sources are linked.

For a focused target verification, build `RinGL` and the LibGfx/LibWeb WebGL
objects from the RinOS Ladybird build directory. The relevant CMake target
includes the OS-Core RinGPU surface, adapter, synchronization adapter, and
bridge sources exactly once through `RinGL::RinGL`.

The independent RinGL `textured-draw` test exercises the matching native
pipeline route with the public matrix setter, position/UV vertex attributes,
the varying texture shader, and typed image/sampler binding creation.
