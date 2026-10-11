# CTS reference results

`samsung/results.csv`: Samsung's own Vulkan driver on the Galaxy S23 FE (Android deqp-vk,
`scripts/cts-run.sh` with `DEQP_EXTRA=--deqp-vk-library-path=/system/lib64/libvulkan.so`), run on
2026-09-30: 3398 Pass, 4011 NotSupported. The baseline for `scripts/cts-diff.py radv.csv
samsung/results.csv` ("regress" = Samsung passes, RADV doesn't).
