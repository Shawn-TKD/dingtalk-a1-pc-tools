# A1 AP product reconstruction status

Generated from the V1.6.88 AP Ghidra function/call/string exports. These
numbers measure evidence handling, not source-code identity.

## Measured workset

- all AP functions: 18,361
- evidence-backed product/business seeds: 496
- direct call neighborhood: 1,128
- seeds with meaningful recovered names: 359 (72.4%)
- seeds copied into the focused export: 210 (42.3%)
- seeds with a written semantic review: 398 (80.2%)
- seeds tied to compatible-source traceability addresses: 156 (31.5%)

A successful Ghidra decompile is intentionally not counted as understood.
The 70–80% target uses written semantic reviews, not focused-file membership.
A second metric tracks which reviewed behaviors exist in compatible C.

## Highest-ranked unnamed, unreviewed functions

| score | address | bytes | product neighbors | example evidence |
| ---: | --- | ---: | ---: | --- |
| 236 | `0x103c92fc` | 822 | 315 | [%s.%03d][DTIOT][%s]%s():%d  |
| 221 | `0x1030cc2c` | 1818 | 17 | === Lenovo DTIOT OS HAL Test Application ===\r\n |
| 207 | `0x101c551c` | 466 | 1 | CRITICAL ERROR: sector_size (%d) > DTIOT_BUFFER_SIZE (%d) - would cause buffer overflow! |
| 183 | `0x1030bc4c` | 324 | 1 |   init           - Test DTIOT HAL initialization/deinitialization |
| 167 | `0x1030d534` | 16 | 0 | ===@-@ Lenovo DTIOT OS HAL Test Application ===\r\n |
| 146 | `0x103c6598` | 94 | 15 | call-graph adjacency |
| 143 | `0x103e007c` | 12 | 15 | call-graph adjacency |
| 123 | `0x103c5ec8` | 160 | 4 | call-graph adjacency |
| 121 | `0x103c5b60` | 54 | 4 | call-graph adjacency |
| 116 | `0x103c8638` | 28 | 3 | call-graph adjacency |
| 114 | `0x103e03a0` | 80 | 2 | call-graph adjacency |
| 113 | `0x1030e410` | 48 | 2 | call-graph adjacency |
| 111 | `0x1030e0ec` | 144 | 1 | call-graph adjacency |
| 111 | `0x103e04e0` | 12 | 2 | call-graph adjacency |
| 110 | `0x103ebf08` | 118 | 1 | call-graph adjacency |
| 109 | `0x1030e318` | 46 | 1 | call-graph adjacency |
| 109 | `0x103cf610` | 54 | 1 | call-graph adjacency |
| 109 | `0x103cf64c` | 54 | 1 | call-graph adjacency |
| 109 | `0x103cf688` | 54 | 1 | call-graph adjacency |
| 109 | `0x103ebf94` | 44 | 1 | call-graph adjacency |
| 108 | `0x102eac50` | 24 | 1 | call-graph adjacency |
| 108 | `0x103cf788` | 22 | 1 | call-graph adjacency |
| 108 | `0x103cf7a4` | 22 | 1 | call-graph adjacency |
| 108 | `0x103cf7c0` | 22 | 1 | call-graph adjacency |
| 107 | `0x101c5730` | 428 | 1 | CRITICAL ERROR: sector_size (%d) > DTIOT_BUFFER_SIZE (%d) - would cause buffer overflow! |
| 107 | `0x1030e220` | 14 | 1 | call-graph adjacency |
| 107 | `0x103e02b8` | 12 | 1 | call-graph adjacency |
| 106 | `0x102eac48` | 6 | 1 | call-graph adjacency |
| 99 | `0x103c9070` | 496 | 3 | [DTIOT]Failed to open fatal log file(%s)\n |
| 90 | `0x1030c67c` | 212 | 5 | Hello DTIOT Flash! |

Machine-readable queue: `analysis/ap-product-function-worklist.tsv`.
Re-run `build-product-worklist.py` after each naming/review pass.
