# Hardware Implementation & Verification Methodology

> High-Level Synthesis (Vitis HLS) design strategy, compute core architecture, and verification methodology for the ANNS_FHE accelerator.

---

## 1. Design Strategy: HLS-First Methodology

| Manual RTL Flow | Vitis HLS Flow (Selected) |
| :--- | :--- |
| Write every butterfly, FSM, address generator manually | Synthesizable C++ with fine-grained HLS pragmas |
| Debug at RTL waveform level | Fast functional debug in C simulation (`csim`) |
| Handcrafted SystemVerilog testbenches per module | Unified C++ testbenches for both `csim` and `cosim` |
| Custom AXI memory burst wrappers | Automated AXI4 master burst generation via `#pragma HLS INTERFACE` |

---

## 2. Hardware Module Architecture

### 2.1 Low-Level Arithmetic & Transform Hierarchy

```
hardware_fpga_model/hls/src/
├── hls_params.h          # Compile-time hardware constants (N=16384, alpha=4, max_limbs=15)
├── mod_arith.h/cpp       # 64-bit modular addition, subtraction, and Barrett multiplication
├── ntt.h/cpp             # Forward (Cooley-Tukey) & Inverse (Gentleman-Sande) NTT engines
├── poly_arith.h/cpp      # Vectorized polynomial element-wise operations in NTT domain
├── automorphism.h/cpp    # Galois slot permutation engine
├── rescale.h/cpp         # CKKS modulus-switching and noise-reduction engine
├── key_switch.h/cpp      # Hybrid key-switching pipeline (FastBasesConv + ApproxModDown)
└── fhe_accel_top.h/cpp   # Top-level AXI master kernel with multi-bank URAM storage
```

---

## 3. Verification & Synthesis Workflow

### Phase 1: Test Vector Generation (`integration_tools/`)
* Generate deterministic test vectors using OpenFHE C++ APIs matching hardware parameters ($N=16384$, 11 $Q$ primes, 4 $P$ primes).
* Serialize evaluation keys, Galois automorphism maps, twiddles, and Barrett constants to raw binary format.

### Phase 2: C Simulation (`csim_design`)
* Verify modular arithmetic, NTT/INTT, polynomial arithmetic, automorphism, rescale, and key-switching against OpenFHE golden vectors in C++ simulation.
* Achieve 100% bit-exact accuracy across all arithmetic modules.

### Phase 3: High-Level Synthesis (`csynth_design`)
* Synthesize C++ core to VHDL and Verilog RTL using Vitis HLS targeting the AMD Virtex UltraScale+ FPGA (`xcvu37p-fsvh2892-2-e`).
* Enforce loop pipelining ($II=1$) and cyclic array partitioning across 8 parallel 64-bit lanes (512-bit wide datapath).

### Phase 4: Full System Simulation (`cpp_host_model/`)
* Execute the complete multi-stage interactive IVF-PQ query pipeline:
  1. Coarse centroid distance computation (Stage 1).
  2. 32-step distance compaction (Stage 1b).
  3. Interactive client handshake (Stage 2).
  4. Fine candidate ADC distance evaluation (Stage 3).
  5. Top-$K$ nearest-neighbor extraction (Stage 4).
