# Hardware Memory Architecture & Interface Specification

> **Target Architecture:** Vitis HLS Prototype on AMD Virtex UltraScale+ (`xcvu37p-fsvh2892-2-e`)  
> **Source Reference:** `hardware_fpga_model/hls/src/fhe_accel_top.h`, `fhe_accel_top.cpp`, `hls_params.h`

---

## 1. Memory Hierarchy Overview

```
                                  GLOBAL MEMORY (DDR4 / HBM2e)
                                  
   AXI4-MM gmem0 (512-bit Data Bus)                   AXI4-MM gmem1 (512-bit Data Bus)
   [Polynomial & Ciphertext Buffer]                   [Evaluation Keys & Twiddle Tables]
   ├─ ct_query (Replicated Query)                     ├─ Twiddle Factors Q (11 limbs)
   ├─ pt_centroids (Packed Centroids)                 ├─ Twiddle Factors P (4 limbs)
   ├─ ct_diff / ct_sq (Intermediates)                 ├─ Inverse Twiddle Factors Q & P
   ├─ pt_mask / pt_batch (Plaintext Batches)          ├─ FastBasesConv & ApproxModDown Tables
   ├─ auto_map (Galois Permutation Indices)           ├─ Rescale CRT Conversion Constants
   └─ ct_compact / ct_all_dists (Output Ciphertexts)  └─ Streamed Evaluation Keys (Relin & Rotation)
                                 │                                    │
                                 │ AXI Burst Ingest                   │ AXI Burst Stream
                                 ▼                                    ▼
   ┌────────────────────────────── ON-CHIP ACCELERATOR STORAGE ──────────────────────────────┐
   │                                                                                         │
   │  UltraRAM (URAM) Working Buffers [~11.8 MB]       Block RAM (BRAM) Storage [~3.9 MB]    │
   │  (6 Buffers × 15 Limbs × 16384 Coeffs × 8 B)      (Read-Only Context Precomputed Tables)│
   │  ├─ g_work_0: Operand A / In-place compute buffer ├─ g_twiddles_q: Forward Twiddles (Q) │
   │  ├─ g_work_1: Operand B / Digits / partsCt        ├─ g_inv_twiddles_q: Inv Twiddles (Q) │
   │  ├─ g_work_2: Output / partsCtCompl / Rescale tmp ├─ g_twiddles_p: Forward Twiddles (P) │
   │  ├─ g_work_3: KeySwitch ext c0 accumulator        ├─ g_inv_twiddles_p: Inv Twiddles (P) │
   │  ├─ g_work_4: KeySwitch ext c1 accumulator        └─ CRT & Rescale lookup buffers       │
   │  └─ g_work_5: Isolated result c_out_1                                                   │
   │                                                   Registers & DSP Multiplexers          │
   │  • Directives:                                    ├─ RNS Moduli (rns_primes, p_primes)  │
   │    #pragma HLS BIND_STORAGE impl=uram             ├─ Barrett Multipliers (ap_uint<128>) │
   │    #pragma HLS ARRAY_PARTITION cyclic factor=8    └─ AXI-Lite Scalar Control Registers  │
   └─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. AXI Global Memory Layout

All transfers over AXI interfaces operate in **512-bit beats** (8 $\times$ 64-bit words per beat). Every allocated slot is aligned to an 8-word boundary.

### 2.1 `poly_gmem` Layout (AXI Master Bundle `gmem0`)

```
Word Offset (64-bit words)      Size (Words)            Contents
──────────────────────────────────────────────────────────────────────────────────────────────
0x0000_0000 (0)                 11 × 16384 (180,224)    ct_query.c0 (Replicated query, Level 0)
0x0002_C000 (180,224)           11 × 16384 (180,224)    ct_query.c1
0x0005_8000 (360,448)           11 × 16384 (180,224)    pt_centroids (Plaintext centroids)
0x0008_4000 (540,672)           11 × 16384 (180,224)    ct_diff.c0 / ct_sq.c0 (Workspace 1)
0x000B_0000 (720,896)           11 × 16384 (180,224)    ct_diff.c1 / ct_sq.c1 (Workspace 1)
0x000D_C000 (901,120)           11 × 16384 (180,224)    Workspace 2 (c0 for rotations/intermediates)
0x0010_8000 (1,081,344)         11 × 16384 (180,224)    Workspace 2 (c1 for rotations/intermediates)
0x0013_4000 (1,261,568)         11 × 16384 (180,224)    ct_query_dimpack.c0 (Dimension-major query)
0x0016_0000 (1,441,792)         11 × 16384 (180,224)    ct_query_dimpack.c1
0x0018_C000 (1,622,016)         11 × 16384 (180,224)    pt_batch_current (Current candidate batch PT)
0x001B_8000 (1,802,240)         11 × 16384 (180,224)    pt_mask_current (Validity/Compaction mask PT)
0x001E_4000 (1,982,464)         9 × 16384 (147,456)     ct_compact.c0 (Coarse result, Level 2)
0x0020_8000 (2,129,920)         9 × 16384 (147,456)     ct_compact.c1
0x0022_C000 (2,277,376)         9 × 16384 (147,456)     ct_all_dists.c0 (Accumulated fine dists, Level 2)
0x0025_0000 (2,424,832)         9 × 16384 (147,456)     ct_all_dists.c1
0x0027_4000 (2,572,288)         8,192 words (64 KB)     auto_map (Galois permutation index table)
──────────────────────────────────────────────────────────────────────────────────────────────
Total poly_gmem footprint: ~21 MB (comfortably fits in 32 MB allocated simulation memory)
```

---

### 2.2 `key_gmem` Layout (AXI Master Bundle `gmem1`)

`key_gmem` holds all read-only precomputed context tables and large evaluation keys streamed during key switching:

```
Word Offset (64-bit words)      Size (Words)            Contents
──────────────────────────────────────────────────────────────────────────────────────────────
0x0000_0000 (0)                 11 × 16384 (180,224)    twiddles_q (Forward NTT twiddles for Q)
0x0002_C000 (180,224)           11 × 16384 (180,224)    inv_twiddles_q (Inverse NTT twiddles for Q)
0x0005_8000 (360,448)           4 × 16384 (65,536)      twiddles_p (Forward NTT twiddles for P)
0x0006_8000 (425,984)           4 × 16384 (65,536)      inv_twiddles_p (Inverse NTT twiddles for P)
0x0007_8000 (491,520)           8 words                 qHatInvModq_0 & qHatModp_0 (Partition 0)
0x0007_8040 (491,584)           8 words                 qHatInvModq_1 & qHatModp_1 (Partition 1)
0x0007_8080 (491,648)           8 words                 qHatInvModq_2 & qHatModp_2 (Partition 2)
0x0007_80C0 (491,712)           16 words                PInvModq (ApproxModDown table)
0x0007_8100 (491,776)           8 words                 PHatInvModp (ApproxModDown table)
0x0007_8140 (491,840)           64 words                PHatModq (ApproxModDown table)
0x0007_8200 (492,032)           16 words                QlQlInvModqlDivqlModq (Rescale table)
0x0007_8240 (492,096)           16 words                qlInvModq (Rescale table)
0x0008_0000 (524,288)           3 × 14 × 16384 × 2      RelinKey (evk_a and evk_b, 1,376,256 words ≈ 11 MB)
                                (1,376,256 words)
0x001D_0000 (1,900,544)         85 × 1,376,256 words    Rotation Keys (85 keys × 11 MB ≈ 935 MB)
                                (~117M words)
──────────────────────────────────────────────────────────────────────────────────────────────
```

---

## 3. On-Chip Storage Architecture

### 3.1 UltraRAM (URAM) Polynomial Workspace
Six independent static buffers (`g_work_0` to `g_work_5`) are instantiated in UltraRAM:

```cpp
// fhe_accel_top.cpp
#pragma HLS BIND_STORAGE variable=g_work_0 type=ram_2p impl=uram
#pragma HLS ARRAY_PARTITION variable=g_work_0 cyclic factor=8 dim=1
...
#pragma HLS BIND_STORAGE variable=g_work_5 type=ram_2p impl=uram
#pragma HLS ARRAY_PARTITION variable=g_work_5 cyclic factor=8 dim=1
```

* **Capacity per buffer**: $\text{MAX\_LIMBS} \times N = 15 \times 16384 = 245,760 \text{ words} = 1.966 \text{ MB}$.
* **Total URAM footprint**: $6 \times 1.966 \text{ MB} \approx 11.8 \text{ MB}$ ($40\%$ of available URAM on `xcvu37p`).
* **Cyclic Partitioning**: Factor 8 allows full 512-bit parallel reads and writes per clock cycle, achieving $\text{II}=1$ in `burst_load`, `burst_store`, and coefficient loops.

#### Workspace Buffer Assignment Matrix

| Hardware Operation | `g_work_0` | `g_work_1` | `g_work_2` | `g_work_3` | `g_work_4` | `g_work_5` |
|---|---|---|---|---|---|---|
| `OP_NTT` | In/Out Poly | — | — | — | — | — |
| `OP_INTT` | In/Out Poly | — | — | — | — | — |
| `OP_POLY_ADD/SUB` | Operand A | Operand B | Output Result | — | — | — |
| `OP_POLY_MUL_NTT` | Operand A | Operand B | Output Result | — | — | — |
| `OP_AUTOMORPHISM` | Source Poly | Dest Poly | — | `auto_map` | — | — |
| `OP_RESCALE` | $c_0$ in | $c_1$ in | $c'_0$ out | $c'_1$ out | — | — |
| `OP_KEY_SWITCH` | Input $c_1$ / $c'_0$ out | `partsCt` / Digits | `partsCtCompl` | $c_{\text{ext}, 0}$ Accumulator | $c_{\text{ext}, 1}$ Accumulator | $c'_1$ out |

> [!NOTE]
> `g_work_5` completely eliminates buffer aliasing during Key Switching, guaranteeing that `approx_mod_down` on $c_0$ and $c_1$ write to distinct memory regions without overwriting input data.

---

### 3.2 Block RAM (BRAM) Twiddle Factor ROMs
Forward and inverse twiddles for all 11 $Q$ primes and 4 $P$ primes are preloaded into dual-port Block RAMs:

```cpp
#pragma HLS BIND_STORAGE variable=g_twiddles_q type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=g_inv_twiddles_q type=ram_2p impl=bram
```

* **Size**: $15 \times 16384 \times 8 \text{ B} = 1.966 \text{ MB}$ each (Total BRAM: ~3.93 MB, 20.6% of FPGA BRAM).
* Loaded once at system start via `OP_LOAD_CONTEXT`.

---

## 4. Hardware Top-Level Control Interface (AXI-Lite)

The accelerator is controlled via scalar registers mapped to AXI-Lite (`s_axilite` bundle `control`):

```cpp
extern "C" void fhe_accel_top(
    ap_uint<512>* poly_gmem,       // m_axi bundle=gmem0, offset=slave
    ap_uint<512>* key_gmem,        // m_axi bundle=gmem1, offset=slave

    uint32_t op_code,              // s_axilite: Operation Opcode (1..8, 99)
    uint32_t src_a_offset,         // s_axilite: Word offset in poly_gmem
    uint32_t src_b_offset,         // s_axilite: Word offset in poly_gmem
    uint32_t dst_offset,           // s_axilite: Word offset in poly_gmem
    uint64_t evk_offset,           // s_axilite: Word offset in key_gmem
    uint32_t active_limbs,         // s_axilite: Current active limbs (L_ℓ)
    uint32_t prime_idx,            // s_axilite: Single prime index
    uint32_t galois_elt,           // s_axilite: Galois rotation element
    
    uint32_t sizeQl,               // s_axilite: Number of Q primes (10)
    uint32_t sizeP,                // s_axilite: Number of P primes (4)
    uint32_t numPartQ,             // s_axilite: Partitions (3)
    uint32_t alpha,                // s_axilite: Limbs per partition (4)

    uint64_t rns_primes[MAX_LIMBS],// s_axilite: 15 Q moduli
    uint64_t p_primes[4],          // s_axilite: 4 P moduli
    ap_uint<128> barrett_m_q[15],  // s_axilite: Barrett multipliers (Q)
    uint32_t barrett_k_q[15],      // s_axilite: Barrett shifts (Q)
    ap_uint<128> barrett_m_p[4],   // s_axilite: Barrett multipliers (P)
    uint32_t barrett_k_p[4],       // s_axilite: Barrett shifts (P)
    uint64_t n_inv_q[15],          // s_axilite: N^{-1} mod q_i
    uint64_t n_inv_p[4],           // s_axilite: N^{-1} mod p_j
    uint64_t n_inv_mod_q_last,     // s_axilite: N^{-1} mod q_{last}
    
    /* Table offsets in key_gmem */
    uint32_t twiddles_q_offset,
    uint32_t inv_twiddles_q_offset,
    uint32_t twiddles_p_offset,
    uint32_t inv_twiddles_p_offset,
    uint32_t qHatInvModq_0_offset, uint32_t qHatModp_0_offset,
    uint32_t qHatInvModq_1_offset, uint32_t qHatModp_1_offset,
    uint32_t qHatInvModq_2_offset, uint32_t qHatModp_2_offset,
    uint32_t PInvModq_offset,
    uint32_t PHatInvModp_offset,
    uint32_t PHatModq_offset,
    uint32_t QlQlInvModqlDivqlModq_offset,
    uint32_t qlInvModq_offset
);
```

---

## 5. Evaluation Key Streaming Protocol

During `OP_KEY_SWITCH`:
1. The kernel calculates the starting address of the target Evaluation Key in `key_gmem` using `evk_offset`.
2. For each of the `numPartQ = 3` digits:
   * Streams polynomial component $\text{evk\_a}[j]$ in 512-bit bursts: $14 \times 16384 = 229,376$ words.
   * Multiplies $\text{digit}[j] \times \text{evk\_a}[j]$ and accumulates into `g_work_3` ($c_{\text{ext}, 0}$).
   * Streams polynomial component $\text{evk\_b}[j]$ in 512-bit bursts: 229,376 words.
   * Multiplies $\text{digit}[j] \times \text{evk\_b}[j]$ and accumulates into `g_work_4` ($c_{\text{ext}, 1}$).
3. This streaming model allows the accelerator to process 700+ MB of rotation keys with zero on-chip key storage penalty.
