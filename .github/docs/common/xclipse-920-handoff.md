# Xclipse 920 RADV bring-up: status and handoff

Everything done so far to run RADV (Mesa's AMD Vulkan driver) on the Samsung Xclipse 920 GPU of a
Galaxy S23 FE (Exynos 2200), what is still broken, and how to continue on a new computer.

Related documents:

- [xclipse-920-testing.md](xclipse-920-testing.md): test results and the GFX10.3/GFX11 hybrid findings
  in detail.
- [`.github/tools/xclipse/README.md`](../../tools/xclipse/README.md): build scripts, test programs,
  capture tools and the oracle workflow.
- OMORI (the game on NW.js/WebGL through Zink: launcher app, touch controls, game build) lives on
  the `omori` branch: its `.github/docs/common/xclipse-920-omori.md`, `.github/tools/xclipse/omori/`
  and `omori-build/` (private, Git LFS).

Branch `adreno-main` on `git.miuthecat.uk/miu/mesa-xclipse-radv`. Last state of this document:
2026-10-10, driver commit `141b5bda172`.

---

## 1. Summary

| Area | State |
| :-- | :-- |
| Vulkan through RADV (Debian container in PRoot) | Works: vkcube (DRI3 and CPU copy), compute, textures, MSAA partly broken |
| OpenGL | OpenGL 4.6 / GLES 3.2 through Zink on RADV; draws correctly since `9eab0365907` (the old glmark2 score of ~109 was measured while vertex buffer draws drew nothing) |
| WebGL | NW.js 0.111.1 → ANGLE → Zink → RADV works; OMORI is playable (~45 FPS), see the `omori` branch |
| Presentation | DRI3/Present through KiralyCraft's Termux:X11 fork; vkcube ~162 FPS uncapped |
| Vulkan CTS | Partially run; 4 groups being compared with Samsung's driver, many hangs fixed |
| radeonsi (native GL) | Ported on the `opengl` branch (2026-10-11): GL 4.6 / GLES 3.2, glmark2 validates, OMORI runs; see its `xclipse-920-opengl.md` |
| GPU hangs | Reboot the whole phone. The **random reset at process start** (§6.0) is fixed since `141b5bda172` (no user fence): none in 1000 process starts (was ~1 in 6, then ~1 in 60) |

### The key insight

Xclipse 920 is reported by Samsung's `sgpu` kernel driver as `amdgpu` 3.40 with family VGH
(VanGogh, RDNA2, GFX10.3), but it is a **hybrid**:

| Behaves like GFX10.3 | Behaves like GFX11 (RDNA3) |
| :-- | :-- |
| Command processor, PM4 packets, most context/SH registers | Shader instruction **encoding** |
| Parameter exports (`exp param*`, target 32+) | Buffer/image **format numbering** and image descriptor layout |
| **VM (valid-mask) bit** on the final PS export is required | Packed compute local invocation IDs (`v0`) |
| Per-vertex primitive ID through LDS | PS interpolation (`lds_param_load` + `v_interp_*`) and packed PS exports |
| | Occlusion query events (`PIXEL_PIPE_STAT_CONTROL`/`_DUMP`) |

**Not** supported even though the encoding is GFX11: VOPD dual issue and `s_delay_alu`.
No SDMA engine, no VRAM (GTT only), 3 render backends, 6 CUs.

The rule that found almost every bug: **Samsung's own Vulkan driver is the oracle.** Capture what
it submits for the same work, compare with RADV, make RADV match.

---

## 2. All Mesa changes (in commit order)

| Commit | What and why |
| :-- | :-- |
| `fbc4ae0af1c` radv: allow amdgpu-compatible GPUs on the platform bus | sgpu is a platform device without PCI info |
| `dbfbb9942a0` ac/gpu_info: support Samsung Xclipse (sgpu) kernel quirks | accept DRM 3.40 for non-PCI devices; no VRAM means integrated |
| `8a9077a8cf7` radv: don't create a private SDMA blit queue without SDMA | no SDMA engine on Xclipse |
| `0fb1ba88259` radv,aco: support Samsung Xclipse 920 shader ISA | `is_xclipse`, `gfx11_isa_encoding`: ACO emits GFX11 encodings, wave64, no `s_delay_alu` |
| `0b4c08f06b2` ac,radv: GFX11 resource formats and packed local IDs | `ac_rsrc_gfx_level()` for all descriptors |
| `de8fdfb6310` radv: lower PS exports for the GFX11 ISA | packed color exports without COMPR |
| `3b6c2cadd7f` radv/amdgpu: NO_CPU_ACCESS only with VRAM | Termux:X11 mmaps DRI3 buffers |
| `8b32752103c` ac/descriptors: GFX11 image descriptor fields | MAX_MIP/MIN_LOD placement, no RESOURCE_LEVEL; fixed texture sampling |
| `1f3de41eb30` aco: no VOPD on encoding-only GFX11 | VOPD in wave32 hung the GPU |
| `42b96405c78` aco,ac/nir,radv: fix pixel shader discard | **VM bit** on final export; discard exit clears exec + `s_endpgm 1`; MRTZ after colors |
| `c4e2f53fbb6` radv: drop depth/stencil exports with early fragment tests | Z export + DEPTH_BEFORE_SHADER hung the GPU |
| `d0dd42ec1a3` ac,radv: GFX11 occlusion query events | ZPASS_DONE is ignored; all 64 occlusion_query tests pass |
| `04121b95452` ac/gpu_info: NGG passthrough without GS_ALLOC_REQ | large points hung; all 308 clipping tests pass |
| `e9f9af9ad21` radv: implicit primitive ID per vertex | per-primitive export hung fast-linked pipelines |
| `9eab0365907` radv: GFX11 vertex formats in vertex buffer descriptors | dynamic vertex input (Zink, ANGLE) read every attribute as 0: no GL/WebGL draws |
| `90a21ed6495` radv: emulate CLEAR_STATE on Xclipse | the kernel never sets up the clear state buffer; registers RADV assumed reset kept the previous GPU user's values → random hangs at process start |
| `a7a4d4335de` ac,radv: program Xclipse like Samsung's driver | no late alloc / PC oversubscription, smaller PC dealloc limits, no binning, no RB alternation, no CP DMA prefetch, EMIT_MEM_SYNC, HIGH ctx priority; made the random hang rarer |
| `141b5bda172` radv/amdgpu: don't use a user fence on Xclipse | the kernel's GART TLB flush for the user fence BO hit the power-gated GPU and reset the phone: **the** random reset at process start (§6.0) |

Tools/doc commits: `b26c163a650`, `694bbf055d8`, `cedab838001`, `8b6837dfc17`, `8073294ac3e`,
`14d834a0528`, `4e5e040452a`.

All Xclipse-specific behaviour is gated on `radeon_info::is_xclipse`
(`family == CHIP_VANGOGH && !pci.valid`) or `compiler_info.gfx11_isa_encoding`, so normal AMD GPUs
are unaffected.

---

## 3. Setting up a new computer

### 3.1 Host requirements

- x86_64 Linux with Docker (running, and the user in the `docker` group; otherwise run the scripts
  with `DOCKER="sudo docker"`), `git`, `python3`, `llvm-mc` (LLVM, for disassembling shaders),
  `curl`, `unzip`, `adb`, `nmap`.
- About 30 GB of disk for builds (CTS builds are large).

### 3.2 Get the code and build images

```bash
git clone ssh://git@git.miuthecat.uk:63000/miu/mesa-xclipse-radv.git mesa-for-android-container
cd mesa-for-android-container && git checkout adreno-main
cd .github/tools/xclipse
DOCKER="sudo docker" docker/build-images.sh        # mesa-cross-arm64 and mesa-cross-arm64-gl
```

Gitea SSH listens on port **63000** (port 22 on that host is the machine's own OpenSSH). Add the
new computer's public key in Gitea → Settings → SSH Keys.

### 3.3 Android NDK (for Samsung-driver tools and the Android CTS)

```bash
mkdir -p ~/xclipse-build && cd ~/xclipse-build
curl -sSLO https://dl.google.com/android/repository/android-ndk-r27c-linux.zip && unzip -q android-ndk-r27c-linux.zip
```

Keep build directories **out of `/tmp`** (tmpfs is wiped on reboot); the scripts default to a
`WORK` directory under `$HOME`.

### 3.4 Reach the phone

**Preferred: USB.** With the phone on a USB data cable (USB debugging allowed for this computer),
`ssh phone` goes through an adb port forward, so IP changes don't matter:

```
Host phone
    HostName 127.0.0.1
    Port 8022
    User u0_a138
    IdentityFile ~/.ssh/xclipse_phone
    IdentitiesOnly yes
    HostKeyAlias xclipse-phone
    ProxyCommand sh -c 'adb -s <serial> forward tcp:8022 tcp:8022 >/dev/null && exec nc 127.0.0.1 8022'
```

After every reboot: unlock the phone, then `ADB_SERIAL=<serial> scripts/phone-up.sh` starts sshd by
typing into Termux over adb (with retries; right after boot typed input can get lost).

Wi-Fi details (still useful without a cable):

- Phone: Termux, `pkg install openssh proot-distro`, Debian 13 container
  (`proot-distro install debian`). The installed Termux is the **Google Play build**
  (user `u0_a138`).
- Add the new computer's key to `~/.ssh/authorized_keys` in Termux, then start `termux-wake-lock; sshd`.
  Use a key **without a passphrase** for the phone (e.g. `ssh-keygen -t ed25519 -N "" -f
  ~/.ssh/xclipse_phone`): the scripts run ssh non-interactively.
- `~/.ssh/config` on the computer:

  ```
  Host phone
      HostName 192.168.0.X
      Port 8022
      User u0_a138
      IdentityFile ~/.ssh/xclipse_phone
      IdentitiesOnly yes
  ```

- **Wireless adb** (no USB needed) makes this possible without touching Termux: Developer options
  → Wireless debugging → *Pair device with pairing code*, then on the computer
  `adb pair <ip>:<pairing port> <code>` (once per computer). The connect port changes every time
  wireless debugging restarts, and Arch's `adb` has no mDNS support, so find it with
  `nmap -p 30000-49999 --open <ip>` and `adb connect <ip>:<port>`. With the screen unlocked, adb can
  start sshd by typing into Termux:

  ```bash
  adb shell monkey -p com.termux -c android.intent.category.LAUNCHER 1
  adb shell input text 'termux-wake-lock;sshd'; adb shell input keyevent 66   # spaces as %s
  ```

- **The phone's IP changes after every reboot** (MAC randomization). Find it with
  `.github/tools/xclipse/scripts/findphone.sh`, or:

  ```bash
  for i in $(seq 2 254); do (timeout 2 bash -c "echo > /dev/tcp/192.168.0.$i/8022" 2>/dev/null && echo 192.168.0.$i) & done; wait
  ```

  Setting the Wi-Fi network's "MAC address type" to "Phone MAC" on the phone avoids this.
  `scripts/findphone.sh` does the scan and updates the `Host phone` entry automatically.
- The Debian rootfs is at `$PREFIX/var/lib/proot-distro/containers/debian/rootfs` (not
  `installed-rootfs`). Copy files in through `proot-distro login debian -- tar -x ...`.

### 3.5 Build and install

```bash
cd .github/tools/xclipse
DOCKER="sudo docker" VK_LAYERS=overlay WORK=~/xclipse-build PHONE=phone scripts/build-radv.sh     # /opt/radv
DOCKER="sudo docker" WORK=~/xclipse-build PHONE=phone scripts/build-mesa-gl.sh                     # /opt/mesa-xclipse (Zink)
DOCKER="sudo docker" tests/build.sh glibc                                                         # tests for RADV
DOCKER="sudo docker" NDK=~/xclipse-build/android-ndk-r27c tests/build.sh android                 # tests for Samsung + libibdump.so
DOCKER="sudo docker" WORK=~/xclipse-build scripts/build-cts.sh linux                              # deqp-vk for Debian
DOCKER="sudo docker" WORK=~/xclipse-build NDK=~/xclipse-build/android-ndk-r27c scripts/build-cts.sh android
WORK=~/termux-x11-build scripts/build-termux-x11.sh                                              # DRI3 Termux:X11 APK
```

Note: `build-radv.sh` without `VK_LAYERS=overlay` reconfigures Meson without layers.

Already installed on the phone (survives reboots): `/opt/radv` (RADV + overlay layer +
`libnosubmit.so` + test binaries), `/opt/mesa-xclipse` (Zink GL), `/opt/cts` (Linux deqp-vk +
data + case lists), Termux `~/cts-android` (Android deqp-vk), `~/xt/bin` (Android tests and
`libibdump.so`), `~/capture-reference.sh`, `~/cts-run.sh`, `~/start-debian-radv.sh`.

### 3.6 Run things

```bash
~/start-debian-radv.sh vkcube                     # in Termux: X server + vkcube on RADV
# Zink (inside the Debian shell):
LD_LIBRARY_PATH=/opt/mesa-xclipse/lib LIBGL_DRIVERS_PATH=/opt/mesa-xclipse/lib/dri \
MESA_LOADER_DRIVER_OVERRIDE=zink MESA_SHADER_CACHE_DIR=~/.cache/mesa-xclipse-gl glmark2
# FPS overlay:
VK_ADD_LAYER_PATH=/opt/radv/layers VK_INSTANCE_LAYERS=VK_LAYER_MESA_overlay \
VK_LAYER_MESA_OVERLAY_CONFIG=fps,position=top-right vkcube --present_mode 0
```

If Termux:X11 says "already has an active X server connection", start it again. That message is
also printed when the server does come up; check for `$PREFIX/tmp/.X11-unix/X0` instead.
`scripts/x11-start.sh` (copied to Termux `~/x11-start.sh`) starts the server and checks it.

---

## 4. Debugging workflow (how every fix was found)

1. **Find a failing/hanging case.** Run a CTS group on RADV and on Samsung, then diff:

   ```bash
   # RADV, inside Debian (one job at a time when hunting hangs):
   cd /opt/cts && JOBS=1 ./cts-run.sh <group>.txt                     # results: /root/cts-results
   # Samsung, in Termux:
   CTS=~/cts-android OUT=~/cts-results-samsung \
   DEQP_EXTRA="--deqp-vk-library-path=/system/lib64/libvulkan.so" ~/cts-run.sh <group>.txt
   # On the computer:
   scripts/cts-diff.py radv.csv samsung.csv            # 'regress' = Samsung passes, RADV doesn't
   ```

   After a hang + reboot, `cts-run.sh` re-runs the interrupted chunk one case at a time; the next
   reboot pins the culprit (`Hang` in `results.csv`, RADV's IBs in `/root/cts-results/flight/`).
2. **Samsung's streams** for that case: `~/capture-reference.sh <case>` (Termux) →
   `~/cts-android/ref/<case>/` (IBs, shaders, BO snapshots, result).
3. **RADV's streams without touching the GPU:**

   ```bash
   LD_PRELOAD=/opt/radv/libnosubmit.so RADV_DEBUG=dumpibs,shaders,ir ./deqp-vk --deqp-case=<case>
   ```

4. **Compare.** IBs: native `ac_ib_parser gfx1033 ib_*.bin` (built in Docker, see
   `build-native/` in the old work dir). Shaders: `llvm-mc --disassemble -triple=amdgcn -mcpu=gfx1100`.
   Per-draw register state: `capture/draws.py` (DB/SPI/PA state per draw), `capture/regstate.py`
   (full register diff between two dumps at a chosen draw) and `capture/mkregfile.py` (Samsung's
   differing context registers as a `reg value` file, for bisecting).
5. **Minimal test** running on both drivers when CTS is too big (`vkdiscard`, `vkocc`, `vkprim`...).
6. Temporary experiment switches (`getenv("XCL_...")`) are fine while bisecting, but must be
   reverted before committing.
7. **After every reset, get the kernel log.** No root needed: an Android bugreport contains the
   previous boot's kernel log (`LAST KMSG`). `ADB_SERIAL=<serial> scripts/hang-report.sh` pulls it and
   prints the end (ACPM/ITMON/sgpu lines, the faulting address). `sgpu@16E00000` is the GPU register
   base in the device tree, so `0x16E0xxxx` addresses are GPU registers: offset / 4 is the dword
   address (GC segment 0 starts at `0x1260`), e.g. `0xA240` → `0x1260 + 0x1630` =
   `GCVM_INVALIDATE_ENG17_REQ` in the kernel's `gc_10_3_0_offset.h`.

Never set up Termux:Boot or any autostart that resumes GPU tests: it makes the phone reboot in a
loop right after Termux starts.

---

## 5. Vulkan CTS status

Case lists: `vulkan-cts-1.4.6.2`, `mustpass/main/vk-default` (98 groups, 3,247,552 cases).

Samsung's driver (the oracle) ran all 7,409 cases of `fragment-operations`, `clipping`,
`dynamic-state` and `memory`: **3,398 pass, 4,011 not supported, 0 failures**. So every RADV
failure in these groups is a RADV bug.

| Group | RADV now |
| :-- | :-- |
| `fragment-operations` (151) | no hangs; last full run 120 pass / 12 n/s / 19 fail, of which most were occlusion queries (now 64/64 pass); 5 MSAA sample-count cases still fail |
| `clipping` (308) | **308/308 pass** |
| `dynamic-state` (671) | partial: fast-linked hangs fixed; `image.clear`, `ds_state.stencil_params_advanced` (fast-linked) and `image.resolve` (both variants) fail |
| `memory` (6,279) | not run yet on RADV |
| everything else | not run |

The Samsung results for these 4 groups are on the phone in `~/cts-results-samsung/results.csv`.
RADV results in Debian `/root/cts-results/results.csv` are partial and partly from older builds:
**restart clean** (`rm -rf /root/cts-results`) after pulling the latest code.

---

## 6. Open problems (next steps, in priority order)

### 6.0 Random reset at process start (fixed 2026-10-10)

Symptom: at random, a RADV process made the phone reset instantly at its start (nothing in logcat).
Measured with `tests/vkloop.sh` (`DYN=1 vkloop.sh 100 /opt/radv/vkvbo`):

| Build | Runs before the reset |
| :-- | :-- |
| up to `9eab0365907` | 8, 8, 5, 1, 13, 2, 7 (≈ 1 in 6) |
| + CLEAR_STATE emulation (`90a21ed6495`) | 43, 18 |
| + Samsung's settings (`a7a4d4335de`) | 62, 117 (with 8 more Samsung register values, see below) |
| same, compute only (`vkloop.sh 300 /opt/radv/vkcomp store 1`) | 48 |
| + no user fence (`141b5bda172`) | **500/500 compute, 500/500 vkvbo** |
| Samsung's driver (`tests/samloop.sh 300`) | 300/300 |

**Cause.** It was never a GPU hang. The previous boot's kernel log (§4 step 7) shows the same
sequence every time:

1. `acpm_ipc timeout retry 1..5` from several threads: the ACPM power-management firmware stops
   answering.
2. ITMON: a CPU **write** to `G3D_P` address `0x16E0A240` (`GCVM_INVALIDATE_ENG17_REQ`) times out
   (`Configuration(SFR) Path transaction`), policy "s2d" → `WDTRESET right now!`.

sgpu uses invalidation engine 17 only for **GART** TLB flushes by MMIO (`gmc_v10_0_flush_vm_hub`,
called from `amdgpu_gart_bind/unbind`). The only GART-bound buffer from userspace is the **user
fence BO** of the CS fence chunk (`amdgpu_cs.c`: `amdgpu_ttm_alloc_gart(uf)`): RADV sent one with
every submission, Samsung's driver never does. When the BO is unbound (context destroyed / process
exit), the TLB flush can land while sgpu has power-gated the GPU between jobs (IFPO,
`vangogh_lite_ifpo_power_off` after the last job), and the phone resets. RADV never reads the user
fence, so `141b5bda172` drops it on Xclipse.

What this means for the earlier work: the GPU-state fixes (`90a21ed6495`, `a7a4d4335de`) made the
reset rarer, most likely by changing the timing of the first jobs, not by fixing a GPU hang. They match
Samsung's driver and stay. Also tried on 2026-10-10 without effect on the rate (patch in
`.github/tools/xclipse/experiments/`): NGG in wave32, `GE_PC_ALLOC = 0`, legacy-SC
binner state, `GE_NGG_SUBGRP_CNTL.THDS_PER_SUBGRP = 256`, `SPI_SHADER_REQ_CTRL_VS = 7`,
`CB_RMI_GL2_CACHE_CONTROL`, NGG `VGT_GS_MODE`, `PA_SC_BINNER_CNTL_1/_2`. All of them still draw
correctly (NGG wave32 included, which is interesting for §6.4).

Method that worked: a loop that logs every start/result with `sync`, then `scripts/hang-report.sh`
after the reset. Samsung's control loop: `tests/samloop.sh` (Termux, `~/samloop.sh` on the phone).

### 6.1 Fast-linked pipelines draw nothing (`dynamic_state.fast_linked_library.*`)

- `image.clear` and `ds_state.stencil_params_advanced` fail ("Image verification failed": the result
  shows only the clear color), while the **monolithic** variants pass. `line_width.*` passes.
- Already ruled out: hang (fixed), `SPI_VS_OUT_CONFIG` (correct after the per-vertex prim ID fix),
  PS epilog VM bit (set by the assembler).
- Lead: the **PS main part → PS epilog** hand-off. The main part ends with
  `s_movk_i32 s3, 0x8000` (address hi), a `v_xor3_b32 v0, v0, s0, s0` and `s_setpc_b64 s[2:3]`;
  the epilog converts `v0..v3` and exports `mrt0 done`. Check: the epilog address in the PS user
  SGPRs, the VGPRs the epilog expects, and whether `s_setpc_b64` into another BO works (the jump
  target is in the 32-bit shader arena at `address32_hi`). Also compare with a simpler
  fast-linked test that passes. Samsung has no pipeline libraries to compare with, so build a
  minimal GPL test.
- Also check the VS side in GPL mode (non-passthrough NGG with `GS_ALLOC_REQ`, since passthrough is
  disabled when the primitive ID is exported): `vkprim` (monolithic, same path) works.

### 6.2 MSAA

- `dynamic_state.*.image.resolve` fails in both variants; 5
  `fragment_operations.early_fragment.sample_count_*` / `samplemask_*` tests fail (samples 2/4).
- Leads: FMASK descriptors still use GFX10.3 (`ac_build_fmask_descriptor` asserts `< GFX11`);
  MSAA image/CB layout; resolve meta shaders. Capture Samsung with a minimal MSAA resolve test.

### 6.3 Continue the CTS

1. Clean RADV run of `fragment-operations clipping dynamic-state memory`; diff with Samsung.
2. Then the priority groups in `/opt/cts/order.txt` (api, compute, glsl, draw, renderpasses...),
   running each group on Samsung first.
3. Groups likely to hang (ray tracing, mesh shaders): consider **not exposing** those features on
   Xclipse until verified (`radv_physical_device.c` feature/extension tables).

### 6.4 Wave32

All shaders are forced to wave64 (`radv_physical_device.c`, `is_xclipse`). The original wave32
hang was almost certainly VOPD (now disabled); Samsung uses wave32 for NGG and small compute
shaders. Re-test with wave32 allowed; it may improve performance.

### 6.5 Other

- `s_version` prefix (Samsung emits `s_version 5|W64/W32` in every shader): tested, no effect so
  far; keep in mind.
- Samsung programs `PA_SC_BINNER_CNTL_2` (`0x00c00800`) and zeroes `0x28060`/`0x286dc`; RADV leaves
  them alone. No visible effect found yet.
- radeonsi port (native GL without Zink): **done on the `opengl` branch** (2026-10-11), documented in
  its `xclipse-920-opengl.md`; the notes below were the plan. Motivation: Zink translates every GL call on the
  caller's thread; for OMORI (WebGL → ANGLE → GL → Zink → RADV) that is Chrome's GPU thread at ~67%
  of a core, one of the two biggest CPU costs left (the other is proot; see the `omori` branch doc
  §6). Skipping GL with ANGLE's Vulkan backend doesn't help: Zink's worker threads go away, but the
  total CPU stays the same and the GPU load goes up (2026-10-11, `omori` doc §6 6a); a native GL
  driver is the remaining way to cut that cost. Builds without LLVM already
  (`scripts/build-mesa-gl.sh`); radeonsi would need ACO and the
  same hybrid handling as RADV, most of it in the shared `src/amd/common` code but wired up
  separately in radeonsi:
  - GFX11 instruction encoding in ACO, wave64, no VOPD/`s_delay_alu` (shared with RADV via
    `gfx11_isa_encoding`);
  - GFX11 resource/vertex formats and image descriptor fields (`ac_rsrc_gfx_level()`);
  - PS export lowering (packed exports, VM bit, discard, MRTZ after colors), occlusion query events,
    NGG passthrough without GS_ALLOC_REQ, per-vertex primitive ID;
  - CLEAR_STATE emulation (the kernel has no clear state buffer) and Samsung's preamble settings;
  - **no user fence** in radeonsi's amdgpu winsys (`src/gallium/winsys/amdgpu`): the same GART TLB
    flush reset as RADV's (§6.0).
  Effort: days to weeks, with its own hangs to debug; test with glmark2/piglit against Zink.
- Docs: update `xclipse-920-testing.md` with the fixes from 2026-09-30 (this document has them).
- Upstreaming: the gating is clean, but upstream needs a better detection than "VanGogh on the
  platform bus" (e.g. the sgpu driver name or device ID `0x73a0`).

---

## 7. Useful facts

- Device info: `amdgpu 3.40.0`, family 144 (VGH), device id `0x73a0`, chip_rev `6291969`,
  external_rev 128, 1 SE / 1 SA / 6 CUs, 3 RBs (`enabled_rb_mask 0x7`), GFX 10.0 rings
  (4 gfx, 3 compute), MEC fw 26, PFP fw 28, ME fw 23, ~6 GB GTT.
- Samsung userspace: `/vendor/lib64/hw/vulkan.samsung.so`, `libdrm_sgpu.so`. Around a submission
  it uses `AMDGPU_IB_FLAG_EMIT_MEM_SYNC` on the first IB, ctx priority 512 (HIGH), BOs with
  `CPU_GTT_USWC | VM_ALWAYS_VALID`, and ioctls `0x5d..0x8c` that are **not** in the sgpu driver
  (another device), so not GPU-relevant.
- **sgpu kernel source**: `drivers/gpu/drm/samsung/sgpu/` in
  `github.com/AuroraRomTeam/android_kernel_samsung_exynos2200` (an amdgpu fork). Extra uapi:
  `DRM_AMDGPU_WGP_GATING` (0x5e), CS chunks `MEMTRACK_HTILE_WA` (0xA), `TIME`, `MIN_FREQ_LOCK`,
  `MODE1`, ctx flag `IFH`, IB flags `PERF_COUNTER`/`SQ_THREAD_TRACE`. IFPO (GPU power-off when idle)
  is done in the job path. No clear state buffer for MGFX (see §6.0).
- No `dmesg`, debugfs or sysfs access, even over ADB (stock Samsung build); kernel log buffer is
  empty. The capture tools are the only window into the GPU.
- After rebuilding RADV, delete the shader caches in the container (`/root/.cache/mesa_shader_cache*`,
  `/root/.cache/radv_builtin_shaders`): debug builds abort on "Mesa cache keys mismatch!".
- `RADV_DEBUG=hang` dumps go to `$HOME/radv_dumps_*` but only when the phone survives the hang.
- Debian `glibc` programs can't be started outside PRoot from Termux (app data is not executable
  without `termux-exec`), so in-process signal-based capture (`ibdump.c`) only works for Android
  builds; use `nosubmit.c` for RADV.
