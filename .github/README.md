# Mesa for Samsung Xclipse 920

RADV, Mesa's AMD Vulkan driver, running on the **Samsung Xclipse 920** GPU (Exynos 2200) inside a
Linux container on Android: Debian in proot under Termux, with the picture shown by Termux:X11.
OpenGL and OpenGL ES run on top of it through Zink.

This is the `xclipse` branch of a fork of [lfdevs' Mesa for Android Container](https://github.com/lfdevs/mesa-for-android-container)
(Mesa for Adreno GPUs in Android containers). The original project's README is
[here](https://github.com/lfdevs/mesa-for-android-container/blob/adreno-main/.github/README.md).

> [!WARNING]
> Experimental. A GPU hang reboots the whole phone, so save your work in other apps first. Tested on
> one device only.

## Status

Tested on a Galaxy S23 FE (SM-S711B, Exynos 2200), Android 16, with Samsung's stock kernel.

| | State |
| :-- | :-- |
| Vulkan (RADV) | Works: vkcube, compute, textures, occlusion queries, discard, vertex input. MSAA is partly broken. |
| OpenGL 4.6 / OpenGL ES 3.2 | Works through Zink on RADV (glmark2, glxgears). |
| WebGL | Works in NW.js/Chromium through ANGLE and Zink; an RPG Maker MV game plays at ~60 FPS. |
| Presentation | DRI3/Present with [KiralyCraft's Termux:X11 fork](https://github.com/KiralyCraft/termux-x11/tree/fix/present-vblank-pacing-minimal). |
| Vulkan CTS | Partly run and compared against Samsung's own driver; see the test results. |
| Random phone resets | The reset at process start is fixed (none in 1000 process starts). |

## Why it needed changes

Samsung's `sgpu` kernel driver is a fork of `amdgpu` and reports the GPU as VanGogh (RDNA2, GFX10.3)
on the platform bus. The chip is a hybrid: the command processor and registers are GFX10.3, but the
shader cores decode the **GFX11** instruction encoding and the texture units use GFX11 resource
formats and descriptors. The kernel driver also differs from amdgpu (no clear state buffer, no SDMA,
DRM 3.40, a GART TLB flush that can hit the power-gated GPU).

All changes are gated on the detected Xclipse device (`radeon_info::is_xclipse`), so other AMD GPUs
are unaffected. The main ones:

- device detection on the platform bus, DRM 3.40, no SDMA queue;
- ACO emits the GFX11 encoding in wave64, without `s_delay_alu` and VOPD;
- buffer, image and vertex descriptors use the GFX11 layout and formats;
- pixel shader export and discard handling like Samsung's compiler;
- CLEAR_STATE emulated, GPU state programmed like Samsung's driver, no user fence;
- `MESA_VK_WSI_X11_WAIT_READY=1` so Termux:X11 never shows a half-drawn frame.

Every commit and the reason for it is listed in the
[status document](docs/common/xclipse-920-handoff.md#2-all-mesa-changes-in-commit-order).

## Build and install

You need an x86_64 Linux computer with Docker, adb, and a phone with Termux (the Play Store build
was tested), `proot-distro` with a Debian 13 container, and the Termux:X11 fork above. The tools
cross-compile in Docker and install over SSH into the container.

```bash
git clone -b xclipse https://github.com/Miu-exe/mesa-for-android-container
cd mesa-for-android-container/.github/tools/xclipse
docker/build-images.sh                               # build images (once)
PHONE=phone scripts/build-radv.sh                    # RADV  -> /opt/radv in the container
PHONE=phone scripts/build-mesa-gl.sh                 # Zink  -> /opt/mesa-xclipse
```

`PHONE` is an SSH destination for Termux's sshd. Setting up the phone (SSH, Debian, X server,
PulseAudio) is described step by step in the [status document](docs/common/xclipse-920-handoff.md#3-setting-up-a-new-computer).

## Use it

Start Termux:X11 (`scripts/x11-start.sh`, copied to Termux), then in the Debian container:

```bash
# Vulkan
export VK_DRIVER_FILES=/opt/radv/radv_icd.json MESA_VK_WSI_X11_WAIT_READY=1 DISPLAY=:0
vkcube

# OpenGL / OpenGL ES through Zink
LD_LIBRARY_PATH=/opt/mesa-xclipse/lib LIBGL_DRIVERS_PATH=/opt/mesa-xclipse/lib/dri \
MESA_LOADER_DRIVER_OVERRIDE=zink glmark2
```

After rebuilding the driver, delete the shader caches (`~/.cache/mesa_shader_cache*`).

## Documentation

- [Status, findings and handoff](docs/common/xclipse-920-handoff.md): what works, the hybrid in
  detail, every commit, setup, debugging workflow and open problems.
- [Test results](docs/common/xclipse-920-testing.md): Vulkan tests and CTS comparisons.
- [Tools](tools/xclipse/README.md): build scripts, test programs, capture and register-decoding
  tools.

## Credits and license

- [Mesa](https://gitlab.freedesktop.org/mesa/mesa), MIT license.
- [lfdevs](https://github.com/lfdevs/mesa-for-android-container): the Mesa for Android Container
  fork this branch is built on.
- [KiralyCraft](https://github.com/KiralyCraft/termux-x11): the Termux:X11 fork with DRI3.

Not affiliated with Samsung or AMD.
