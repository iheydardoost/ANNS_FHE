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


def get_config_path():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(script_dir, "config.json")


# ---------------------------------------------------------------------------
# create_models: Train IVF centroids + PQ codebooks and save to disk
# ---------------------------------------------------------------------------
def create_models():
    config_mgr = get_config_manager()

    loader = SiftDataLoader(
        file_path=config_mgr.dataset.path,
        expected_dim=config_mgr.dataset.dimension
    )
    X = loader.load_dataset()
    print(f"Loaded dataset matrix dimensions: {X.shape}\n")

    pipeline = IVFPQPreprocessor(config_mgr)
    ivf_centroids, pq_codebooks = pipeline.run_preprocessing(X)

    print("\n--- Preprocessing Output Shapes ---")
    print(f"IVF Centroids Matrix: {ivf_centroids.shape}")   # (n_list, dimension)
    print(f"PQ Codebooks Tensor:  {pq_codebooks.shape}")   # (M, k_subcentroids, sub_dim)

    save_preprocessing_artifacts(ivf_centroids, pq_codebooks, config_mgr.dataset.models_output_dir)


# ---------------------------------------------------------------------------
# encode_dataset: Assign vectors to IVF clusters and PQ-encode them
# ---------------------------------------------------------------------------
def encode_dataset():
    config_mgr = get_config_manager()

    loader = SiftDataLoader(
        file_path=config_mgr.dataset.path,
        expected_dim=config_mgr.dataset.dimension
    )
    X = loader.load_dataset()
    print(f"Loaded dataset matrix dimensions: {X.shape}\n")

    encoder = IVFPQEncoder(
        models_dir=config_mgr.dataset.models_output_dir,
        m_subvectors=config_mgr.pq.m_subvectors,
        k_subcentroids=config_mgr.pq.k_subcentroids
    )

    ivf_assignments, pq_codes = encoder.encode_dataset(X)

    os.makedirs(config_mgr.dataset.encoding_output_dir, exist_ok=True)

    np.save(os.path.join(config_mgr.dataset.encoding_output_dir, "ivf_assignments.npy"), ivf_assignments)
    np.save(os.path.join(config_mgr.dataset.encoding_output_dir, "pq_codes.npy"), pq_codes)

    # Raw binaries for C++ core
    ivf_assignments.astype(np.int32).tofile(
        os.path.join(config_mgr.dataset.encoding_output_dir, "ivf_assignments.bin"))
    if config_mgr.pq.k_subcentroids <= 256:
        pq_codes.astype(np.uint8).tofile(
            os.path.join(config_mgr.dataset.encoding_output_dir, "pq_codes.bin"))
    else:
        pq_codes.astype(np.uint16).tofile(
            os.path.join(config_mgr.dataset.encoding_output_dir, "pq_codes.bin"))

    print("\n--- Encoding Population Summary ---")
    print(f"IVF Assignments Array Shape: {ivf_assignments.shape} (Dtype: {ivf_assignments.dtype})")
    print(f"PQ Compressed Codes Shape:  {pq_codes.shape} (Dtype: {pq_codes.dtype})")
    print(f"Total Database Storage: {pq_codes.nbytes / 1024:.2f} KB")


# ---------------------------------------------------------------------------
# fhe_preprocess: Offline FHE key generation + centroid/codebook encryption
#   Requires the models and encoded data to already exist on disk.
#   Calls C++ binary: fhe_core_bin preprocess <config.json>
# ---------------------------------------------------------------------------
def fhe_preprocess():
    from fhe_wrapper import FHESearcherWrapper

    config_path = get_config_path()
    wrapper = FHESearcherWrapper(config_path)

    print("\n=== Running FHE Offline Preprocessing ===")
    print("This will generate encryption keys and encrypt the IVF/PQ index.")
    print("(One-time operation; results are serialized to disk for reuse.)\n")

    try:
        output = wrapper.preprocess()
        print(output)
        print("\n[OK] FHE preprocessing completed successfully.")
    except RuntimeError as e:
        print(f"\n[ERROR] FHE preprocessing failed:\n{e}")
        sys.exit(1)


# ---------------------------------------------------------------------------
# test_query: Run encrypted or plaintext search and report recall
# ---------------------------------------------------------------------------
def test_query(top_k: int, batch_mode: bool = False, jobs: int = 1, num_queries: int = 10):
    config_mgr = get_config_manager()

    query_loader = SiftDataLoader(
        file_path=config_mgr.dataset.query_path,
        expected_dim=config_mgr.dataset.dimension
    )
    X_query = query_loader.load_dataset()
    ground_truth = query_loader.load_groundtruth(config_mgr.dataset.groundtruth_path)

    # Limit queries if requested
    if num_queries > 0 and len(X_query) > num_queries:
        X_query = X_query[:num_queries]
        ground_truth = ground_truth[:num_queries]

    encryption_config = config_mgr.raw_config.get("encryption", {})
    encryption_enabled = encryption_config.get("enabled", False)

    if encryption_enabled:
        n_probe = encryption_config.get("n_probe", 4)
        from fhe_wrapper import FHESearcherWrapper
        config_path = get_config_path()
        wrapper = FHESearcherWrapper(config_path)

        use_encryption = encryption_config.get("use_encryption", True)

        if batch_mode:
            mode_str = "encrypted" if use_encryption else "plaintext"
            print(f"\nRunning C++ {mode_str} ANNS in BATCH mode "
                  f"(n_probe={n_probe}, top_k={top_k}) over {len(X_query)} queries with jobs={jobs}...")
            ordered_pred_ids, total_latency, avg_active, throughput = \
                wrapper.search_batch(top_k=top_k, jobs=jobs, limit=num_queries)

            pq_predictions = []
            for pred_ids in ordered_pred_ids:
                if len(pred_ids) < top_k:
                    pred_ids = np.pad(pred_ids, (0, top_k - len(pred_ids)), constant_values=-1)
                pq_predictions.append(pred_ids[:top_k])
            pq_predictions = np.array(pq_predictions)

            print(f"[OK] Total C++ Core Batch Time (Wall-clock): {total_latency:.2f} ms")
            print(f"[OK] Average Query Active Computation:        {avg_active:.2f} ms")
            print(f"[OK] Effective Throughput Latency:            {throughput:.2f} queries/sec")
        else:
            # Single-query mode
            print(f"\nRunning C++ FHE ANNS in SINGLE-QUERY mode "
                  f"(n_probe={n_probe}, top_k={top_k}) over {len(X_query)} queries...")
            pq_predictions = []
            latencies = []

            for i in range(len(X_query)):
                pred_ids, latency_report = wrapper.search(query_idx=i, top_k=top_k)
                if len(pred_ids) < top_k:
                    pred_ids = np.pad(pred_ids, (0, top_k - len(pred_ids)), constant_values=-1)
                pq_predictions.append(pred_ids[:top_k])

                try:
                    import re
                    m = re.search(r'total=([\d.]+)ms', latency_report)
                    if m:
                        latencies.append(float(m.group(1)))
                except Exception:
                    pass

            pq_predictions = np.array(pq_predictions)
            avg_latency = np.mean(latencies) if latencies else 0.0
            print(f"[OK] Average C++ Core Query Latency: {avg_latency:.2f} ms")
    else:
        # Pure Python plaintext path
        n_probe = config_mgr.ivf.n_list
        searcher = IVFPQSearcher(
            models_dir=config_mgr.dataset.models_output_dir,
            index_dir=config_mgr.dataset.encoding_output_dir,
            m_subvectors=config_mgr.pq.m_subvectors,
            k_subcentroids=config_mgr.pq.k_subcentroids
        )

        print(f"\nRunning IVF-PQ ADC search (n_probe={n_probe}, top_k={top_k}) over {len(X_query)} queries...")
        pq_predictions = []

        for q in X_query:
            pred_ids, _ = searcher.query(q, n_probe=n_probe, top_k=top_k)
            if len(pred_ids) < top_k:
                pred_ids = np.pad(pred_ids, (0, top_k - len(pred_ids)), constant_values=-1)
            pq_predictions.append(pred_ids)

        pq_predictions = np.array(pq_predictions)

    # ---- Compute Recall@K ----
    recall_score = IVFPQSearcher.evaluate_recall(ground_truth, pq_predictions, k=top_k)

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
    print(f"IVF Coarse Centroids (n_list): {config_mgr.ivf.n_list}")
    print(f"PQ Subspace Count (M):         {config_mgr.pq.m_subvectors}")
    print(f"Search Execution Knobs:        n_probe={encryption_config.get('n_probe', config_mgr.ivf.n_list)}, Top-K={top_k}")
    print("-" * 60)
    print(f"Validated Recall@{top_k} (Mean):      {avg_recall * 100:.2f}%")
    print(f"Recall Standard Deviation:     {std_recall * 100:.2f}%")
    print(f"Recall Range:                  [{min_recall * 100:.1f}% - {max_recall * 100:.1f}%]")
    print("=" * 60)

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


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="FHE-ANNS pipeline: train models, encode, preprocess FHE index, or query.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python main.py create_models
  python main.py encode_dataset
  python main.py fhe_preprocess
  python main.py test_query --top_k 10
  python main.py test_query --top_k 8 --batch -j 4 -n 50
"""
    )

    parser.add_argument(
        "function",
        choices=["create_models", "encode_dataset", "fhe_preprocess", "test_query"],
        help=(
            "create_models   — Train IVF centroids and PQ codebooks, save to disk.\n"
            "encode_dataset  — Assign and PQ-encode all database vectors.\n"
            "fhe_preprocess  — (Offline, one-time) Generate FHE keys and encrypt the index.\n"
            "test_query      — Run ANNS queries (plaintext or FHE) and report Recall@K."
        )
    )

    parser.add_argument(
        "--top_k",
        type=int,
        default=8,
        help="Number of nearest neighbors to retrieve (default: 8). "
             "Passed directly to C++ core via argument; not read from config.json."
    )

    parser.add_argument(
        "--batch",
        action="store_true",
        help="Run queries in batch mode for true throughput measurement."
    )

    parser.add_argument(
        "-j", "--jobs",
        type=int,
        default=1,
        help="Number of parallel threads for batch mode (default: 1)."
    )

    parser.add_argument(
        "-n", "--num_queries",
        type=int,
        default=10,
        help="Max number of queries to run (default: 10). Set to -1 for all."
    )

    args = parser.parse_args()

    if args.function == "create_models":
        create_models()
    elif args.function == "encode_dataset":
        encode_dataset()
    elif args.function == "fhe_preprocess":
        fhe_preprocess()
    elif args.function == "test_query":
        test_query(args.top_k, args.batch, args.jobs, args.num_queries)
    else:
        print("Invalid function choice")
        sys.exit(1)


if __name__ == "__main__":
    main()