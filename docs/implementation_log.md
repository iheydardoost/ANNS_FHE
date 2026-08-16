# Engineering Implementation & Verification Log

This document records the implementation, integration, and verification of the FPGA hardware accelerator and simulation framework for **Encrypted Interactive IVF-PQ Search** over CKKS FHE.

---

## 1. Cryptographic Test Vector Generation & Infrastructure (`integration_tools/`)

* **OpenFHE Integration**:
  * Configured OpenFHE `CryptoContext` matching exact hardware parameters ($N=16384$, $\text{scalingModSize}=50$, $\text{firstModSize}=55$, 11 $Q$ primes, 4 $P$ primes).
  * Implemented `integration_tools/generate_interactive_test_vectors.cpp` and `integration_tools/test_vector_generator.cpp`.
* **Key Material & Tables Serialized**:
  * Relinearization Key (`evalkey_mult_a_*.bin`, `evalkey_mult_b_*.bin`, 3 digits $\times$ 15 limbs).
  * 27 Rotation Keys (`rotkey_step*_a_*.bin`, `rotkey_step*_b_*.bin`, 3 digits $\times$ 14 limbs for Level 1).
  * 27 Galois Automorphism Maps (`auto_map_step*.bin`, packed $N/2$ 64-bit words).
  * RNS/Barrett tables (`barrett_constants.bin`, `n_inv.bin`, `twiddles.bin`, `inv_twiddles.bin`, FastBasesConv & ApproxModDown tables).
* **Validation**:
  * Implemented standalone `integration_tools/verify_interactive_trace.py` for shape and non-zero polynomial integrity verification.

---

## 2. Hardware FHE Accelerator Core (`hardware_fpga_model/hls/`)

All core arithmetic and FHE transformation engines implemented in synthesizable C++ (Vitis HLS):

* `mod_arith.cpp` / `mod_arith.h`: 64-bit modular addition, subtraction, and multiplication with 128-bit Barrett reduction.
* `ntt.cpp` / `ntt.h`: Forward Cooley-Tukey DIF NTT and Inverse Gentleman-Sande DIT INTT.
* `poly_arith.cpp` / `poly_arith.h`: Vectorized component-wise addition, subtraction, and multiplication in the NTT domain.
* `automorphism.cpp` / `automorphism.h`: Galois permutation engine mapping polynomial coefficients for slot rotation.
* `rescale.cpp` / `rescale.h`: Rescaling engine performing centered reduction, dropped-limb INTT, and modulus-switching division.
* `key_switch.cpp` / `key_switch.h`: Hybrid key-switching engine featuring digit decomposition, INTT, FastBasesConv ($Q \to Q \cup P$), evaluation key dot-product accumulation, and ApproxModDown.
* `fhe_accel_top.cpp` / `fhe_accel_top.h`: Top-level AXI master kernel integrating 512-bit burst memory engines and 6 dual-port UltraRAM working banks.

### Unit Testbench Verification Matrix:
* `tb_mod_arith`: **100% Bit-Exact Passed**
* `tb_ntt`: **100% Bit-Exact Passed** (180,224 / 180,224 coefficients)
* `tb_intt`: **100% Bit-Exact Passed** (180,224 / 180,224 coefficients)
* `tb_poly_arith`: **100% Bit-Exact Passed**
* `tb_automorphism`: **100% Bit-Exact Passed**
* `tb_rescale`: **100% Bit-Exact Passed** (163,840 / 163,840 coefficients)
* `tb_keyswitch`: **100% Bit-Exact Passed** (All partitions, FBC, ApproxModDown)
* `tb_fhe_accel` (Top Integration): **100% Bit-Exact Passed**

---

## 3. Host Orchestration & Hardware Simulation (`cpp_host_model/`)

* **Virtual DRAM Bus (`src/sim_memory_bus.h`, `src/sim_memory_bus.cpp`)**:
  * Manages 64 MB `poly_gmem` (AXI `gmem0`) and 512 MB `key_gmem` (AXI `gmem1`) with 512-bit word-aligned burst access.
* **FHE Host Driver (`src/fhe_host_driver.h`, `src/fhe_host_driver.cpp`)**:
  * Implements context loading (`OP_LOAD_CONTEXT`, OP 99).
  * Provides hardware primitive wrappers (NTT, INTT, PolyAdd, PolySub, PolyMul, Automorphism, KeySwitch, Rescale).
  * Implements composite FHE pipelines (`eval_add`, `eval_sub_plain`, `eval_mult_relin_rescale`, `eval_mult_plain_rescale`, `eval_rotate`).
* **Interactive Query Manager (`src/interactive_query_manager.h`, `src/interactive_query_manager.cpp`)**:
  * `stage1_coarse_centroid_distance`: Level 0 plain subtraction, relinearization, rescaling, and 7-step tree-sum across 128 dimensions.
  * `stage1b_distance_compaction`: 32-step masked extraction and power-of-2 shift-addition packing centroid distances into slots 0–31.
  * `stage2_client_cluster_selection`: Simulates client-side decryption and `argmin` cluster selection.
  * `stage3_fine_candidate_batch_distance`: Processes candidates in batches of $B=64$, evaluating PQ ADC lookups and dimension accumulation.
  * `stage4_client_top_k`: Client-side decryption and extraction of final Top-8 nearest neighbors.
* **Top-Level Simulation Executable (`src/main_sim.cpp`)**:
  * Runs the complete interactive query search and verifies all 6 checkpoints.

---

## 4. End-to-End Verification Checkpoint Results

| Checkpoint | Description | Result |
| :--- | :--- | :---: |
| **CP1** | Hardware Context & Key Material Initialized (OP 99) | **PASS** |
| **CP2.1** | Coarse Distance Subtraction (`ct_diff.c0`, `ct_diff.c1`, 180,224 words) | **100% Bit-Exact PASS** |
| **CP3** | 32-Step Distance Compaction (`ct_compact.c0`, `ct_compact.c1`, 147,456 words) | **100% Bit-Exact PASS** |
| **CP4** | Client Cluster Selection Handshake (Cluster 31) | **PASS** |
| **CP5** | Fine Candidate ADC Distance Accumulation (`ct_all_dists.c0`, `ct_all_dists.c1`, 147,456 words) | **100% Bit-Exact PASS** |
| **CP6** | Client Top-8 Extraction & Ground Truth Matching | **100% Bit-Exact PASS (8/8 hits, 100% Recall@8)** |

---

## 5. Hardware Synthesis & Performance Summary (`xcvu37p-fsvh2892-2-e`)

* **Synthesis Tool**: AMD Vitis HLS 2025.2
* **Target Clock**: 5.00 ns (200.00 MHz)
* **Resource Utilization**:
  * **LUTs**: 171,181 / 1,303,680 (13.13%)
  * **Flip-Flops**: 109,393 / 2,607,360 (4.20%)
  * **BRAM (18K)**: 832 / 4,032 (20.63%)
  * **UltraRAM (URAM 288K)**: 384 / 960 (40.00%)
  * **DSP48E2**: 881 / 9,024 (9.76%)
* **Performance Speedup**: **25.8$\times$ faster** than CPU server baseline (137.5 ms vs 3,544.0 ms).
* **Energy Efficiency**: **203.4$\times$ lower energy per query** (3.92 J vs 797.4 J).
