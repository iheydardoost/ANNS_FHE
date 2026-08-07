# Accelerating Approximate Nearest Neighbor Search On Fully Homomorphic Encrypted Data (ANNS_FHE)

This repository hosts the implementation of a master's thesis project focused on accelerating Approximate Nearest Neighbor Search (ANNS) over Fully Homomorphic Encrypted (FHE) datasets. The codebase currently features a comprehensive software-based reference model (Golden Model) that performs Inverted File with Product Quantization (IVF-PQ) and Asymmetric Distance Computation (ADC) over encrypted data using the OpenFHE library.

---

## 📂 Repository Structure

The project is structured into modular subdirectories corresponding to the planned implementation milestones. Currently, the primary focus is on the software reference model.

*   📁 **[`software_golden_model/`](./software_golden_model)**: The core implementation. Contains a Python pipeline for dataset preprocessing, model training, and quantization, alongside a high-performance OpenFHE C++ backend.
    *   📁 **[`software_golden_model/fhe_core/`](./software_golden_model/fhe_core)**: Source code for the high-performance OpenFHE C++ backend compiled to the core executable (`fhe_core_bin`). It handles offline preprocessing, key generation, ciphertext index serialization, SIMD query packing, and homomorphic distance evaluation.
    *   📄 **[`software_golden_model/main.py`](./software_golden_model/main.py)** & **[`fhe_wrapper.py`](./software_golden_model/fhe_wrapper.py)**: Python orchestrator and wrapper interfacing Python query workflows with the compiled C++ core binary.
*   📁 **[`cpp_host_model/`](./cpp_host_model)**: *(Planned)* Host-side C++ runtime orchestration, pre-processing libraries, and CPU-offloading scheduling programs.
*   📁 **[`hardware_fpga_model/`](./hardware_fpga_model)**: *(Planned)* Hardware RTL modules, Vitis High-Level Synthesis (HLS) kernels, testbenches, and Vivado simulation resources.
*   📁 **[`integration_tools/`](./integration_tools)**: *(Planned)* Deployment configurations, automation scripts, and integration utilities for end-to-end execution.

---

## 🐍 Software Golden Model Overview

The `software_golden_model` implements a complete coarse-to-fine IVF-PQ indexing and search pipeline. It features Asymmetric Distance Computation (ADC) to mathematically evaluate mathematically exact query residuals.

The underlying OpenFHE C++ core (`fhe_core`) is highly versatile and supports **4 distinct execution modes** configured via [`config.json`](./software_golden_model/config.json):

1.  **Plaintext Search Mode (`use_encryption: false`)**
    Executes a fast CPU-based baseline search using IVF-PQ in plaintext. Uses thread-local pre-allocated memory buffers for optimal benchmarking.
2.  **Encrypted Interactive Search (`use_encryption: true, interactive: true`)**
    Executes an encrypted IVF search where the client interacts with the server. The server calculates coarse centroid distances, sends them to the client for decryption/selection, and the client sends back the selection mask to continue the ADC step on the server.
3.  **Encrypted Exact Search (`use_encryption: true, interactive: false, add_penalty: false`)**
    Bypasses the IVF cluster selection and computes full distances across the entire dataset. This behaves exactly like exact nearest neighbor search over FHE, yielding maximum recall at the cost of higher latency.
4.  **Encrypted IVF with Penalty (`use_encryption: true, interactive: false, add_penalty: true`)**
    Uses IVF to find the top $n_{\text{probe}}$ nearest coarse centroids securely on the server. To avoid revealing which clusters were probed, it adds a massive penalty to the distances of data points that do not belong to these probed centroids, effectively filtering them out homomorphically.

---

## ⚙️ Compilation & Build Instructions (C++ Core)

To compile the C++ core execution binary (`fhe_core_bin`):

### Prerequisites
*   **Compiler Toolchain**: Clang or MinGW-w64 toolchain (configured and accessible on system `PATH`).
*   **Build System**: CMake (version 3.12+).
*   **OpenFHE Library**: Installed, built, and accessible by CMake (e.g., at `C:\OpenFHE`).

### Step-by-Step Build Commands
Navigate to the core directory and build using CMake:
```powershell
cd software_golden_model/fhe_core
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```
This generates the C++ core binary (`fhe_core_bin.exe` on Windows / `fhe_core_bin` on Linux).

---

## 📦 Installation & Configuration (Python Pipeline)

1. Navigate into the golden model directory and ensure dependencies are installed:
   ```powershell
   cd software_golden_model
   pip install -r requirements.txt
   ```
   *(Dependencies include `numpy`, `pandas`, `scikit-learn` (for K-Means), `matplotlib`, etc.)*

2. Execution settings (dataset paths, IVF/PQ hyper-parameters, and encryption configurations) are managed inside [`config.json`](./software_golden_model/config.json).

### Key Parameters in `config.json`:
```json
{
    "encryption": {
        "enabled": true,
        "use_encryption": false,
        "interactive": true,
        "add_penalty": false,
        "poly_modulus_degree": 65536,
        "scale_bits": 45,
        "n_probe": 1,
        "eval_sign_deg": 247,
        "eval_indicator_deg": 59,
        "serialization_dir": "../dataset/siftsmall_fhe_keys/",
        "ram_limit_gb": 32.0
    }
}
```
*   `use_encryption`, `interactive`, `add_penalty`: Toggles the 4 execution modes outlined above.
*   `n_probe`: Controls the number of coarse clusters probed during IVF search.
*   `poly_modulus_degree` & `scale_bits`: CKKS encryption parameters.
*   `eval_sign_deg` & `eval_indicator_deg`: Polynomial degrees for homomorphic sign/comparison evaluation.
*   `serialization_dir`: Target directory for storing/loading precomputed OpenFHE cryptocontexts, keys, and encrypted codebooks.

---

## 🚀 Basic Usage Workflow

Execute the pipeline stages sequentially from the `software_golden_model/` directory using [`main.py`](./software_golden_model/main.py):

### 1. Train Codebooks & Coarse IVF Centroids
Clusters the base dataset into coarse partitions via K-Means and generates orthogonal subspace PQ codebooks on residual vectors:
```powershell
python main.py create_models
```

### 2. Encode and Compress Dataset
Assigns base vectors to their closest coarse IVF centroids, calculates residual vectors, and quantizes the residuals into discrete PQ indices:
```powershell
python main.py encode_dataset
```

### 3. Offline FHE Preprocessing (Keygen & Index Encryption)
Generates the OpenFHE `CryptoContext`, public/secret/evaluation keys, then encrypts the coarse centroids and PQ codebooks and serializes them:
```powershell
python main.py preprocess
```

### 4. Run Query Evaluation
Executes Asymmetric Distance Computation (ADC) queries over the database according to the mode set in `config.json`. Mapped candidates are compared against the ground truth to validate Recall@K:
```powershell
python main.py test_query --top_k 8 --batch -j 4
```

#### ⚙️ Query CLI Options
*   `--batch`: Process all query vectors.
*   `-j`, `--jobs <X>`: Specifies the parallel threads or worker count (default: `1`).
*   `-n`, `--num_queries <Y>`: Limits the number of query tests to execute (default: `10`).
*   `--top_k <K>`: Specifies the top-K nearest neighbors to retrieve (default: `8`).

---

## 📊 Benchmarking & Analysis Scripts

To run large-scale evaluations, the repository includes automation and plotting scripts under the `software_golden_model/` directory:

*   **`run_experiments.py`**: Automates testing the pipeline across a wide variety of parameter combinations ($n_{\text{list}}$, $M$, $K_{\text{pq}}$) to find optimal configurations.
*   **`analyze_results.py`**: Processes the CSV data generated by the parameter sweep and generates visualization plots (Recall vs. Top-K).
*   **`run_fhe_benchmarks.py`**: Runs side-by-side performance comparisons of Plaintext search versus Fully Homomorphic Encrypted (FHE) search, validating mathematical correctness and highlighting computational overhead.