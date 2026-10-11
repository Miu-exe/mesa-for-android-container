# Samsung Xclipse 920 (Exynos 2200) – RADV bring-up and test results

> For the full current status, all fixes and how to continue, see
> [xclipse-920-handoff.md](xclipse-920-handoff.md).

This document describes how RADV (Mesa's Vulkan driver for AMD GPUs) was brought up on the
Samsung Xclipse 920 GPU, how it was tested, and the results so far. Everything here was done on
a single device and is **experimental**.

## Test device

| | |
| :-- | :-- |
| Device | Samsung Galaxy S23 FE (SM-S711B) |
| SoC | Exynos 2200 (`s5e9925`) |
| GPU | Samsung Xclipse 920 (AMD RDNA2-based, 6 CUs) |
| Android | 16 |
| Kernel GPU driver | Samsung `sgpu`, reports itself as `amdgpu` DRM **3.40.0** |
| Container | Debian 13 (trixie) in PRoot (`proot-distro`) inside Termux |
| X server | Termux:X11, [KiralyCraft `fix/present-vblank-pacing-minimal`](https://github.com/KiralyCraft/termux-x11/tree/fix/present-vblank-pacing-minimal) (DRI3 + Present) |
| Mesa | this repository, branch `xclipse` (lfdevs' `adreno-main` + the Xclipse commits, 26.1.0-devel) |

## How the GPU appears to Mesa

`/dev/dri/renderD128` is world-accessible from Termux and answers the standard amdgpu ioctls:

| Query | Value |
| :-- | :-- |
| DRM driver | `amdgpu` 3.40.0 (platform bus, `/sgpu@16E00000`, no PCI address) |
| Family | 144 = `AMDGPU_FAMILY_VGH` (VanGogh), device id `0x73a0` |
| Shader engines / CUs | 1 SE, 1 SA, 6 CUs (3 WGPs), wave32/64 |
| Memory | no VRAM, ~6 GB GTT (unified memory) |
| Rings | 4 GFX, 3 compute, **no SDMA**, no video engines |

## Key finding: a GFX10.3 / GFX11 hybrid

Xclipse 920 is not a plain RDNA2 (GFX10.3) GPU. The command processor and register interface
behave like GFX10.3, but the **shader core follows GFX11 (RDNA3) conventions**. This was
established by capturing the command buffers and shader binaries that Samsung's own Vulkan
driver submits and comparing them with RADV's output.

| Area | Behaves like | Evidence |
| :-- | :-- | :-- |
| PM4 packets, context/SH registers, NGG setup | GFX10.3 | Samsung's IBs decode correctly as GFX10.3 |
| Shader instruction encoding | **GFX11** | Samsung shaders disassemble cleanly only as gfx1100 (`s_endpgm` = `0xbfb00000`) |
| VOPD dual issue, `s_delay_alu` | **not supported** | never emitted by Samsung's compiler; VOPD hangs the GPU |
| Compute local invocation IDs | **GFX11** (packed in `v0`) | `v_and_b32 0x3ff` / `v_bfe_u32 10, 10` in Samsung shaders |
| Buffer/image format numbering | **GFX11** | e.g. `8_8_8_8_UNORM` = 42 in Samsung's descriptors |
| Image descriptor layout | **GFX11** | no `RESOURCE_LEVEL` bit, GFX11 `MAX_MIP`/`MIN_LOD` placement |
| Pixel shader interpolation | **GFX11** (`lds_param_load` + `v_interp_p10/p2`) | Samsung pixel shaders |
| Pixel shader color exports | **GFX11** (packed, no `COMPR`) | Samsung pixel shaders |
| Vertex parameter exports | GFX10.3 (`exp param0`) | Samsung NGG shaders |

## Mesa changes

| Commit | Change |
| :-- | :-- |
| `radv: allow amdgpu-compatible GPUs on the platform bus` | accept `DRM_BUS_PLATFORM` devices, no PCI info required |
| `ac/gpu_info: support Samsung Xclipse (sgpu) kernel quirks` | accept DRM 3.40 for non-PCI devices; treat "no VRAM" as integrated |
| `radv: don't create a private SDMA blit queue without SDMA` | Xclipse has no SDMA engine |
| `radv,aco: support Samsung Xclipse 920 shader ISA` | GFX11 encodings in ACO, wave64, no `s_delay_alu` |
| `ac,radv: use GFX11 resource formats and packed local IDs on Xclipse` | `ac_rsrc_gfx_level()` for descriptors |
| `radv: lower PS exports for the GFX11 ISA on Xclipse` | packed color exports |
| `radv/amdgpu: only request NO_CPU_ACCESS BOs when the GPU has VRAM` | lets Termux:X11 mmap DRI3 buffers |
| `ac/descriptors: use the GFX11 image descriptor fields on Xclipse` | fixes texture sampling |
| `aco: don't use VOPD on chips with only the GFX11 encoding` | fixes wave32 hangs |

## How the testing was done

- **Builds**: everything was cross-compiled on an x86_64 PC (Docker `debian:trixie` with
  `crossbuild-essential-arm64`; Android NDK r27c for Termux-side tools). Nothing was compiled on
  the phone.
- **Reference driver**: Samsung's own Vulkan driver (`/vendor/lib64/hw/vulkan.samsung.so`) was
  used as ground truth. A small in-process capture library (seccomp `SIGSYS` trap on `ioctl`)
  saved every submitted IB, the shader binaries and all buffer contents without root. IBs were
  decoded with Mesa's `ac_ib_parser`, shaders with `llvm-mc -mcpu=gfx1100`, and descriptors with
  Mesa's register database (`src/amd/registers/gfx1*-rsrc.json`).
- **Safety**: GPU hangs on this phone reboot the whole device, so every new feature was first
  run on Samsung's driver (which cannot hang), compared offline, and only then tried on RADV.
- **Test programs** ([`.github/tools/xclipse/tests`](../../tools/xclipse/tests)): small single-purpose Vulkan programs, each checking its output on the CPU:
  compute buffer stores (1D and 8×8 workgroups), `vkCmdFillBuffer` (CP DMA and compute paths),
  storage texel buffers (`R32_UINT`, `R8G8B8A8_UINT`), an offscreen interpolated-color triangle,
  and texture sampling through a combined image sampler.

## Results

### Functional tests (RADV)

| Test | Result |
| :-- | :-- |
| Device enumeration (`vulkaninfo`) | ✅ `AMD Radeon Graphics (RADV VANGOGH)`, integrated GPU, Vulkan 1.4 |
| Command submission + fences | ✅ |
| `vkCmdFillBuffer`, CP DMA (256 B) and compute (64 KiB) | ✅ |
| Compute stores, 1D and 8×8 workgroups | ✅ 1024/1024, 256/256 |
| Storage texel buffers `R32_UINT` / `R8G8B8A8_UINT` | ✅ 256/256 each |
| Offscreen triangle with interpolated colors | ✅ pixel-identical to Samsung's driver |
| Texture sampling (linear filter) | ✅ 16/16, same values as Samsung's driver |
| `vkcube` via DRI3/Present (Termux:X11) | ✅ spinning textured cube confirmed on screen |
| `vkcube` with `MESA_VK_WSI_DEBUG=sw` (stock Termux:X11) | ✅ |

### OpenGL through Zink

`glxinfo -B`: `zink Vulkan 1.4(AMD Radeon Graphics (RADV VANGOGH))`, OpenGL **4.6** core and
compatibility, OpenGL ES **3.2**, direct rendering.

glmark2 (800×600 window, Termux:X11 DRI3/Present; ~120 FPS looks capped by the 120 Hz display):

```log
    GL_RENDERER:    zink Vulkan 1.4(AMD Radeon Graphics (RADV VANGOGH) (MESA_RADV))
[build] use-vbo=true: FPS: 119 FrameTime: 8.412 ms
[texture] <default>: FPS: 117 FrameTime: 8.619 ms
[shading] shading=phong: FPS: 119 FrameTime: 8.425 ms
[bump] bump-render=normals: FPS: 117 FrameTime: 8.576 ms
[refract] <default>: FPS: 88 FrameTime: 11.413 ms
[terrain] <default>: FPS: 103 FrameTime: 9.767 ms
                                  glmark2 Score: 109
```

### Vulkan CTS (incomplete)

`deqp-vk` from VK-GL-CTS **vulkan-cts-1.4.6.2**, `mustpass/main/vk-default` (98 groups,
3,247,552 cases), run with a resumable chunked runner because GPU hangs reboot the phone.

| Group | Cases run | Pass | Fail | Not supported | Notes |
| :-- | --: | --: | --: | --: | :-- |
| `api.smoke.*` | 6 | 6 | 0 | 0 | includes 4 rendering tests |
| `info` | 21 | 19 | 1 | 1 | fail: `device_extension_dependencies` (`VK_KHR_swapchain_maintenance1` dependency, not Xclipse-specific) |
| `api` (partial) | 3,000 | 2,999 | 0 | 1 | stopped by a GPU hang/reboot in chunks `api-00015..17`; culprit not yet identified |

> **These CTS numbers predate the texture-descriptor and VOPD fixes.** The run has to be
> restarted from scratch; a full pass is estimated at 2–3 days on the phone.

## Known issues and limitations

- RADV still advertises the full VanGogh feature set (ray tracing, mesh shaders, …); most of it
  is untested on Xclipse and likely to fail or hang.
- All shaders are forced to wave64. Samsung's driver uses wave32 for NGG and small compute
  shaders; the original wave32 hang was most likely VOPD, so wave32 can probably be re-enabled.
- MSAA/FMASK, DCC and HTILE (color/depth compression) have not been verified.
- radeonsi (native OpenGL) builds without LLVM but has not been ported yet; use Zink.
- Mesa's shader cache must not be shared between different builds (debug builds assert on a
  cache key mismatch) — use a separate `MESA_SHADER_CACHE_DIR`.
- `ac_compute_device_uuid` prints a warning about invalid PCI bus info (harmless).
- The first boot of Termux:X11 after a phone reboot sometimes fails with "already has an active
  X server connection"; starting the server again works.

## Reproducing

All tools and scripts used here (cross-compile images, build scripts, the test programs, the
capture tools and the CTS runner) are in [`.github/tools/xclipse`](../../tools/xclipse/README.md).

Build RADV (+ Zink) for arm64 Debian from this branch, install it into the container, then:

```bash
# Vulkan through RADV
export VK_DRIVER_FILES=/opt/radv/radv_icd.json      # ICD JSON pointing at libvulkan_radeon.so
vkcube

# OpenGL through Zink on RADV
export LD_LIBRARY_PATH=/opt/mesa-xclipse/lib
export LIBGL_DRIVERS_PATH=/opt/mesa-xclipse/lib/dri
export MESA_LOADER_DRIVER_OVERRIDE=zink
export MESA_SHADER_CACHE_DIR=$HOME/.cache/mesa-xclipse-gl
glxinfo -B
glmark2
```

With the stock Termux:X11 (no DRI3), add `MESA_VK_WSI_DEBUG=sw` so presentation goes through a
CPU copy.
