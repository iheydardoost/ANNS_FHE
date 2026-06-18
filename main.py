import argparse
import sys
import numpy as np
import os
from config_manager import IVFPQConfigManager
from data_loader import SiftDataLoader
from preprocessor import IVFPQPreprocessor
from encoder import IVFPQEncoder
from searcher import IVFPQSearcher
from serialize_models import save_preprocessing_artifacts

def brute_force_exact_search(X_base: np.ndarray, X_query: np.ndarray, k: int) -> np.ndarray:
    """Computes exact ground-truth nearest neighbors via brute-force Euclidean distance."""
    print("Computing Ground Truth exact neighbors via brute-force...")
    gt_indices = []
    for q in X_query:
        dists = np.sum((X_base - q) ** 2, axis=1)
        gt_indices.append(np.argsort(dists)[:k])
    return np.array(gt_indices)

def create_models():
    # Initialize configuration manager
    config_mgr = IVFPQConfigManager("config.json")
    
    # Load SIFT10K dataset
    loader = SiftDataLoader(
        file_path=config_mgr.dataset.path, 
        expected_dim=config_mgr.dataset.dimension
    )
    X = loader.load_dataset()
    print(f"Loaded dataset matrix dimensions: {X.shape}\n")
    
    # Initialize and run unified preprocessing pipeline
    pipeline = IVFPQPreprocessor(config_mgr)
    ivf_centroids, pq_codebooks = pipeline.run_preprocessing(X)
    
    # Final verification diagnostics
    print("\n--- Preprocessing Output Shapes ---")
    print(f"IVF Centroids Matrix: {ivf_centroids.shape}") # (n_list, dimension) -> (32, 128)
    print(f"PQ Codebooks Tensor:  {pq_codebooks.shape}")  # (M, k_subcentroids, sub_dim) -> (8, 256, 16)

    # Saving to files
    save_preprocessing_artifacts(ivf_centroids, pq_codebooks, config_mgr.dataset.models_output_dir)


def encode_dataset():
    # Initialize configuration manager
    config_mgr = IVFPQConfigManager("config.json")
    
    # Load SIFT10K dataset
    loader = SiftDataLoader(
        file_path=config_mgr.dataset.path, 
        expected_dim=config_mgr.dataset.dimension
    )
    X = loader.load_dataset()
    print(f"Loaded dataset matrix dimensions: {X.shape}\n")
    
    # Initialize encoder using saved parameters from the "models" directory
    encoder = IVFPQEncoder(models_dir=config_mgr.dataset.models_output_dir, m_subvectors=config_mgr.pq.m_subvectors)
    
    # Encode
    ivf_assignments, pq_codes = encoder.encode_dataset(X)
    
    # Save the index deployment data structures
    os.makedirs(config_mgr.dataset.encoding_output_dir, exist_ok=True)
    
    np.save(os.path.join(config_mgr.dataset.encoding_output_dir, "ivf_assignments.npy"), ivf_assignments)
    np.save(os.path.join(config_mgr.dataset.encoding_output_dir, "pq_codes.npy"), pq_codes)
    
    print("\n--- Encoding Population Summary ---")
    print(f"IVF Assignments Array Shape: {ivf_assignments.shape} (Dtype: {ivf_assignments.dtype})")
    print(f"PQ Compressed Codes Shape:  {pq_codes.shape} (Dtype: {pq_codes.dtype})")
    print(f"Total Database Storage: {pq_codes.nbytes / 1024:.2f} KB (Down from raw vectors size!)")


def search_query():
    # Initialize configuration manager
    config_mgr = IVFPQConfigManager("config.json")

    # Load SIFT10K dataset
    loader = SiftDataLoader(
        file_path=config_mgr.dataset.path, 
        expected_dim=config_mgr.dataset.dimension
    )
    X = loader.load_dataset()
    
    # Slice the last 100 vectors to use as test queries; use the rest as the database base
    X_base = X[:-100]
    X_query = X[-100:]
    
    top_k = 8
    n_probe = config_mgr.ivf.n_list
    
    # Compute exact ground truth
    ground_truth = brute_force_exact_search(X_base, X_query, k=top_k)
    
    # Instantiate Searcher
    searcher = IVFPQSearcher(models_dir=config_mgr.dataset.models_output_dir,
                              index_dir=config_mgr.dataset.encoding_output_dir,
                                m_subvectors=config_mgr.pq.m_subvectors)
    
    # Execute queries using IVF-PQ ADC
    print(f"Running IVF-PQ ADC search (n_probe={n_probe}, top_k={top_k}) over test queries...")
    pq_predictions = []
    
    for q in X_query:
        pred_ids, _ = searcher.query(q, n_probe=n_probe, top_k=top_k)
        # Pad with dummy values if candidate pool returned fewer items than top_k
        if len(pred_ids) < top_k:
            pred_ids = np.pad(pred_ids, (0, top_k - len(pred_ids)), constant_values=-1)
        pq_predictions.append(pred_ids)
        
    pq_predictions = np.array(pq_predictions)
    
    # Calculate Recall@K accuracy validation metric
    recall_score = searcher.evaluate_recall(ground_truth, pq_predictions, k=top_k)
    
    print("\n================ SEARCH VALIDATION SUMMARY ================")
    print(f"Configuration Evaluated:  n_list={config_mgr.ivf.n_list}, M={config_mgr.pq.m_subvectors}")
    print(f"Search Execution Knobs:   n_probe={n_probe}, Top-K={top_k}")
    print(f"Validated Recall@{top_k}:      {recall_score * 100:.2f}%")
    print("==========================================================")


def main():
    parser = argparse.ArgumentParser(
        description="Run different functions based on command-line argument"
    )
    
    parser.add_argument(
        "function",
        choices=["create_models",
                  "encode_dataset",
                    "search_query"],
        help="Choose which function to run"
    )
    
    args = parser.parse_args()
    
    # Map arguments to functions
    if args.function == "create_models":
        result = create_models()
    elif args.function == "encode_dataset":
        result = encode_dataset()
    elif args.function == "search_query":
        result = search_query()
    else:
        print("Invalid function choice")
        sys.exit(1)
    
    # print(f"Result: {result}")


if __name__ == "__main__":
    main()