# ANNS_FHE
## IVF-PQ Vector Search Engine

A pure NumPy implementation of an Inverted File with Product Quantization (IVF-PQ) index for efficient Approximate Nearest Neighbor Search (ANNS), designed and validated using the SIFT10K dataset.

### Features
* **Modular Architecture:** Fully Object-Oriented pipeline built with standalone, configurable modules.
* **Dual-Stage Quantization:** Coarse partitioning via K-Means coupled with Product Quantization (PQ) on residual vectors.
* **Asymmetric Distance Computation (ADC):** Uncompressed query parsing via fast vectorized lookup tables.
* **Config-Driven:** Fully customizable execution parameters driven entirely via JSON.

---

### Installation
Ensure you have Python 3.11+ installed. Install the required dependencies using pip:  
``
pip install -r requirements.txt
``

---

### Configuration
Modify config.json to configure dataset paths, 
cluster list sizes (n_list), 
subvector fragmentation counts (m_subvectors), 
and convergence tolerance metrics.

---

### Usage
The entire lifecycle of the engine is managed via main.py. 
Execute the following stages sequentially:

1. Training Phase
Train the coarse global IVF centroids and generate the PQ subspace codebooks:  
``
python main.py create_models
``

2. Population & Encoding Phase
Slice the space, compute vector residuals against coarse anchors, 
and compress the dataset into discrete indices:  
``
python main.py encode_dataset
``

3. Query & Evaluation Phase
Execute Asymmetric Distance Computation (ADC) queries 
and output standard Recall@K accuracy metrics against an exact brute-force baseline:  
``
python main.py search_query
``