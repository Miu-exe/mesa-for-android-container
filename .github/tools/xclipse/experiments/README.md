# Experiment patches

Driver experiments from the hunt for the random reset at process start (handoff §6.0), kept as
patches because they were not committed. Apply on top of the commit they were made on with
`git apply`; each was measured with `tests/vkloop.sh`.

| Patch | Base | What |
| :-- | :-- | :-- |
| `experiments-latealloc-memsync-prio-prefetch.patch` | `3c3412b991b` | Switches for late alloc, EMIT_MEM_SYNC, HIGH ctx priority and CP DMA prefetch, each tested alone (no effect on the rate) |
| `combined-samsung-match.patch` | `3c3412b991b` | All Samsung-like settings together; became `6438036149e` |
| `experiments-samsung-regs-exp-mask.patch` | (a test branch) | `RADV_XCLIPSE_EXP=<mask>` switches for 8 more Samsung register values (NGG wave32, GE_PC_ALLOC, binner, ...); no effect on the rate, all render correctly |

The actual cause was the user fence BO (`a1f6895af6a`).
