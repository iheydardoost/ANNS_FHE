# Master Architecture & Verification Plan

> **Target:** Complete End-to-End Hardware Simulation and Verification of the **Encrypted Interactive Search** mode for IVF-PQ ANNS over CKKS FHE.  
> **FPGA Platform:** AMD Virtex UltraScale+ (`xcvu37p-fsvh2892-2-e`)  
> **Toolchain:** Vitis HLS, Vivado, GCC 11+, Python 3.10+

---

## 1. Hardware Architecture & Primitives

All low-level hardware FHE compute primitives are implemented, verified bit-accurately, and synthesized:

* `mod_arith` (64-bit modular add, sub, mul with 128-bit Barrett reduction) — **100% Bit-Exact**
* `ntt` (Forward Cooley-Tukey DIF NTT) & `intt` (Inverse Gentleman-Sande INTT) — **100% Bit-Exact**
* `poly_arith` (Poly Add, Sub, Mul in NTT domain) — **100% Bit-Exact**
* `automorphism` (Galois coefficient permutation) — **100% Bit-Exact**
* `rescale` (Centered reduction + drop last limb) — **100% Bit-Exact**
* `key_switch` (Hybrid key switching with FastBasesConv, AXI streaming eval keys, ApproxModDown) — **100% Bit-Exact**
* `fhe_accel_top` (Integrated top-level kernel with 512-bit AXI memory burst interfaces and URAM storage) — **100% Bit-Exact**

---

## 2. System Architecture & Components

```
┌────────────────────────────────────────────────────────────────────────┐
│                        SYSTEM ARCHITECTURE MAP                         │
└────────────────────────────────────────────────────────────────────────┘

 [Application Layer: Interactive Query Manager]
   │ - IVF-PQ clustering logic (n_list=32, M=8, K=256, B=64)
   │ - Sub-quantizer codebook management
   ▼
 [FHE Host Driver: Composite Operations]
   │ - eval_sub_plain, eval_mult_relin_rescale, eval_rotate
   │ - Virtual DRAM memory map allocation
   ▼
 [Hardware Accelerator: fhe_accel_top]
   │ - Dual 512-bit AXI4 interfaces (poly_gmem, key_gmem)
   │ - 6x on-chip UltraRAM working banks (g_work_0 .. g_work_5)
   ▼
 [Target FPGA Device: AMD Virtex UltraScale+ xcvu37p]
```

### Component Breakdown

#### Component 1: Deterministic Test Vector Infrastructure (`integration_tools/`)
* **Tool:** OpenFHE C++ Generator (`integration_tools/generate_interactive_test_vectors.cpp`).
* **Objective:** Produce deterministic golden binary test vectors capturing every intermediate state of the Encrypted Interactive Search flow.
* **Validator:** Standalone Python verification script (`integration_tools/verify_interactive_trace.py`).

#### Component 2: Host Orchestrator & Composite Driver (`cpp_host_model/`)
* **Memory Bus Bridge:** `cpp_host_model/src/sim_memory_bus.h`, `sim_memory_bus.cpp` providing 512-bit beat read/write access.
* **Driver:** `cpp_host_model/src/fhe_host_driver.h`, `fhe_host_driver.cpp` invoking `fhe_accel_top()` sequentially.
* **Query Manager:** `cpp_host_model/src/interactive_query_manager.h`, `interactive_query_manager.cpp` orchestrating Phase 1 $\to$ Handshake $\to$ Phase 2.

#### Component 3: End-to-End System Simulation & Verification
* **Test Runner:** `cpp_host_model/src/main_sim.cpp`.
* **Verification Protocol:** Executes full verification across 6 checkpoints (CP1 to CP6).

---

## 3. Verification Checkpoints Matrix

```
┌────────────────────────────────────────────────────────────────────────┐
│                        VERIFICATION CHECKPOINTS                        │
├──────┬──────────────────────────────┬──────────────────────────────────┤
│ CP1  │ Context & Key Material       │ Bit-exact match on all tables    │
├──────┼──────────────────────────────┼──────────────────────────────────┤
│ CP2  │ Coarse Centroid Distance     │ Bit-exact match on ct_diff, ct_sq│
├──────┼──────────────────────────────┼──────────────────────────────────┤
│ CP3  │ Distance Compaction (32x)    │ Bit-exact match on ct_compact    │
├──────┼──────────────────────────────┼──────────────────────────────────┤
│ CP4  │ Interactive Client Handshake │ Selected cluster ID matches SW   │
├──────┼──────────────────────────────┼──────────────────────────────────┤
│ CP5  │ Fine Candidate Batch Distance│ Bit-exact match on ct_all_dists  │
├──────┼──────────────────────────────┼──────────────────────────────────┤
│ CP6  │ Top-K Ground Truth Recall    │ Recall@8 matches ground truth    │
└──────┴──────────────────────────────┴──────────────────────────────────┘
```

---

## 4. Hardware Characterization & Experimental Metrics

1. **Hardware Resource Utilization** (Post-synthesis on `xcvu37p`):
   * LUTs, FFs, BRAMs, URAMs, DSP48E2 utilization and percentage of device budget.
2. **Timing Closure**:
   * Target clock period: 5.0 ns (200 MHz).
3. **Latency & Throughput Breakdown**:
   * Cycle count and execution time for Coarse Distance (Phase 1), Compaction (Phase 1b), and PQ ADC (Phase 2).
   * Comparison against single-threaded and multi-threaded CPU Golden Model.
4. **Energy Efficiency**:
   * Energy per query (Joules/query) for FPGA vs. CPU server baseline.
5. **Retrieval Accuracy**:
   * Validation of Recall@K matching unencrypted IVF-PQ ground truth.
