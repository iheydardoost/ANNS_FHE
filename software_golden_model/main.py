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

def get_config_manager():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "config.json")
    return IVFPQConfigManager(config_path)

def create_models():
    # Initialize configuration manager using script-relative config.json
    config_mgr = get_config_manager()
    
    # Load SIFT dataset
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
    print(f"IVF Centroids Matrix: {ivf_centroids.shape}") # (n_list, dimension)
    print(f"PQ Codebooks Tensor:  {pq_codebooks.shape}")  # (M, k_subcentroids, sub_dim)

    # Saving to files
    save_preprocessing_artifacts(ivf_centroids, pq_codebooks, config_mgr.dataset.models_output_dir)


def encode_dataset():
    # Initialize configuration manager using script-relative config.json
    config_mgr = get_config_manager()
    
    # Load SIFT dataset
    loader = SiftDataLoader(
        file_path=config_mgr.dataset.path, 
        expected_dim=config_mgr.dataset.dimension
    )
    X = loader.load_dataset()
    print(f"Loaded dataset matrix dimensions: {X.shape}\n")
    
    # Initialize encoder using saved parameters from the "models" directory
    encoder = IVFPQEncoder(models_dir=config_mgr.dataset.models_output_dir,
                            m_subvectors=config_mgr.pq.m_subvectors,
                            k_subcentroids=config_mgr.pq.k_subcentroids)
    
    # Encode
    ivf_assignments, pq_codes = encoder.encode_dataset(X)
    
    # Save the index deployment data structures
    os.makedirs(config_mgr.dataset.encoding_output_dir, exist_ok=True)
    
    np.save(os.path.join(config_mgr.dataset.encoding_output_dir, "ivf_assignments.npy"), ivf_assignments)
    np.save(os.path.join(config_mgr.dataset.encoding_output_dir, "pq_codes.npy"), pq_codes)
    
    # Save raw binaries for C++ core
    ivf_assignments.astype(np.int32).tofile(os.path.join(config_mgr.dataset.encoding_output_dir, "ivf_assignments.bin"))
    if config_mgr.pq.k_subcentroids <= 256:
        pq_codes.astype(np.uint8).tofile(os.path.join(config_mgr.dataset.encoding_output_dir, "pq_codes.bin"))
    else:
        pq_codes.astype(np.uint16).tofile(os.path.join(config_mgr.dataset.encoding_output_dir, "pq_codes.bin"))
    
    print("\n--- Encoding Population Summary ---")
    print(f"IVF Assignments Array Shape: {ivf_assignments.shape} (Dtype: {ivf_assignments.dtype})")
    print(f"PQ Compressed Codes Shape:  {pq_codes.shape} (Dtype: {pq_codes.dtype})")
    print(f"Total Database Storage: {pq_codes.nbytes / 1024:.2f} KB (Down from raw vectors size!)")


def test_query(top_k: int, batch_mode: bool = False, jobs: int = 1, num_queries: int = 10):
    # Initialize configuration manager using script-relative config.json
    config_mgr = get_config_manager()

    # Load SIFT query dataset and ground truth using paths from config
    query_loader = SiftDataLoader(
        file_path=config_mgr.dataset.query_path, 
        expected_dim=config_mgr.dataset.dimension
    )
    X_query = query_loader.load_dataset()
    
    # Load ground truth
    ground_truth = query_loader.load_groundtruth(config_mgr.dataset.groundtruth_path)

    # Slice dataset to query limit if requested
    if num_queries > 0 and len(X_query) > num_queries:
        X_query = X_query[:num_queries]
        ground_truth = ground_truth[:num_queries]
    
    encryption_config = config_mgr.raw_config.get("encryption", {})
    encryption_enabled = encryption_config.get("enabled", False)
    
    if encryption_enabled:
        n_probe = encryption_config.get("n_probe", 4)
        from fhe_wrapper import FHESearcherWrapper
        script_dir = os.path.dirname(os.path.abspath(__file__))
        config_path = os.path.join(script_dir, "config.json")
        wrapper = FHESearcherWrapper(config_path)
        
        use_encryption = encryption_config.get("use_encryption", True)
        
        if batch_mode:
            if use_encryption:
                print(f"\nRunning C++ FHE ANNS search in PARALLEL SUBPROCESS mode (n_probe={n_probe}, top_k={top_k}) over {len(X_query)} test queries with jobs={jobs}...")
                print(f"   (Bypassing OpenFHE multi-threading memory contention and allocator locks by spawning independent single-query subprocesses)")
                
                from concurrent.futures import ThreadPoolExecutor, as_completed
                import time
                
                pq_predictions_list = [None] * len(X_query)
                latencies = [0.0] * len(X_query)
                
                start_time = time.perf_counter()
                
                def run_one_query(q_idx):
                    pred_ids, latency_report = wrapper.search(query_idx=q_idx, top_k=top_k)
                    try:
                        lat_val = float(latency_report.split()[0])
                    except Exception:
                        lat_val = 0.0
                    return q_idx, pred_ids, lat_val
                
                with ThreadPoolExecutor(max_workers=jobs) as executor:
                    futures = [executor.submit(run_one_query, i) for i in range(len(X_query))]
                    for future in as_completed(futures):
                        q_idx, pred_ids, lat_val = future.result()
                        if len(pred_ids) < top_k:
                            pred_ids = np.pad(pred_ids, (0, top_k - len(pred_ids)), constant_values=-1)
                        pq_predictions_list[q_idx] = pred_ids[:top_k]
                        latencies[q_idx] = lat_val
                        
                end_time = time.perf_counter()
                total_latency = (end_time - start_time) * 1000.0
                avg_active = np.mean(latencies) if latencies else 0.0
                throughput = total_latency / len(X_query)
                
                pq_predictions = np.array(pq_predictions_list)
                
                print(f"✓ Total Execution Time (Wall-clock, parallel subprocesses): {total_latency:.2f} ms")
                print(f"✓ Average Query Active Computation Time (Isolated):          {avg_active:.2f} ms")
                print(f"✓ Effective Query Latency (Throughput-based):                 {throughput:.2f} ms")
            else:
                print(f"\nRunning C++ FHE ANNS search in BATCH mode (n_probe={n_probe}, top_k={top_k}) over {len(X_query)} test queries with jobs={jobs}...")
                ordered_pred_ids, total_latency, avg_active, throughput = wrapper.search_batch(top_k=top_k, jobs=jobs, limit=num_queries)
                
                pq_predictions = []
                for pred_ids in ordered_pred_ids:
                    if len(pred_ids) < top_k:
                        pred_ids = np.pad(pred_ids, (0, top_k - len(pred_ids)), constant_values=-1)
                    pq_predictions.append(pred_ids[:top_k])
                pq_predictions = np.array(pq_predictions)
                
                print(f"✓ Total C++ Core Batch Execution Time (Wall-clock): {total_latency:.2f} ms")
                print(f"✓ Average Query Active Computation Time (Isolated):   {avg_active:.2f} ms")
                print(f"✓ Effective Query Latency (Throughput-based):         {throughput:.2f} ms")
        else:
            print(f"\nRunning C++ FHE ANNS search in SINGLE-QUERY mode (n_probe={n_probe}, top_k={top_k}) over {len(X_query)} test queries...")
            pq_predictions = []
            latencies = []
            
            for i in range(len(X_query)):
                pred_ids, latency_report = wrapper.search(query_idx=i, top_k=top_k)
                if len(pred_ids) < top_k:
                    pred_ids = np.pad(pred_ids, (0, top_k - len(pred_ids)), constant_values=-1)
                pq_predictions.append(pred_ids[:top_k])
                
                try:
                    lat_val = float(latency_report.split()[0])
                    latencies.append(lat_val)
                except Exception:
                    pass
                    
            pq_predictions = np.array(pq_predictions)
            avg_latency = np.mean(latencies) if latencies else 0.0
            print(f"✓ Average C++ Core Query Latency: {avg_latency:.2f} ms")
    else:
        n_probe = config_mgr.ivf.n_list
        # Instantiate Searcher
        searcher = IVFPQSearcher(models_dir=config_mgr.dataset.models_output_dir,
                                 index_dir=config_mgr.dataset.encoding_output_dir,
                                 m_subvectors=config_mgr.pq.m_subvectors,
                                 k_subcentroids=config_mgr.pq.k_subcentroids)
        
        # Execute queries using IVF-PQ ADC
        print(f"\nRunning IVF-PQ ADC search (n_probe={n_probe}, top_k={top_k}) over {len(X_query)} test queries...")
        pq_predictions = []
        
        for q in X_query:
            pred_ids, _ = searcher.query(q, n_probe=n_probe, top_k=top_k)
            if len(pred_ids) < top_k:
                pred_ids = np.pad(pred_ids, (0, top_k - len(pred_ids)), constant_values=-1)
            pq_predictions.append(pred_ids)
            
        pq_predictions = np.array(pq_predictions)
    
    # Calculate Recall@K accuracy validation metric
    recall_score = IVFPQSearcher.evaluate_recall(ground_truth, pq_predictions, k=top_k)
    
    # Calculate individual recall to report richer statistics
    individual_recalls = []
    for i in range(len(X_query)):
        true_set = set(ground_truth[i, :top_k])
        pred_set = set(pq_predictions[i, :top_k])
        hits = len(true_set.intersection(pred_set))
        individual_recalls.append(hits / top_k)
        
    avg_recall = np.mean(individual_recalls)
    std_recall = np.std(individual_recalls)
    min_recall = np.min(individual_recalls)
    max_recall = np.max(individual_recalls)
    
    print("\n" + "=" * 60)
    print("                    SEARCH VALIDATION SUMMARY                    ")
    print("=" * 60)
    print(f"Dataset Dimension:             {config_mgr.dataset.dimension}")
    print(f"IVF Coarse Centroids (n_list):  {config_mgr.ivf.n_list}")
    print(f"PQ Subspace Count (M):         {config_mgr.pq.m_subvectors}")
    print(f"Search Execution Knobs:        n_probe={n_probe}, Top-K={top_k}")
    print("-" * 60)
    print(f"Validated Recall@{top_k} (Mean):      {avg_recall * 100:.2f}%")
    print(f"Recall Standard Deviation:     {std_recall * 100:.2f}%")
    print(f"Recall Range:                  [{min_recall * 100:.1f}% - {max_recall * 100:.1f}%]")
    print("=" * 60)
    
    # Print sample query diagnostics
    print("\nSample Query Diagnostics:")
    print("------------------------------------------------------------")
    for i in range(min(20, len(X_query))):
        true_neighbors = list(ground_truth[i, :top_k])
        pred_neighbors = list(pq_predictions[i, :top_k])
        matches = set(true_neighbors).intersection(set(pred_neighbors))
        print(f"Query {i + 1:02d}:")
        print(f"   True Neighbors: {true_neighbors}")
        print(f"   Pred Neighbors: {pred_neighbors}")
        print(f"   Recall@{top_k}:      {len(matches) / top_k * 100:.1f}% (Hits: {len(matches)}/{top_k})")
    print("------------------------------------------------------------")


def main():
    parser = argparse.ArgumentParser(
        description="Run different functions based on command-line arguments"
    )
    
    parser.add_argument(
        "function",
        choices=["create_models",
                  "encode_dataset",
                  "test_query"],
        help="Choose which function to run"
    )
    
    parser.add_argument(
        "--top_k",
        type=int,
        default=8,
        help="specify top-k value for query search (default: 8)"
    )
    
    parser.add_argument(
        "--batch",
        action="store_true",
        help="run queries in batch mode to measure true C++ latency without OS process/IO overhead"
    )
    
    parser.add_argument(
        "-j", "--jobs",
        type=int,
        default=1,
        help="number of parallel threads to use in C++ batch mode (default: 1)"
    )
    
    parser.add_argument(
        "-n", "--num_queries",
        type=int,
        default=10,
        help="maximum number of queries to test (default: 10). Set to -1 to run all queries."
    )
    
    args = parser.parse_args()
    
    if args.function == "create_models":
        create_models()
    elif args.function == "encode_dataset":
        encode_dataset()
    elif args.function == "test_query":
        test_query(args.top_k, args.batch, args.jobs, args.num_queries)
    else:
        print("Invalid function choice")
        sys.exit(1)


if __name__ == "__main__":
    main()