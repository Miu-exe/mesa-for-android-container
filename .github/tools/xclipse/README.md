# Xclipse bring-up tools

Tools used to bring up RADV on the Samsung Xclipse 920 (Exynos 2200) and to test it.
The results and findings are in [xclipse-920-testing.md](../../docs/common/xclipse-920-testing.md).

Everything is cross-compiled on an x86_64 Linux PC; nothing needs to be built on the phone.
If you are not in the `docker` group, prefix the scripts with `DOCKER="sudo docker"`.

## Layout

| Directory | Contents |
| :-- | :-- |
| `docker/` | Debian trixie arm64 cross-compile images (`build-images.sh`), Meson cross file, CMake toolchain |
| `scripts/` | build RADV / Mesa GL / Termux:X11 / CTS, launcher, resumable CTS runner, `cts-diff.py`, `findphone.sh` (find the phone's new IP), `x11-start.sh` (start Termux:X11), `pulse-start.sh` (PulseAudio for the container), `phone-up.sh` (sshd after a reboot, USB), `hang-report.sh` (kernel log of the previous boot after a reset) |
| `tests/` | small Vulkan test programs that check their own results on the CPU |
| `experiments/` | uncommitted driver experiments from the reset hunt, as patches (see its README) |
| `cts-results/` | Samsung driver CTS baseline (`samsung/results.csv`) for `cts-diff.py` |
| `capture/` | capture tools used to reverse-engineer Samsung's driver, descriptor decoder |
| `probes/` | tiny libdrm/amdgpu query programs |
| `webgl/` | NW.js WebGL/PIXI test app with a Zink/RADV launcher (`run-nw.sh`); copy `pixi.js` (v4) next to it. |

## Building

```bash
docker/build-images.sh                               # once: mesa-cross-arm64(-gl) images
WORK=~/xclipse-build PHONE=phone scripts/build-radv.sh      # RADV -> Debian /opt/radv on the phone
VK_LAYERS=overlay WORK=... PHONE=phone scripts/build-radv.sh # + Mesa overlay layer (/opt/radv/layers)
WORK=~/xclipse-build PHONE=phone scripts/build-mesa-gl.sh   # RADV+Zink+radeonsi -> /opt/mesa-xclipse
WORK=~/termux-x11-build scripts/build-termux-x11.sh         # DRI3/Present Termux:X11 APK
WORK=~/xclipse-build scripts/build-cts.sh linux             # Vulkan CTS for the Debian container (RADV)
WORK=~/xclipse-build NDK=... scripts/build-cts.sh android   # Vulkan CTS for Android (Samsung's driver)
tests/build.sh glibc                                 # tests for Debian (RADV)
NDK=/path/to/android-ndk-r27c tests/build.sh android # tests for Termux (Samsung's driver) + drmtrace
```

`PHONE` is an ssh destination for Termux's `sshd`, e.g. a `~/.ssh/config` entry with `Port 8022`.

## Tests (`tests/`)

Each program prints a one-line result. Run the glibc builds inside the Debian container with
`VK_DRIVER_FILES=/opt/radv/radv_icd.json`, and the Android builds from Termux (they then use
Samsung's own driver, which is the reference).

| Program | What it checks | Arguments / env |
| :-- | :-- | :-- |
| `vkfill` | `vkCmdFillBuffer` (CP DMA for small, compute shader for large sizes) | `fill|empty [size] [memtype] [queue family]` |
| `vkcomp` | compute buffer stores, 1D and 8×8 workgroups | `empty|store|storec|store2d [groups]`, `MT=<memtype>` |
| `vktexel` | storage texel buffer stores (`r32` vs `rgba8` format numbering) | `r32|rgba8` |
| `vktri` | offscreen interpolated-color triangle (graphics, PS exports), writes `tri.ppm` | `NODRAW=1` = clear only |
| `vksample` | texture sampling through a combined image sampler | `NOSAMPLE=1`, `DUMPDESC=1` prints RADV's descriptor via `VK_EXT_descriptor_buffer` |
| `vkdiscard` | fragment discard with color + depth (half the image discarded) | `NOZ=1` = no depth export |
| `vkvbo` | triangle from an interleaved vertex buffer (`R32G32_SFLOAT` + `R8G8B8A8_UNORM`), prints PASS/FAIL | `DYN=1` = `VK_EXT_vertex_input_dynamic_state` (Zink's path), `CLEANUP=1` = destroy everything before exit |

`vkloop.sh <runs> <program...>` (inside Debian) repeats a test program and appends each start and
result to `/root/vk-loop.log` with a `sync` in between, so after a GPU hang reboots the phone the last
`start` line without a result shows the run that hung. This is how the random startup hangs were
measured (see the handoff document). `samloop.sh [runs]` (in Termux) is the same loop for the Android
`vktri` on Samsung's driver, as a control (`~/sam-loop.log`).

## Capturing another driver (`capture/`)

- **`ibdump.c`**: in-process capture of everything a process submits to the GPU. It installs a
  seccomp filter that traps every `ioctl` into a `SIGSYS` handler in the same process, follows
  `GEM_VA`/`GEM_MMAP`, and on each `DRM_AMDGPU_CS` saves the IBs (`ib_NNNNNN.bin`) and the shaders
  they reference (`shader_<stage>_<va>.bin`); unmapped BOs are mapped by the tool itself. No root
  needed. Linked into the tests (`IBDUMP=<log>`) or, as `libibdump.so` (built by
  `tests/build.sh android`), preloaded into any Android Vulkan program, e.g. with Samsung's
  driver as the reference:

  ```bash
  LD_PRELOAD="$PWD/libibdump.so $LD_PRELOAD" IBDUMP=cs.log IBDUMP_DIR=cap IBDUMP_SYNC=1 ./vkfill fill 65536
  ```

  | Variable | Meaning |
  | :-- | :-- |
  | `IBDUMP=<log>` | enable; log file with one entry per submission (sequence number, timestamp, thread) |
  | `IBDUMP_DIR=<dir>` | output directory |
  | `IBDUMP_SYNC=1` | flight recorder: `fsync` the IBs and log *before* the submission reaches the GPU, so the last stream survives a hang that reboots the phone |
  | `IBDUMP_KEEP=<n>` | keep only the last *n* submissions (ring buffer, for long runs) |
  | `IBDUMP_BOS=1` | also snapshot every BO (≤ 4 MiB), e.g. to read descriptors |

  It does not work under PRoot (PRoot swallows `SIGSYS`), so use it with Android builds.
- **`nosubmit.c`** (glibc, preload): makes every `DRM_IOCTL_AMDGPU_CS` fail via `SECCOMP_RET_ERRNO`,
  so RADV compiles all shaders and builds all command buffers but nothing reaches the GPU. Combined
  with `RADV_DEBUG=dumpibs,shaders,ir` it gives RADV's full streams for a test that would hang the
  phone. Works under PRoot (no signals involved):
  `LD_PRELOAD=/opt/radv/libnosubmit.so RADV_DEBUG=dumpibs ./deqp-vk --deqp-case=...`
- **`drmtrace.c`**: ptrace tracer that decodes amdgpu ioctls (`GEM_CREATE`, `GEM_VA`, `CS`
  chunks, …) and hex-dumps vendor ioctls: `drmtrace ./vkcomp store 1`.
- **`draws.py`** / **`regstate.py`** / **`mkregfile.py`**: per-draw DB/SPI/PA state of a decoded IB
  dump, the full register diff between two dumps at a chosen draw (`regstate.py a.txt 2 b.txt 2`,
  or `ps:<pgm_lo>` to pick the draw by pixel shader), and Samsung's differing context registers as
  a `reg value` file for bisecting.
- **`firstdraw-regdiff.py`**: replays every register write up to the first draw for RADV (dump +
  its `gfx_init` buffer) and for Samsung (`ib_*.bin`) and prints the registers that differ; see the
  header for how to get RADV's init buffer.
- **`rsrcdec.py`**: decodes buffer/image/sampler descriptors with Mesa's register database:
  `rsrcdec.py gfx11 img <8 hex dwords>` (also `buf`, `samp`, and `gfx10`).

Decode captured IBs with Mesa's `ac_ib_parser gfx1033 ib_000.bin` (a native build of
`src/amd/common/ac_ib_parser`) and shaders with
`llvm-mc --disassemble -triple=amdgcn -mcpu=gfx1100` (Xclipse shaders use the GFX11 encoding).

## Vulkan CTS on Samsung's driver (reference)

`scripts/build-cts.sh android` builds a native Android `deqp-vk` (headless target, NDK). Copy it
with its `vulkan/` data directory and the `vk-default` case lists to Termux (e.g. `~/cts-android`)
and point it at the system loader, which loads Samsung's driver:

```bash
./deqp-vk --deqp-vk-library-path=/system/lib64/libvulkan.so --deqp-case='dEQP-VK.api.smoke.*'
```

`scripts/capture-reference.sh <case>...` (Termux) runs cases this way with `libibdump.so`
preloaded and stores Samsung's command streams, shaders, BO snapshots and the result under
`~/cts-android/ref/<case>/`, the known-good reference for a case that hangs on RADV.

### Oracle workflow

Samsung's driver is the reference for correct behaviour. When RADV fails or hangs a case:

1. Run the same group on both drivers with `cts-run.sh` (see below) and compare with
   `scripts/cts-diff.py radv/results.csv samsung/results.csv`. `regress` lists cases where Samsung
   passes and RADV does not.
2. Capture Samsung's streams for the case (`capture-reference.sh`) and get RADV's without touching
   the GPU (`nosubmit.c` + `RADV_DEBUG=dumpibs,shaders,ir`).
3. Compare shaders (`llvm-mc -mcpu=gfx1100`) and the register state at the draw (decoded with
   `ac_ib_parser`); if needed, write a minimal test program that runs on both drivers.

## Vulkan CTS (`scripts/cts-run.sh`)

A GPU hang reboots the phone, so the runner splits each case list into chunks, runs several
at once, and appends every result to `results.csv` as soon as it is known. Before a chunk
starts, a "running" marker is synced to disk. After a reboot, that chunk is re-run one case per
process with a sync before each case, which identifies the case that took the GPU down; it is
recorded as `Hang` and skipped from then on. In that one-case-at-a-time mode the runner also
enables `RADV_DEBUG=dumpibs` and writes it with `O_SYNC` to `flight/<case>.txt`, so RADV's decoded
command streams of the hanging case survive the reboot (files of cases that finish are deleted). `scripts/cts-bg.sh` (run in Termux) starts or
resumes the run in the background; it is safe to call again after every reboot, e.g. from
Termux:Boot.
