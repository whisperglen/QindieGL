# You Are Empty compatibility status

## Phase A — instrumentation

Status: **complete in source, built, and smoke-tested**. The Phase A acceptance
criterion is met: the observed startup crash is correlated to an API pointer,
the exact `ds2render.dll` caller RVA, and the preceding GL/WGL event sequence.

YAE does not reach the menu yet. The remaining hardware-detection failure is a
Phase B startup/capability issue, not an instrumentation failure.

### Implemented diagnostics

- Structured logging with `0=ERROR`, `1=WARN`, `2=INFO`, `3=DEBUG`, and
  `4=TRACE`; INFO is the default.
- Frame and per-frame draw IDs. A frame advances only after a successful real
  D3D9 `Present`; `D3DERR_WASSTILLDRAWING` does not create a false frame.
- Requested-procedure accounting split into implemented, unknown, disabled,
  stubbed-requested, and stubbed-actually-invoked categories.
- Standards-compatible `wglGetProcAddress`: intentionally disabled and unknown
  procedures return `NULL` instead of a shared fake function pointer.
- Startup capability block containing executable/architecture, exposed OpenGL
  identity, D3D adapter/caps, diagnostic settings, and important advertised GL
  and WGL extensions.
- INFO events for the texture-unit and ARB-program limits used during hardware
  detection.
- Shutdown summary containing frames/draws, procedure categories, unsupported
  enum origins, D3D failures, resets, PBuffers, ARB program totals, and VBO
  allocation totals.
- D3D failures record HRESULT, API call, frame, and draw origin. GL errors retain
  the wrapper function that produced them until `glGetError` consumes the error.
- A fixed 256-event crash ring. Unhandled-exception reports include exception
  parameters, x86 registers, raw stack words, executable stack addresses as
  `module+RVA`, current state, and recent GL/WGL/D3D events.
- `DebugMaxDrawCall`, `DebugDumpFrame`, and `DebugDumpDraw` support draw binary
  search and a selected-draw state dump.

### Reliable GLIntercept workflow

`tools/glintercept/` contains validated x86 FULL and SUMMARY configurations plus
`Prepare-YAETrace.ps1`.

The preparation script:

- verifies that both GLIntercept and QindieGL are x86;
- verifies required definition/plugin files;
- refuses to overwrite an existing game setup;
- copies the official GL function-definition corpus beside the game;
- installs a small QindieGL overlay for DS2's historical
  `glMultiTexCoord4sdARB` spelling.

GLIntercept 1.3.4 requires the first line of every definition file to be a
unique `#define`; the overlay now satisfies this parser requirement. QindieGL
also exports `wglSwapMultipleBuffers`, which GLIntercept requires while mapping
the OpenGL 1.1/WGL surface.

The captured hardware-detection trace is clean:

```text
FunctionParser errors: 0
Unknown function diagnostics: 0
Unknown/??? parameters: 0
```

### Smoke-test configuration

Game: `H:\YAE\Original\You Are Empty\YOU_ARE_EMPTY.EXE`

```text
screen_w=1920
screen_h=1080
fullscreen=1
refresh=60
hdr=0
use_shaders=0
use_normalmaps=0
```

Final installed QindieGL profile:

```ini
[Settings]
LogLevel = 2
CrashDiagnostics = 1
DebugMaxDrawCall = -1
DebugDumpFrame = -1
DebugDumpDraw = -1
EnableARBProgramsStub = 0

[Extensions]
GL_ARB_vertex_buffer_object = 0
```

The 3440x1440 attempt failed before renderer initialization with
`Can't Initialize Screen Resolution`. The 1920x1080 monitor/config removes that
separate test-environment blocker.

### Observed startup blocker for Phase B

With the truthful baseline, ARB vertex/fragment programs are not advertised and
their procedures return `NULL`. DS2 correctly detects the missing fragment
extension, but then unconditionally calls its null `glGetProgramivARB` slot.

The enhanced crash report resolved the otherwise unhelpful `EIP=0` failure to:

```text
Exception: 0xC0000005, execute address 0
Frame: 0
Draw: 0
Caller: ds2render.dll+0x1FB7E
API slot: glGetProgramivARB
Arguments: GL_FRAGMENT_PROGRAM_ARB,
           GL_MAX_PROGRAM_ALU_INSTRUCTIONS_ARB,
           output pointer
```

Disassembly confirms that the indirect call at `ds2render.dll+0x1FB7E` is not
guarded after the extension-absent branch. This is now a deterministic Phase B
capability/fallback problem rather than an opaque crash.

An isolated ARB capability probe was also run. After supplying ABI-correct
addresses for every DS2-requested ARB procedure, the trace contained no unknown
procedures and no crash, but DS2 still displayed `engine can't be run on this
hardware` after its final limit queries. That probe was archived and was not
left enabled because the shader implementation is incomplete and the target
configuration is `use_shaders=0`.

### Files changed

- `code/d3d_diagnostics.cpp/.hpp`
- `code/d3d_wrapper.cpp/.hpp`
- `code/d3d_global.cpp/.hpp`
- `code/d3d_array.cpp`, `code/d3d_immediate.cpp/.hpp`
- `code/d3d_extension.cpp/.hpp`, `code/d3d_get.cpp`
- buffer, state, texture, matrix, lighting, list, pixel, clip, blend, and
  PBuffer wrappers now route GL errors through origin-preserving diagnostics
- `msvc/QindieGL-src.vcxproj` and `.filters`
- `msvc/QindieGL.ini`
- `msvc/opengl32.def`
- `tools/glintercept/`

### Build and verification

Final build command used Visual Studio 18 Community's installed MSVC toolchain
with a Win32 `v145` command-line override:

```powershell
MSBuild.exe msvc\QindieGL-src.vcxproj /t:Rebuild /m `
  /p:Configuration=ReleaseNoRemixMods /p:Platform=Win32 `
  /p:PlatformToolset=v145
```

Results:

- full rebuild: passed;
- output: `bin\ReleaseNoRemixMods\opengl32.dll`;
- PE architecture: x86 (`0x14C`);
- `wglGetProcAddress`, `wglSwapBuffers`, and `wglSwapMultipleBuffers` exports:
  present;
- project/filter XML parse: passed;
- trace-preparation PowerShell parse: passed;
- `git diff --check`: passed; only Git LF-to-CRLF notices were emitted;
- final DLL SHA-256 matched the DLL deployed as
  `QindieGL-traced.dll` in the game directory.

The rebuild retains pre-existing warnings in the ARB compiler, pixel path,
miscellaneous string conversion, and Remix helper code; no build errors occur.

### Captured artifacts

- Clean hardware probe trace:
  `H:\YAE\Original\You Are Empty\gliInterceptLog_FULL.phaseA-clean-arb-probe.txt`
- GLIntercept parser log for that trace:
  `H:\YAE\Original\You Are Empty\gliLog.phaseA-clean-arb-probe.txt`
- Structured QindieGL probe log:
  `H:\YAE\Original\You Are Empty\QindieGL.phaseA-clean-arb-probe.log`
- Enhanced null-call crash report:
  `H:\YAE\Original\You Are Empty\QindieGL-crash-20260830-172618.txt`
- Earlier attempts are preserved under descriptive `phaseA-*` names; original
  pre-test DLL/INI/config backups are preserved as `phaseA-preexisting` files.

### Milestone state

- Native vs QindieGL visual parity: not evaluated; no rendered frame yet.
- VBO: disabled for the initial YAE path; not declared working.
- ARB shaders: unused by the game config; compatibility path remains partial
  and is disabled in the final profile.
- PBuffers: entry points were requested, but no PBuffer was created.
- D3D failures: none before hardware detection terminates.
- Pure QindieGL to D3D9: not stable/playable and not ready for RTX Remix.
- Commits created: none.

The next scoped task is Phase B: make DS2 select its genuine non-shader fallback
without advertising incomplete ARB support, then reach the main menu and first
level.

## Phase B — startup and capability detection

Status: **complete and tested through the first playable level**. YAE now boots,
passes the DS2 hardware gate, renders a stable main menu, and reaches gameplay
both from the console and through the normal New Game flow.

### Hardware gate and compatibility profile

Disassembly of `ds2render.dll` established that its startup gate requires all of
the following, even with `use_shaders=0`:

```text
GL_ARB_fragment_program
GL_ARB_vertex_program
GL_ARB_vertex_buffer_object
GL_EXT_draw_range_elements
```

The texture-unit and ARB-program limit queries are logged but are not themselves
the final pass/fail comparisons. DS2 also resolves and calls the complete ARB
program entry-point surface without consistently guarding null pointers.

The exception is isolated behind `yae_fallback_compatibility=1` in both known YAE
executable profiles, `[game.game]` and `[game.YOU_ARE_EMPTY]`. It exposes the
minimum gate extensions and ABI-correct entry points needed by this engine. The
global/default profile remains unchanged.

### Startup fixes

- Added the complete ARB vertex-attribute entry-point surface requested by DS2.
- Made `GL_PROGRAM_ERROR_POSITION_ARB` return the required no-error value `-1`.
- Implemented correct CPU-backed VBO offset resolution for legacy vertex arrays
  and element arrays. This fixed the startup/level-load null dereference caused
  by `glVertexPointer(..., 0)` while an array buffer was bound.
- Saved the array-buffer binding at array specification time and validated the
  referenced byte range when resolving it for a draw.
- Silently accepted `GL_LINE_SMOOTH`, which DS2 enables but QindieGL cannot map
  directly to D3D9.
- Capped repeated invalid-combiner diagnostics so a bad scene cannot grow the
  log without bound.
- Added a YAE-only `GL_TEXTURE_RECTANGLE` storage compatibility path. Rectangle
  textures are backed by D3D9 2D texture storage; the three DS2 startup surfaces
  now allocate without `GL_INVALID_OPERATION`.

### Test configuration

```text
1920x1080x32 fullscreen at 60 Hz
depth_bpp=24
stencil_bpp=0
hdr=0
use_shaders=0
use_normalmaps=0
motion_blur=false
use_cached_skinning=false
```

The reproducible engine configuration is stored as
`tools/glintercept/ds2engine.phase-b-test.cfg`.

The final New Game validation ran for 3272 presented frames and 447085 draw
calls. The QindieGL summary reported:

```text
unsupported enums: none
failed D3D calls: none
device resets: 0
PBuffers created: 0
ARB programs uploaded: 7
ARB programs compiled: 0
VBOs created: 184
peak VBO storage: 57990452 bytes
```

The user confirmed that the intro opens, its audio plays, and the first level
starts. The video image is currently reduced to a thin white strip; this is a
remaining rectangle/video-coordinate rendering defect, not a loading blocker.

### Captured Phase B artifacts

- Normal New Game run:
  `H:\YAE\Original\You Are Empty\QindieGL.phaseB-new-game-success.log`
- Matching clean GLIntercept trace:
  `H:\YAE\Original\You Are Empty\gliInterceptLog_FULL.phaseB-new-game-success.txt`
- Matching GLIntercept parser log:
  `H:\YAE\Original\You Are Empty\gliLog.phaseB-new-game-success.txt`
- Longer console-to-level run:
  `H:\YAE\Original\You Are Empty\QindieGL.phaseB-gameplay-console.log`
- First QindieGL gameplay screenshot:
  `tools/glintercept/phaseB-texture2d-current.png`

### Compatibility exceptions and remaining work

The task's proposed Phase C configuration assumes that DS2 can run with VBO and
ARB programs absent. The retail `ds2render.dll` disproves that assumption: it
hard-requires both families and, after loading the level, invokes the ARB program
API heavily even though `use_shaders=0`. The present Phase B profile therefore:

- advertises VBO and uses corrected CPU-backed VBO semantics;
- exposes ARB programs in explicit compatibility-stub mode;
- keeps HDR, normal maps, and optional material shaders disabled.

This is sufficient for startup but not visual parity. In the first level, base
textures are missing from much of the static geometry, lightmaps are mapped
incorrectly, nearby geometry can disappear, input/frame pacing is jerky, and
performance is low. These are the starting defects for Phase C. The trace shows
seven actual DS2 ARB programs and no generic vertex-attrib-array invocations, so
the first Phase C experiment should compile that small observed program corpus
instead of broadening generic OpenGL support.

Pure QindieGL to D3D9 is now usable for Phase C investigation, but it is not
visually correct, stability-qualified, or ready for RTX Remix.

## Phase C - basic world-rendering checkpoint

Status: **validated in the first playable level**.

The following QindieGL rendering fixes are now working together in the YAE
fallback profile:

- static and dynamic geometry remain visible at normal viewing distances;
- the lightmap atlas uses the correct vertical orientation;
- VBO-backed index ranges no longer produce full-screen flashing triangles;
- texture matrices use affine 2D transforms without corrupting the vertex layout;
- the menu remains intact after entering and leaving gameplay.

The apparent missing-diffuse defect was traced to modified game data rather
than another texture-stage bug. The active `med1.ds2` and `med1_lm_0.tga` had
been rebuilt on February 21, 2026. That scene registered only 219 materials,
created 11 static vertex buffers, requested a missing `baked_atlas_0` four
times, and intentionally bound the neutral gray texture to the lightmapped
static diffuse stage. Replacing that stage with a diagnostic texture proved
that its UVs and lightmap combine state were already correct.

Restoring the retail files dated July 28, 2006 removed the missing-atlas
requests. The game then registered 338 scene materials and created 16 static
vertex buffers in the QindieGL path. The user confirmed that static diffuse
textures, lightmaps, geometry stability, and the menu all render correctly.

Known retail asset hashes for this test installation:

```text
med1.ds2       B5DAF95A6606725AF54B9C3598FD15BFDAD97725038CC5E7F245931F9C11D529
med1_lm_0.tga  E4CB830A2C12431837D0937C2041DE0E87472847ED75764F2DE5AEC27D6341EF
```

The modified files were preserved outside the repository as
`*.phaseC-rebuilt-backup`; they are not required by QindieGL.

### Intro video and fullscreen recovery

The intro AVI is a 720x420 `GL_TEXTURE_RECTANGLE` upload. OpenGL rectangle
coordinates are expressed in pixels, but the D3D9 2D texture used by the YAE
compatibility path requires normalized coordinates. Fixed-function vertex-array
and immediate-mode draws now scale S/T by the bound rectangle texture size.
ARB fragment-program draws are deliberately excluded because the generated
shader already applies its own `_rectScale` constant.

The first complete-video test exposed a separate pre-existing fullscreen-reset
failure. QindieGL retained the interface returned by `GetSwapChain(0)` while
calling `IDirect3DDevice9::Reset`, causing `D3DERR_INVALIDCALL`; subsequent
draws targeted the invalid device and a failed dynamic-buffer allocation could
leave a dangling COM pointer. Reset now ends the active scene, releases and
reacquires the implicit swap chain, recreates streaming resources, invalidates
cached render state, and reapplies it. Dynamic vertex/index allocation failures
also leave null, zero-sized slots and abort the affected draw safely.

User validation confirmed all of the following in one normal New Game run:

- the intro has a full image with correct orientation and synchronized audio;
- the first level appears after the video;
- control and menus work;
- static textures and lightmaps remain correct;
- no full-screen flashing triangles, hang, or crash occurs.

A second run explicitly exercised `Alt+Tab` and return to the fullscreen game.
The log recorded `Reset hr=0x00000000 S_OK`, the game remained fully usable,
and the session summary reported:

```text
unsupported enums: none
failed D3D calls: none
device resets: 1
ARB programs uploaded/compiled: 8/8
ARB program compilation failures: 0
peak VBO storage: 25175072 bytes
```

Phase C's basic world-rendering acceptance target is therefore complete for the
reference level. The known pickup post-effect alpha mismatch is deferred as an
advanced effect and does not block the Phase D stability run.

## Phase D - stability checkpoint

Status: **15-minute gameplay and repeated save loading validated without a
crash**.

An earlier save/load and menu run ended with the Microsoft runtime dialog
`R6025 - pure virtual function call`. QindieGL's crash report identified the
actual first fault as an access violation in `D3DTextureObject::CopyTextureSubLevel`.
DS2 had requested a 512x512 framebuffer copy immediately after `CreateTexture`
failed with `E_OUTOFMEMORY`; release assertions did not stop the wrapper from
dereferencing the resulting null D3D texture.

The failure paths used while extending a texture from one level to a complete
mip chain also leaked the newly-created COM texture and/or level surfaces when
a get/copy operation failed. Those paths now release every temporary resource
before returning. Texture image/subimage/copy entry points validate the backing
D3D texture and return a GL error instead of invoking through a null pointer.
Object-buffer growth no longer loses the original allocation when `realloc`
fails, and deleting a currently bound texture detaches all wrapper bindings
before destroying the object so no dangling texture pointers remain.

The validating run repeated the original save/load scenario, exercised menus,
performed two fullscreen device resets, and continued through more than 15
minutes of stable gameplay. Its clean shutdown summary reported:

```text
frames: 35127
draw calls: 16741901
failed D3D calls: none
device resets: 2
ARB programs uploaded/compiled: 8/8
ARB program compilation failures: 0
peak VBO storage: 39930622 bytes
```

No texture allocation/recreation warning, skipped framebuffer copy,
`GL_OUT_OF_MEMORY`, or new crash report occurred. Save loading, menus, and
`Alt+Tab` all remained operational.

### Phase D compatibility polish: UI clipping and animated texture matrices

Two non-blocking rendering mismatches found during the stability pass are now
fixed and user-validated.

DS2 deliberately submits scissor boxes which may extend outside the current
framebuffer. QindieGL previously clamped only the box origin while retaining its
original width and height. That enlarged several menu clipping regions and could
also pass an invalid rectangle to D3D9, leaving a stale scissor rectangle active.
The wrapper now preserves the original OpenGL scissor state for `glGet`, clamps
both rectangle edges for D3D9, rejects draws covered by an empty intersection,
and restores the rectangle and enable state after attribute or device-state
reapplication.

The scrolling-material trace showed that DS2 updated an affine texture matrix
every frame (the S translation advanced from approximately 85.68 to 86.40), but
the visible fixed-function texture coordinates remained static. YAE's affine
S/T transforms are now folded into copied vertex coordinates for both vertex
arrays and immediate-mode draws. Complex matrices and ARB vertex-program paths
continue to use their existing implementations, avoiding the unsafe
`COUNT4|PROJECTED` state which previously caused screen-sized flashing
triangles. Transform selection is cached once per texture stage and draw; an
initial per-vertex classification restored the animation but reduced frame rate
from 40-60 FPS to 8-20 FPS and was replaced before this checkpoint.

The final validating run confirmed:

- the affected material scrolls;
- static textures and lightmaps remain correct;
- no flashing triangles are present;
- menu clipping is correct;
- performance returned to its previous 40-60 FPS range.

Its clean shutdown summary reported:

```text
frames: 4824
draw calls: 2602605
failed D3D calls: none
device resets: 1
ARB programs uploaded/compiled: 8/8
ARB program compilation failures: 0
VBOs created: 234
peak VBO storage: 39924880 bytes
```

Remaining observed rendering issue for follow-up:

- the pickup post-effect works but lacks the original translucent appearance.

### Phase C visibility follow-up and fullscreen presentation

The remaining short-distance geometry disappearance was caused by the original
`GL_ARB_occlusion_query` compatibility stubs. They always returned one passed
sample, which is not a conservative result for DS2's visibility system and led
the engine to cull nearby static geometry incorrectly. QindieGL now maps query
objects to D3D9 occlusion queries, implements availability and blocking-result
semantics, and recreates the default-pool query resources safely after a device
reset. During device loss an unresolved query is conservatively treated as
visible so focus changes cannot introduce a transient over-cull.

YAE now uses D3D9 windowed presentation behind its existing full-monitor popup
window. This preserves the game's borderless-fullscreen appearance while
allowing `Alt+Tab` to expose other applications reliably. Draw submission is
also suppressed while the device is lost rather than touching invalid D3D9
streaming resources.

The temporary full GLIntercept configuration used for diagnosis performed an
error check and function trace after every GL call. At YAE's draw-call rate that
reduced performance from roughly 40-60 FPS to 8-30 FPS. Normal launches now
install `tools/glintercept/gliConfig.runtime.ini`, which retains the QindieGL
structured log but disables per-call interception logs and injected error
checks. Routine display-list messages were moved to debug level for the same
reason.

Two distinct lightmap upload paths were observed and are intentionally kept
separate:

- runtime-built 4096x4096 RGBA/BGRA atlases are vertically reflected;
- authored 2048x2048 RGB/BGR atlases preserve their source row order.

The user validated the combined result across two locations: geometry no
longer disappears, static/dynamic textures and correctly oriented lightmaps
remain present, menu clipping is correct, `Alt+Tab` works, and performance is
40-60 FPS depending on the location. No flashing triangles were observed.

The validating session reached 16,160 frames and 7,759,954 draws. It exposed a
separate stability follow-up after multiple area loads: fourteen late 512x512
managed-texture allocations returned `E_OUTOFMEMORY` while the 32-bit process
had about 1,089 MiB committed (839 MiB private). The failure was handled safely
and caused no visible corruption or crash in that run.

### Phase D multi-location address-space fix

Texture-resource counters and `VirtualQuery` address-space snapshots established
that the allocation failures were not a QindieGL texture leak. At the first
failed 2048x2048 lightmap allocation, live D3D textures accounted for an
estimated 130 MiB, but the non-LAA process had only 145 MiB total address space
free and its largest contiguous block was 15 MiB. The requested X8R8G8B8 atlas
requires 16 MiB. A later attempt had only a 12 MiB largest block. The remaining
address space was split between committed memory and reservations owned by the
game, D3D runtime/driver, and interception modules.

The retail `YOU_ARE_EMPTY.EXE` is a 32-bit PE image without the
`IMAGE_FILE_LARGE_ADDRESS_AWARE` flag. Its executable header was patched with
Visual Studio `editbin /LARGEADDRESSAWARE`; this raises the user virtual-address
limit from 2 GiB to 4 GiB on 64-bit Windows without changing game code. The
original executable was preserved beside it as
`YOU_ARE_EMPTY.EXE.pre-laa-backup`.

Hashes for this test installation are:

```text
original/backup SHA-256  18FAB62992049C7C15BF67A353136EEC077B45D789788D7640D75AFE7943E5E3
LAA-patched SHA-256      D98D40CFA518B9103E5B2620FF74C6E1DAADEAC8CF1E3AEBE0DB0B8C40C6432C
```

The user then loaded five locations consecutively, including returning to an
earlier location. Lightmaps remained present and correctly oriented throughout;
textures, UI, 60 FPS operation, and `Alt+Tab` also remained correct. The clean
shutdown summary reported:

```text
frames: 48584
draw calls: 22510476
failed D3D calls: none
device resets: 0
VBOs created: 1017
peak VBO storage: 57862990 bytes
texture allocation failures: none
```

At the final recorded atlas upload, the LAA process still had approximately
1,857 MiB free with a 1,759 MiB largest contiguous block. Live texture storage
was estimated at 260 MiB (288 MiB peak), and process commitment had fallen from
its previous-location peak, so the run showed no runaway wrapper allocation.
Phase D is revalidated with the real occlusion path. The NVIDIA overlay not
appearing over gameplay is a newly observed low-priority compatibility issue.

## Phase E - correct VBO support

Status: **complete and user-validated**.

YAE already exercised the VBO path because its compatibility profile must
advertise `GL_ARB_vertex_buffer_object` to pass the DS2 hardware gate. The
global extension switch remains disabled so unrelated applications do not opt
into the compatibility implementation accidentally. For YAE, VBO data remains
in CPU shadow storage and is copied into the existing D3D9 streaming buffers at
draw time; this preserves the renderer path whose performance and stability
were established in Phases C and D.

The extension entry-point table now publishes both core and ARB names from the
same implementations. Previously, entries which were already ARB-suffixed
caused the table macro to generate invalid names such as
`glBufferDataARBARB`. The complete advertised surface now includes buffer
creation/deletion, data replacement, `BufferSubData`, readback, mapping, and
buffer parameter/map-pointer queries.

The CPU-backed implementation now:

- preserves the previous data store if replacement allocation fails;
- validates targets, usage/access values, negative sizes, and overflow-safe
  byte ranges;
- rejects updates/readback while a buffer is mapped;
- cleans all buffer storage when the final GL context is destroyed while
  retaining it across D3D device resets;
- resolves array-buffer pointers as byte offsets captured when each legacy
  array is specified;
- resolves element-buffer pointers as byte offsets at draw time;
- validates every enabled VBO-backed vertex attribute through the largest
  referenced index before touching the D3D9 streaming buffers;
- aborts invalid draws safely instead of submitting zero-filled or
  uninitialized vertices.

The user loaded and played the new build without visual or stability issues and
reported 30-60 FPS depending on location. Textures, lightmaps, geometry, and
the prior rendering fixes remained intact. The clean shutdown summary reported:

```text
frames: 39181
draw calls: 17730732
failed D3D calls: none
device resets: 0
unsupported enums: none
VBOs created: 937
peak VBO storage: 58845996 bytes
VBO range failures: none
```

This satisfies Phase E acceptance: YAE reaches and renders its gameplay scenes
correctly with VBO enabled. The next task in the supplied plan is optional
Phase F, ARB shader support.

## Phase F - ARB shader path

Status: **in progress**. Test configuration:
`tools/glintercept/ds2engine.phase-f-test.cfg` (`use_shaders=1`, HDR off,
normal maps off). The observed DS2 corpus compiles completely (58 programs,
0 failures, in the latest run).

### Blue-green weapons held by enemies

Symptom: with `use_shaders=1`, weapons attached to an enemy's hand rendered in
the solid blue-green fog colour `(0.32, 0.42, 0.42)`, while the enemy's body and
world geometry were correct.

Root cause: **a DS2 bug, reproduced on the native NVIDIA OpenGL driver**. DS2's
rigid (`E2FD5400`) and skinned (`52D29C62`) material VPs write
`camera_pos_ws - inst_matrix * pos` to `TEXCOORD5`. All five fragment programs
reading `TEXCOORD5` use only its length, as the linear fog distance
(`DP3 t5, t5`). For a bone-attached weapon, DS2 multiplies the hand bone
(including its 0.0688 scale) into the modelview but passes the owner's
`inst_matrix` without it. The weapon's raw vertices (~5000 units) therefore give
a fog distance of about 9000 instead of about 250, and the fog saturates. DS2
also derives the weapon's fragment-program camera by transposing the scaled
attachment matrix instead of inverting it; the value is off by exactly
`0.0688^2` in all three components.

Evidence from the `YAE_FOG_PROBE` diagnostics:

- weapon draw (VP 31, FP 32): `fogDist=9008`, true `eyeDist=258`;
- DS2 wrote every involved local parameter immediately before that draw; the
  values are not stale per-object state and match the owner's body draw;
- every consistent draw had `fogDist == eyeDist` to within 0.001;
- the native reference run (GLIntercept loader and `YOU_ARE_EMPTY.exe.local`
  temporarily moved aside) showed the same blue weapon.

Fix: YAE-only `yae_eye_distance_fog` in `[game.game]` and
`[game.YOU_ARE_EMPTY]`. For vertex programs writing `result.texcoord[5]`, the
generated HLSL recovers the eye-space position as `P^-1 * clip` and rescales
`TEXCOORD5` to that length while keeping its direction. Consistent draws are
unchanged; attached weapons and rigid objects submitted with an identity
`inst_matrix` receive the correct fog. This intentionally deviates from native
rendering; set the key to 0 for native comparisons.

An earlier unbuilt workaround that replaced the fog range of the whole lit
dynamic-material family was discarded: it removed legitimate fog from every
enemy and dynamic object.

Files changed:

- `code/d3d_arb_program.cpp/.hpp`: `eyeDistanceTexCoord5` generation and the
  `_yaeInvProjection` constant;
- `code/d3d_global.cpp/.hpp`, `msvc/QindieGL.ini`: `yae_eye_distance_fog`;
- `code/d3d_diagnostics.cpp/.hpp`, `code/d3d_extension.cpp`,
  `code/d3d_matrix.cpp`, `code/d3d_state.cpp`: fog probe and call history;
- `tools/glintercept/gliConfig.native.ini`: reference configuration (see
  below).

Validation: the user confirmed that enemy weapons are no longer blue and that
enemies and dynamic objects still fade into distance fog. The session ran 5957
frames and 1551990 draws; the six DS2 VPs writing `TEXCOORD5` (programs
7/19/31 and 9/21/33) received the correction; no compilation failures, failed
D3D calls or unsupported enums occurred. Both patched VS variants were also
compiled offline with `fxc /T vs_3_0`. Logs are preserved in the game directory
as `QindieGL.phaseF-fog-probe1.log`, `QindieGL.phaseF-fog-probe2.log` and
`QindieGL.phaseF-eye-distance-fog.log`.

### Diagnostics added

With `LogLevel = 3`, each unique VP/FP/diffuse/saturation combination using the
DS2 fog family logs `YAE_FOG_PROBE`: the CPU-evaluated DS2 fog, the fog after
`yae_eye_distance_fog`, the true eye distance, VP/FP camera consistency against
the modelview, `inst_matrix`, the modelview and the frame/draw of the last write
of each relevant local parameter. The first three saturated draws also dump
`YAE_FOG_HISTORY`: program binds, local writes, program enables, modelview
operations and draws since four draws earlier. Both are inactive at the default
INFO level.

### Native reference runs

`YOU_ARE_EMPTY.exe.local` makes Windows resolve even full-path `opengl32.dll`
loads to the game directory, so GLIntercept cannot forward to the system driver
(`gliLog.txt`: "OpenGL lib file points to GLIntercept lib"), and the game exits
immediately. For a native reference, temporarily rename both the game-directory
`opengl32.dll` loader and `YOU_ARE_EMPTY.exe.local`, launch the executable
directly and restore both files afterwards. `gliConfig.native.ini` is kept for
setups without the `.local` redirection.

### Known remaining issues

- Attached weapons still use DS2's incorrectly scaled fragment-program camera
  for specular/light directions. This matches native rendering and is not
  corrected.
- `glLoadTransposeMatrixd` and `glMultTransposeMatrixd` increment `i` instead
  of `j` in their inner copy loop (out-of-bounds read). Not used by the observed
  DS2 path; to be fixed separately.
