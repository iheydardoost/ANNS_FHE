# Accelerating Approximate Nearest Neighbor Search On Fully Homomorphic Encrypted Data (ANNS_FHE)

This repository hosts the full implementation of the master's thesis project focused on accelerating Approximate Nearest Neighbor Search (ANNS) over Fully Homomorphic Encrypted (FHE) datasets using custom hardware architectures.

---

## 📂 Repository Structure

The project is structured into modular subdirectories corresponding to the implementation milestones:

*   📁 **[`software_golden_model/`]**: Pure NumPy reference pipeline and OpenFHE C++ simulations. Serves as the golden model to generate test vectors.
    *   📁 **[`software_golden_model/fhe_core/`]**: Source code for the high-performance C++ core implementation compiling to the core search binary.
    *   📄 **[`software_golden_model/fhe_wrapper.py`]**: Python wrapper interfacing Python query workflows with the compiled C++ core binary.
*   📁 **[`cpp_host_model/`]**: Host-side C++ runtime orchestration, pre-processing libraries, and CPU-offloading scheduling programs.
*   📁 **[`hardware_fpga_model/`]**: Hardware RTL modules, Vitis High-Level Synthesis (HLS) kernels, testbenches, and Vivado simulation resources.
*   📁 **[`integration_tools/`]**: Deployment configurations, automation scripts, and integration utilities for end-to-end execution.

---

## 🐍 Milestone 1: Python & C++ Plaintext / FHE Golden Model

The code in [`software_golden_model/`] implements a complete coarse-to-fine Inverted File with Product Quantization (IVF-PQ) indexing and search pipeline. It features Asymmetric Distance Computation (ADC) using correct mathematical query residuals to validate search recall against ground-truth benchmarks. The implementation is split into a Python reference pipeline and a high-performance C++ backend that supports both plaintext search and Fully Homomorphic Encrypted (FHE) search simulated with OpenFHE.

### ⚙️ Compilation & Build Instructions (C++ Core)

To compile the C++ core execution binary:

#### Prerequisites
*   **Compiler Toolchain**: Clang or MinGW-w64 toolchain (configured and accessible on system `PATH`).
*   **Build System**: CMake (version 3.12+).
*   **OpenFHE Library**: Installed/built and available at `C:\OpenFHE`.

#### Step-by-Step Build Commands
Navigate to the core directory and build using CMake:
```powershell
cd software_golden_model/fhe_core
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```
This generates the C++ core binary (`fhe_core.exe` on Windows) which is used by the Python pipeline.

### ⚙️ Installation & Configuration

1. Navigate into the golden model directory and ensure dependencies are installed:
   ```powershell
   cd software_golden_model
   pip install -r requirements.txt
   ```

2. Execution settings (dataset paths, cluster lists, subvector subdivisions, and encryption parameters) are configured inside [`config.json`].

```json
{
    "project_root": "/path/to/ANNS_FHE",
    "dataset": {
        "path": "../dataset/siftsmall/siftsmall_base.fvecs",
        "query_path": "../dataset/siftsmall/siftsmall_query.fvecs",
        "groundtruth_path": "../dataset/siftsmall/siftsmall_groundtruth.ivecs",
        "dimension": 128,
        "models_output_dir": "../dataset/siftsmall_IVFPQ_models/",
        "encoding_output_dir": "../dataset/siftsmall_IVFPQ_encoded/"
    },
    "ivf": {
        "n_list": 32,
        "max_iter": 100,
        "seed": 42,
        "tolerance": 1e-5
    },
    "pq": {
        "m_subvectors": 8,
        "k_subcentroids": 256,
        "max_iter": 100,
        "seed": 42,
        "tolerance": 1e-5
    },
    "encryption": {
        "enabled": true,
        "use_encryption": true,
        "poly_modulus_degree": 16384,
        "scale_bits": 40,
        "security_level": "HEStd_128_classic",
        "n_probe": 2,
        "interactive_top_k": true,
        "sign_approx_method": "composition",
        "composition_iterations": 3
    }
}
```

Key parameters inside [`config.json`]:
*   `use_encryption` (under `encryption`): Activates or deactivates Fully Homomorphic Encryption (FHE) mode. If `false`, the pipeline runs in high-performance Plaintext mode.
*   `n_probe` (under `encryption`): Controls the number of coarse clusters probed during IVF search.

---

### 🗃️ Dynamic uint16 PQ Indices Support ($K_{\text{pq}} > 256$)

To support larger and higher-precision Product Quantization codebooks where the number of subcentroids ($K_{\text{pq}}$ / `k_subcentroids`) exceeds $256$ (up to $65,535$), the pipeline automatically upgrades codebook indexing storage from 1-byte `uint8` to 2-byte `uint16`:
*   **Python Encoder & Serialization**: Automatically resolves the required type based on $K_{\text{pq}}$ configuration. If $K_{\text{pq}} \le 256$, `pq_codes.bin` is serialized as `uint8` for storage efficiency; if $K_{\text{pq}} > 256$, it is written as `uint16`.
*   **C++ Core Database Loader**: Inspects `k_subcentroids` inside `load_data()` to dynamically read the binary database index:
    *   For $K_{\text{pq}} \le 256$: Reads file as 8-bit bytes and assigns them to the internal `uint16_t` C++ array.
    *   For $K_{\text{pq}} > 256$: Reads 16-bit elements directly from the binary data stream.
    *   This dynamic widening keeps all critical distance calculation kernels unified under a single type (`uint16_t`) with zero runtime conversion penalty.

---

### 🚀 Usage

Execute the pipeline stages sequentially from the `software_golden_model/` directory using [`main.py`]:

#### 1. Train Codebooks & Coarse IVF Centroids
Clusters the base dataset into coarse partitions via K-Means and generates orthogonal subspace PQ codebooks on residual vectors:
```powershell
python main.py create_models
```

#### 2. Encode and Compress Dataset
Assigns base vectors to closest coarse IVF centroids, calculates residual vectors, and quantizes the residuals into discrete 1-byte PQ indices:
```powershell
python main.py encode_dataset
```

#### 3. Run Query Evaluation
Executes Asymmetric Distance Computation (ADC) queries over the database, maps candidate indices to their coarse clusters, and validates Recall@K accuracy against ground truth:
```powershell
python main.py test_query --top_k 8
```

##### ⚙️ Query CLI Options
The `test_query` command supports the following options:
*   `--batch`: Activates batch processing mode, which processes all query vectors.
*   `-j`, `--jobs <X>`: Specifies the parallel threads or subprocess worker count (default: `1`).
*   `-n`, `--num_queries <Y>`: Limits the number of query tests to execute (default: `10`). Set to `-1` to run all queries.
*   `--top_k <K>`: Specifies the top-K nearest neighbors to retrieve (default: `8`).
---

### 📊 Benchmarking & Analysis Scripts

To run large-scale evaluations and analyze performance characteristics, the repository includes three automation and plotting scripts under the `software_golden_model/` directory:

#### 1. Parameter Sweep Sweep-runner (`run_experiments.py`)
This script automates testing the plaintext pipeline across a wide variety of parameter combinations to find optimal configurations.
* **Mechanism**: It iterates through combinations of IVF clusters ($n_{list}$), PQ subvectors ($M$), and subcentroids ($K_{pq}$). It dynamically updates `config.json`, runs model training, encodes the dataset, and tests queries across multiple `Top-K` values.
* **Execution**:
  ```powershell
  python run_experiments.py
  ```
* **Output**: Writes all recall metrics (mean, std, min, max) to a CSV results file (`results/siftsmall_ivfpq_experiment_results.csv`).

#### 2. Plaintext Experiment Analyzer (`analyze_results.py`)
This script processes the CSV data generated by the parameter sweep.
* **Mechanism**: It loads the results CSV, computes the vector storage footprint and mathematical dataset compression ratio for each combination, prints out the top configurations ranked by recall, and generates comparative plots.
* **Execution**:
  ```powershell
  python analyze_results.py
  ```
* **Output**: Generates and saves three PNG graphs showing Plaintext Recall vs. Top-K as functions of $n_{list}$, $M$, and $K_{pq}$ respectively.

#### 3. Plaintext vs. FHE Benchmarker (`run_fhe_benchmarks.py`)
This script runs side-by-side performance comparisons of the plaintext search versus the Fully Homomorphic Encrypted (FHE) search.
* **Mechanism**:
  1. Disables encryption and runs plaintext queries across the full query set to gather baseline latency and recall stats.
  2. Enables encryption and runs parallel FHE query subprocesses over a sample query batch (e.g., 10 queries, to keep runtime under 5 minutes) for multiple configurations of $n_{probe} \in \{1, 2, 4\}$ and $Top-K \in \{1, 2, 4, 8\}$.
* **Execution**:
  ```powershell
  python run_fhe_benchmarks.py
  ```
* **Output**: 
  * Generates `results/fhe_experiment_summary.json` containing raw latency and recall data.
  * Plots a Recall@K comparison line graph comparing plaintext and FHE (showing exact numerical equivalence).
  * Plots a Query Latency bar chart (seconds per query, on log scale) highlighting the computational FHE overhead on the CPU.

---

### 🔀 Dual Parallelization Behavior

The pipeline employs a dynamic dual-parallelization strategy optimized for each operational mode:

*   **Plaintext Mode (`use_encryption` set to `false`)**:
    *   Uses a native C++20 thread pool internally inside a single running C++ core process.
    *   This provides extremely low overhead, cache-locality optimization, and sub-millisecond query evaluation throughput.
*   **FHE Mode (`use_encryption` set to `true`)**:
    *   Automatically switches to spawning independent parallel C++ subprocesses managed by Python's thread pool (`concurrent.futures.ThreadPoolExecutor`).
    *   This avoids performance issues inherent to OpenFHE's concurrency in a single process, such as:
        *   Memory controller saturation due to massive FHE ciphertext data streams.
        *   L3 cache thrashing and eviction.
        *   Global locks within the OpenFHE context allocator.

---

### ⚠️ FHE Memory Footprint Warning & Safety Guard

*   **Memory Overhead**: Fully Homomorphic Encryption operations under CKKS are extremely memory-intensive. Each active search thread instantiates its own OpenFHE cryptocontext, public/evaluation keys, and ciphertext arrays.
*   **OOM Scaling Math**: The memory footprint of the Query Distance Lookup Table (LUT) scales dynamically with the number of probed clusters ($n_{\text{probe}}$), subspace count ($M$), and subcentroids count ($K_{\text{pq}}$):
    $$\text{Ciphertexts Count} = n_{\text{probe}} \times M \times K_{\text{pq}}$$
    At $N=16384$ ring dimension, each ciphertext consumes $\approx 2 \text{ MB}$. For $M=8$:
    *   If $K_{\text{pq}}=256$: Each coarse probe consumes $\approx 4 \text{ GB}$ of RAM. Probing 32 clusters requires $128 \text{ GB}$ of RAM.
    *   If $K_{\text{pq}}=1024$: Each coarse probe consumes $\approx 16 \text{ GB}$ of RAM. Probing just 2 clusters requires $32 \text{ GB}$ of RAM.
*   **Dynamic Safety Guard**: To prevent the host system from lock-ups or kernel OOM crashes, a dynamic safety check is enforced in `main.cpp` that calculates the estimated memory consumption before running homomorphic queries:
    $$\text{Memory (GB)} = \frac{n_{\text{probe}} \times M \times K_{\text{pq}} \times 2.0}{1024.0}$$
    If the estimated memory footprint exceeds **32.0 GB**, the C++ core terminates immediately with an informative error message.

---

## 🛡️ Validation & Diagnostics

The search engine prints diagnostic statistics on execution:
- Average Recall@K across all query vectors.
- Standard deviation, minimum, and maximum recall bounds.
- Nearest neighbors comparison logs (True indices vs. predicted indices) for query diagnostic inspections.