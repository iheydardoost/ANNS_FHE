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
    "project_root": "D:/iman_heydardoost/master/thesis/ANNS_FHE",
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
        "scheme": "CKKS",
        "poly_modulus_degree": 16384,
        "coeff_modulus_bits": [60, 40, 40, 40, 40, 40, 40, 60],
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

*   **Memory Overhead**: Fully Homomorphic Encryption operations under CKKS are extremely memory-intensive. Each active search thread instantiates an independent OpenFHE cryptocontext, public/eval keys, and ciphertext arrays, requiring approximately **4 GB of physical RAM** per active thread.
*   **OOM Scaling Math**: If the number of probed clusters (`n_probe`) is set to 32, querying a single vector in parallel across all 32 candidate clusters would require:
    $$\text{Memory} = 32 \text{ clusters} \times 4 \text{ GB/thread} = 128 \text{ GB of RAM}$$
    This massive footprint easily causes Out-Of-Memory (OOM) kernel crashes on typical development environments.
*   **Safety Guard**: To prevent OOM crashes, a safety guard cap is enforced in the pipeline that limits `n_probe` to a maximum of **4** when running parallel FHE queries.

---

## 🛡️ Validation & Diagnostics

The search engine prints diagnostic statistics on execution:
- Average Recall@K across all query vectors.
- Standard deviation, minimum, and maximum recall bounds.
- Nearest neighbors comparison logs (True indices vs. predicted indices) for query diagnostic inspections.