# Accelerating Approximate Nearest Neighbor Search On Fully Homomorphic Encrypted Data (ANNS_FHE)

This repository hosts the full implementation of a master's thesis project focused on accelerating **Approximate Nearest Neighbor Search (ANNS)** over **Fully Homomorphic Encrypted (FHE)** datasets using custom hardware architectures.

The project combines algorithmic innovations—such as **Inverted File with Product Quantization (IVF-PQ)**, **Asymmetric Distance Computation (ADC)**, homomorphic distance compaction, and an interactive query protocol—with a custom synthesizable FPGA accelerator implemented in Vitis HLS for the **AMD Virtex UltraScale+ FPGA (`xcvu37p`)**, a host orchestration runtime, and a comprehensive software golden reference pipeline.

---

## 📑 Table of Contents

- [Repository Architecture & Directory Structure](#-repository-architecture--directory-structure)
- [System Architecture & Search Protocols](#-system-architecture--search-protocols)
- [Software Golden Model (`software_golden_model/`)](#-software-golden-model-software_golden_model)
- [Hardware Accelerator Core (`hardware_fpga_model/`)](#-hardware-accelerator-core-hardware_fpga_model)
- [C++ Host Orchestrator & Simulator (`cpp_host_model/`)](#-c-host-orchestrator--simulator-cpp_host_model)
- [Integration & Test Vector Tools (`integration_tools/`)](#-integration--test-vector-tools-integration_tools)
- [Documentation Index (`docs/`)](#-documentation-index-docs)
- [Prerequisites & Build Instructions](#-prerequisites--build-instructions)
- [Usage Workflows & Execution Examples](#-usage-workflows--execution-examples)
- [Hardware Synthesis & Performance Results](#-hardware-synthesis--performance-results)

---

## 📂 Repository Architecture & Directory Structure

The repository is organized into modular components reflecting the complete hardware/software co-design stack:

```text
ANNS_FHE/
├── docs/                      # Architectural specs, hardware interfaces, and protocols
├── software_golden_model/     # Python IVF-PQ pipeline & OpenFHE C++ golden reference core
│   ├── fhe_core/              # High-performance OpenFHE C++ core (4 query modes)
│   ├── main.py                # Python pipeline orchestrator
│   └── run_fhe_benchmarks.py  # Automated latency & recall benchmarking
├── integration_tools/         # Test vector generators & trace validators (OpenFHE)
│   ├── generate_interactive_test_vectors.cpp
│   └── verify_interactive_trace.py
├── hardware_fpga_model/       # Synthesizable Vitis HLS FPGA accelerator core
│   └── hls/
│       ├── src/               # HLS C++ primitives, top kernel, and unit testbenches
│       └── run_hls.tcl        # Vitis HLS synthesis script
└── cpp_host_model/            # Host runtime orchestrator & end-to-end hardware simulation
    └── src/
        ├── sim_memory_bus.cpp # 512-bit AXI global memory emulator (poly_gmem / key_gmem)
        ├── fhe_host_driver.cpp# Composite FHE operator driver
        ├── interactive_query_manager.cpp # Interactive query protocol state machine
        └── main_sim.cpp       # 6-checkpoint end-to-end verification test runner
```

### Component Summary

*   📁 **[`software_golden_model/`](./software_golden_model)**: Reference Python indexing/quantization pipeline and multi-threaded OpenFHE C++ backend (`fhe_core_bin`). Used for dataset preprocessing, codebook training, and software-level algorithm validation.
*   📁 **[`hardware_fpga_model/`](./hardware_fpga_model)**: Synthesizable Vitis HLS implementation of the FHE accelerator targeting the AMD Virtex UltraScale+ FPGA (`xcvu37p-fsvh2892-2-e`). Implements 64-bit modular arithmetic, NTT/INTT, Galois automorphism, rescaling, hybrid key-switching, and dual 512-bit AXI memory interfaces.
*   📁 **[`cpp_host_model/`](./cpp_host_model)**: Host-side C++ runtime orchestration and hardware simulation environment. Models virtual 512-bit DRAM buses, schedules composite operations on the accelerator, and executes end-to-end interactive search verification.
*   📁 **[`integration_tools/`](./integration_tools)**: OpenFHE-based tools to generate deterministic cryptographic keys, RNS constants, twiddle tables, and intermediate simulation traces matching hardware parameters.
*   📁 **[`docs/`](./docs)**: Detailed engineering specifications, hardware memory layouts, mathematical formulations, verification plans, and implementation logs.

---

## 🏛️ System Architecture & Search Protocols

The codebase supports privacy-preserving vector search over CKKS homomorphic ciphertexts using a coarse-to-fine IVF-PQ pipeline:

```text
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

### The 4 Query Execution Modes

The software golden model (`software_golden_model/fhe_core`) supports **4 distinct search modes** via [`config.json`](./software_golden_model/config.json):

1.  **Plaintext IVF-PQ Mode (`use_encryption: false`)**:
    Fast CPU-based baseline search using IVF-PQ in plaintext with zero-allocation thread scratch buffers (`PlaintextScratch`).
2.  **Encrypted Interactive Mode (`use_encryption: true, interactive: true`)**:
    Privacy-preserving two-phase search where the client and server collaborate. Server evaluates coarse cluster distances, client decrypts compacted distances and chooses top-$n_{\text{probe}}$ clusters, and server evaluates fine candidate ADC distances. **(Target mode for the FPGA hardware accelerator)**.
3.  **Encrypted Exact Search Mode (`use_encryption: true, interactive: false, add_penalty: false`)**:
    Bypasses IVF cluster pruning and evaluates homomorphic Euclidean distances across the entire dataset, behaving like exact encrypted nearest neighbor search.
4.  **Encrypted IVF with Penalty Mode (`use_encryption: true, interactive: false, add_penalty: true`)**:
    Non-interactive encrypted IVF search. The server homomorphically identifies the closest $n_{\text{probe}}$ centroids and applies a large additive penalty to all database vectors outside the probed clusters.

---

## 🐍 Software Golden Model (`software_golden_model/`)

The software golden model implements the end-to-end algorithmic pipeline:

*   **Training & Encoding**: K-Means clustering for coarse centroids ($n_{\text{list}}$) and subspace Product Quantization ($M$ subvectors, $K_{\text{pq}}$ subcentroids). Supports automatic dynamic `uint8` / `uint16` codebook indexing ($K_{\text{pq}} > 256$).
*   **OpenFHE C++ Core (`fhe_core/`)**:
    *   `FHEContextManager`: Key generation, serialization, SIMD query packing (`encrypt_query_packed`, `encrypt_query_dimpack`).
    *   `FHESearcher`: Multi-stage homomorphic distance computation, SIMD aggregation, and masking.
    *   `PlaintextSearcher`: Reference unencrypted IVF-PQ engine.
    *   `SignApproximator`: Composite polynomial approximations for homomorphic sign evaluation.
    *   `ThreadPool`: Multi-threaded parallel query execution with lock-free atomic task scheduling.

---

## ⚡ Hardware Accelerator Core (`hardware_fpga_model/`)

The hardware accelerator is implemented in synthesizable C++ for AMD Vitis HLS (`hardware_fpga_model/hls/src/`):

### Cryptographic Parameter Set (Hardware Prototype)
*   **Ring Dimension ($N$)**: $16,384$ (SIMD slots: $8,192$, batch size $B=64$)
*   **RNS Moduli**: $L=11$ $Q$-primes (Level 0: $1 \times 55$-bit prime + $10 \times 50$-bit scale primes)
*   **Special Moduli ($P$)**: $4$ $P$-primes ($4 \times 50$-bit for hybrid key-switching)
*   **Target Device**: AMD Virtex UltraScale+ `xcvu37p-fsvh2892-2-e` @ **200 MHz** ($5.0\text{ ns}$)

### Hardware Arithmetic Engines & Primitives

| Module | Source Files | Description |
| :--- | :--- | :--- |
| **Modular Arithmetic** | `mod_arith.cpp`, `mod_arith.h` | 64-bit modular addition, subtraction, and multiplication with 128-bit Barrett reduction. |
| **NTT / INTT** | `ntt.cpp`, `ntt.h` | Forward Cooley-Tukey DIF NTT and Inverse Gentleman-Sande DIT INTT over 14 stages. |
| **Polynomial Arithmetic** | `poly_arith.cpp`, `poly_arith.h` | Vectorized coefficient-wise addition, subtraction, and multiplication in the NTT domain. |
| **Galois Automorphism** | `automorphism.cpp`, `automorphism.h` | Permutation engine mapping polynomial coefficients for vector slot rotation. |
| **Rescaling** | `rescale.cpp`, `rescale.h` | Modulus-switching division by $q_\ell$ and limb dropping ($L \to L-1$). |
| **Hybrid Key-Switching** | `key_switch.cpp`, `key_switch.h` | Digit decomposition, INTT, FastBasesConv ($Q \to Q \cup P$), evaluation key streaming, and ApproxModDown. |
| **Top Accelerator Kernel** | `fhe_accel_top.cpp`, `fhe_accel_top.h` | Top-level AXI master kernel integrating dual 512-bit burst ports (`poly_gmem`, `key_gmem`) and 6 UltraRAM working banks. |

### Unit Testbench Suite
All hardware primitives are paired with standalone C-simulation testbenches verified **100% bit-exact** against OpenFHE:
*   `tb_mod_arith`, `tb_ntt`, `tb_intt`, `tb_poly_arith`, `tb_automorphism`, `tb_rescale`, `tb_keyswitch`, `tb_fhe_accel`.

---

## 🖥️ C++ Host Orchestrator & Simulator (`cpp_host_model/`)

The host model (`cpp_host_model/`) simulates the complete system and coordinates query execution on the hardware accelerator:

*   **Virtual Memory Bus (`sim_memory_bus.h`, `sim_memory_bus.cpp`)**: Emulates 64 MB `poly_gmem` (AXI `gmem0`) and 512 MB `key_gmem` (AXI `gmem1`) with 512-bit word-aligned burst access.
*   **FHE Host Driver (`fhe_host_driver.h`, `fhe_host_driver.cpp`)**: Translates high-level FHE operations into sequences of `fhe_accel_top()` invocations:
    *   `eval_add`, `eval_sub_plain`, `eval_mult_relin_rescale`, `eval_mult_plain_rescale`, `eval_rotate`.
*   **Interactive Query Manager (`interactive_query_manager.h`, `interactive_query_manager.cpp`)**:
    *   **Phase 1**: Coarse centroid distance evaluation + 7-step tree-sum reduction across 128 dimensions.
    *   **Phase 1b**: 32-step distance compaction into contiguous slots $0 \dots 31$.
    *   **Phase 2**: Interactive client handshake (decryption & cluster selection).
    *   **Phase 3**: Fine candidate batch ADC distance computation ($B=64$) and position-shifted distance accumulation.
    *   **Phase 4**: Client-side decryption and Top-8 nearest neighbor extraction.
*   **Simulation Runner (`main_sim.cpp`)**: Validates the end-to-end interactive search pipeline against **6 verification checkpoints**.

### Verification Checkpoints Matrix

| Checkpoint | Verification Step | Status |
| :--- | :--- | :---: |
| **CP1** | Hardware Context & Key Material Initialization (OP 99) | **PASS** |
| **CP2** | Coarse Centroid Distance Subtraction (`ct_diff.c0`, `ct_diff.c1`, 180,224 words) | **100% Bit-Exact PASS** |
| **CP3** | 32-Step Distance Compaction (`ct_compact.c0`, `ct_compact.c1`, 147,456 words) | **100% Bit-Exact PASS** |
| **CP4** | Client Cluster Selection Handshake | **PASS** |
| **CP5** | Fine Candidate Batch ADC Distance (`ct_all_dists.c0`, `ct_all_dists.c1`, 147,456 words) | **100% Bit-Exact PASS** |
| **CP6** | Client Top-8 Extraction & Ground Truth Matching | **100% Recall@8 (8/8 hits)** |

---

## 🔧 Integration & Test Vector Tools (`integration_tools/`)

The `integration_tools/` directory contains tools built with OpenFHE to generate golden test vectors matching hardware specifications:

*   **`generate_interactive_test_vectors.cpp`**: Generates cryptographic key material (relinearization keys, 27 rotation keys, 27 Galois maps, RNS/Barrett constants, twiddle tables, FastBasesConv/ApproxModDown matrices) and step-by-step intermediate ciphertext vectors.
*   **`test_vector_generator.cpp`**: Generates focused unit test vectors for individual arithmetic primitives (NTT, Automorphism, KeySwitch, Rescale).
*   **`verify_interactive_trace.py`**: Python verification script checking polynomial shape, byte alignment, and non-zero polynomial integrity across all serialized files.

---

## 📚 Documentation Index (`docs/`)

Comprehensive technical documentation is maintained under [`docs/`](./docs):

*   📄 **[`architecture.md`](./docs/architecture.md)**: Hardware architectural specification, word representation, RNS layouts, and module interfaces.
*   📄 **[`interactive_search_protocol.md`](./docs/interactive_search_protocol.md)**: Mathematical protocol formulation for encrypted interactive IVF-PQ search.
*   📄 **[`host_orchestrator_design.md`](./docs/host_orchestrator_design.md)**: Design of the host driver, memory allocator, and query manager.
*   📄 **[`hardware_spec.md`](./docs/hardware_spec.md)**: Detailed hardware parameters, clock targets, and UltraRAM banking scheme.
*   📄 **[`hardware_memory_and_interfaces.md`](./docs/hardware_memory_and_interfaces.md)**: AXI4 memory mapping, burst sizing, and register map specifications.
*   📄 **[`verification_plan.md`](./docs/verification_plan.md)**: Step-by-step verification methodology and checkpoint criteria.
*   📄 **[`master_implementation_roadmap.md`](./docs/master_implementation_roadmap.md)**: Milestone tracking and architecture map.
*   📄 **[`implementation_log.md`](./docs/implementation_log.md)**: Verification results and post-synthesis FPGA resource metrics.

---

## ⚙️ Prerequisites & Build Instructions

### Prerequisites
*   **C++ Toolchain**: GCC 11+, Clang 14+, or MinGW-w64 (C++17 standard required).
*   **Build System**: CMake (v3.12+).
*   **OpenFHE Library**: OpenFHE v1.12+ installed and configured on `CMAKE_PREFIX_PATH` or at `C:/OpenFHE`.
*   **FPGA Toolchain (Optional for synthesis)**: AMD Vitis HLS / Vivado 2024.x or 2025.x (or Vitis headers for standalone simulation).
*   **Python Environment**: Python 3.10+ with `numpy`, `scipy`, `pandas`, `scikit-learn`, and `matplotlib`.

---

### 1. Build the Software Golden Model Core
```powershell
cd software_golden_model/fhe_core
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
cd ../../..
```

### 2. Build the Integration Test Vector Generators
```powershell
cd integration_tools
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
cd ../..
```

### 3. Build & Run the C++ Host Simulator
```powershell
cd cpp_host_model
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
./fhe_interactive_sim ../../integration_tools/test_vectors
cd ../..
```

### 4. Synthesize the FPGA Hardware Accelerator (Vitis HLS)
```powershell
cd hardware_fpga_model/hls
vitis-run --mode hls --tcl run_hls.tcl
cd ../..
```

---

## 🚀 Usage Workflows & Execution Examples

### 1. Python Golden Model End-to-End Workflow

Navigate to `software_golden_model/`:
```powershell
cd software_golden_model
pip install -r requirements.txt
```

#### Step 1: Train Coarse Centroids & PQ Codebooks
```powershell
python main.py create_models
```

#### Step 2: Quantize & Encode Dataset
```powershell
python main.py encode_dataset
```

#### Step 3: Offline Preprocessing (Keygen & Codebook Encryption)
```powershell
python main.py preprocess
```

#### Step 4: Run Query Search
```powershell
python main.py test_query --top_k 8 --batch -j 4
```

---

### 2. Generate Deterministic Test Vectors & Run Hardware Simulation

```powershell
# 1. Generate cryptographic keys, tables, and traces
./integration_tools/build/generate_interactive_test_vectors_bin integration_tools/test_vectors

# 2. Verify binary trace integrity
python integration_tools/verify_interactive_trace.py integration_tools/test_vectors

# 3. Execute full 6-checkpoint end-to-end hardware simulation
./cpp_host_model/build/fhe_interactive_sim integration_tools/test_vectors
```

---

### 3. Automated Benchmarking & Evaluation

Under `software_golden_model/`:
*   **`python run_experiments.py`**: Executes hyperparameter sweeps across $n_{\text{list}}$, $M$, and $K_{\text{pq}}$.
*   **`python analyze_results.py`**: Analyzes sweep logs and plots compression ratio vs. Recall@K.
*   **`python run_fhe_benchmarks.py`**: Runs side-by-side performance comparisons of Plaintext vs. FHE search modes.

---

## 📊 Hardware Synthesis & Performance Results

### FPGA Resource Utilization (`AMD Virtex UltraScale+ xcvu37p-fsvh2892-2-e`)
*Synthesized using AMD Vitis HLS 2025.2 @ 200 MHz (Target clock: 5.0 ns):*

| Resource Type | Used | Available | Utilization (%) |
| :--- | :---: | :---: | :---: |
| **LUTs** | 171,181 | 1,303,680 | **13.13%** |
| **Flip-Flops (FF)** | 109,393 | 2,607,360 | **4.20%** |
| **Block RAM (BRAM 18K)** | 832 | 4,032 | **20.63%** |
| **UltraRAM (URAM 288K)** | 384 | 960 | **40.00%** |
| **DSP Blocks (DSP48E2)** | 881 | 9,024 | **9.76%** |

### Performance & Energy Comparison

| Metric | CPU Baseline (OpenFHE) | FPGA Accelerator (`xcvu37p`) | Improvement |
| :--- | :---: | :---: | :---: |
| **Query Latency** | 3,544.0 ms | **137.5 ms** | **25.8$\times$ Speedup** |
| **Energy / Query** | 797.4 J | **3.92 J** | **203.4$\times$ Lower Energy** |
| **Recall Accuracy** | 100% Recall@8 | **100% Recall@8** | **Bit-Exact Equivalence** |