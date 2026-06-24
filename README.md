# Accelerating Approximate Nearest Neighbor Search On Fully Homomorphic Encrypted Data (ANNS_FHE)

This repository hosts the full implementation of the master's thesis project focused on accelerating Approximate Nearest Neighbor Search (ANNS) over Fully Homomorphic Encrypted (FHE) datasets using custom hardware architectures.

---

## 📂 Repository Structure

The project is structured into modular subdirectories corresponding to the implementation milestones:

*   📁 **`software_golden_model/`**: Pure NumPy reference pipeline and OpenFHE C++ simulations. Serves as the golden model to generate test vectors.
*   📁 **`cpp_host_model/`**: Host-side C++ runtime orchestration, pre-processing libraries, and CPU-offloading scheduling programs.
*   📁 **`hardware_fpga_model/`**: Hardware RTL modules, Vitis High-Level Synthesis (HLS) kernels, testbenches, and Vivado simulation resources.
*   📁 **`integration_tools/`**: Deployment configurations, automation scripts, and integration utilities for end-to-end execution.

---

## 🐍 Milestone 1: Python Plaintext Golden Model

The code in `software_golden_model/` implements a complete coarse-to-fine Inverted File with Product Quantization (IVF-PQ) indexing and search pipeline. It features Asymmetric Distance Computation (ADC) using correct mathematical query residuals to validate search recall against ground-truth benchmarks.

### ⚙️ Installation & Configuration

Navigate into the golden model directory and ensure dependencies are installed:
```powershell
cd software_golden_model
pip install -r requirements.txt
```

Execution settings (dataset paths, cluster lists, subvector subdivisions) are configured inside `config.json`.

```json
{
    "dataset": {
        "path": "../dataset/siftsmall/siftsmall_base.fvecs",
        "query_path": "../dataset/siftsmall/siftsmall_query.fvecs",
        "groundtruth_path": "../dataset/siftsmall/siftsmall_groundtruth.ivecs",
        "dimension": 128,
        "models_output_dir": "../dataset/siftsmall_IVFPQ_models/",
        "encoding_output_dir": "../dataset/siftsmall_IVFPQ_encoded/"
    },
    ...
}
```

### 🚀 Usage

Execute the pipeline stages sequentially from the `software_golden_model/` directory:

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
Executes Asymmetric Distance Computation (ADC) queries over the entire database using SIFT query vectors, maps candidate indices to their coarse clusters, and validates Recall@K accuracy against ground truth:
```powershell
python main.py test_query --top_k 8
```

## 🛡️ Validation & Diagnostics

The search engine prints diagnostic statistics on execution:
- Average Recall@K across all query vectors.
- Standard deviation, minimum, and maximum recall bounds.
- Nearest neighbors comparison logs (True indices vs. predicted indices) for query diagnostic inspections.