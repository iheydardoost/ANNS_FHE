import os
import json
import time
import subprocess
import re
import numpy as np
import matplotlib.pyplot as plt
from data_loader import SiftDataLoader
from config_manager import IVFPQConfigManager

# ==============================================================================
# BENCHMARK CONFIGURATION KNOBS (Configure at the top of the script)
# ==============================================================================
TOP_K_CANDIDATES = [1, 2, 4, 8, 16]
N_PROBE_CANDIDATES_PLAIN = [1, 2, 4, 8, 16, 32]
N_PROBE_CANDIDATES_ENC = [1, 2, 4]
NUM_QUERIES = 10
# ==============================================================================

# Paths
script_dir = os.path.dirname(os.path.abspath(__file__))
config_path = os.path.abspath(os.path.join(script_dir, "config.json"))
main_path = os.path.abspath(os.path.join(script_dir, "main.py"))
output_image_dir = os.path.abspath(os.path.join(script_dir, "../../reports/template_report/thesis_progress_report_1/content/images"))
summary_json_path = os.path.abspath(os.path.join(script_dir, "../../results/fhe_experiment_summary.json"))
os.makedirs(output_image_dir, exist_ok=True)

# Helper to run python script with UTF-8 encoding
def run_main(args):
    cmd = ["python", main_path] + args
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    result = subprocess.run(cmd, capture_output=True, text=True, env=env)
    return result.stdout, result.stderr, result.returncode

def load_config():
    with open(config_path, "r") as f:
        return json.load(f)

def save_config(config):
    with open(config_path, "w") as f:
        json.dump(config, f, indent=4)

def parse_recall_and_latency(stdout):
    mean_recall = 0.0
    latency = 0.0
    for line in stdout.splitlines():
        if "Validated Recall@" in line:
            match = re.search(r"(\d+\.\d+)%", line)
            if match:
                mean_recall = float(match.group(1))
        elif "Effective Query Latency" in line or "Average C++ Core Query Latency" in line:
            match = re.search(r"(\d+\.\d+)\s*ms", line)
            if match:
                latency = float(match.group(1))
    return mean_recall, latency

def parse_predicted_neighbors(stdout):
    # Dict mapping query index -> list of predicted IDs
    q_predictions = {}
    current_q_idx = None
    
    for line in stdout.splitlines():
        line = line.strip()
        
        # 1. Match and extract the Query index (e.g., "Query 01:")
        if line.startswith("Query"):
            try:
                # Splits "Query 01:" -> ["Query", "01:"] -> strips the colon and converts to int
                current_q_idx = int(line.split()[1].rstrip(':'))
            except (IndexError, ValueError):
                continue
        
        # 2. Match the Pred Neighbors line for the active query
        elif line.startswith("Pred Neighbors:") and current_q_idx is not None:
            # Find all sequences of digits wrapped inside np.int32(...)
            pred_ids = [int(num) for num in re.findall(r'np\.int32\((\d+)\)', line)]
            q_predictions[current_q_idx-1] = pred_ids
            
    return q_predictions

def main():
    print("==================================================")
    print("            STARTING FHE BENCHMARK RUNNER         ")
    print("==================================================")
    print(f"Top-K Candidates:   {TOP_K_CANDIDATES}")
    print(f"n_probe Candidates plaintext: {N_PROBE_CANDIDATES_PLAIN}")
    print(f"n_probe Candidates encrypted: {N_PROBE_CANDIDATES_ENC}")
    print(f"Number of Queries:  {NUM_QUERIES}")
    print("==================================================")

    # Backup original configuration
    config = load_config()
    orig_ivf = config["ivf"].copy()
    orig_pq = config["pq"].copy()
    orig_encryption = config["encryption"].copy()

    # Configure baseline evaluation parameters (Safe & standard sizes)
    config["ivf"]["n_list"] = 32
    config["pq"]["m_subvectors"] = 8
    config["pq"]["k_subcentroids"] = 256
    config["encryption"]["enabled"] = True
    config["encryption"]["use_encryption"] = False
    save_config(config)

    # Load configuration using helper to fetch groundtruth path
    config_mgr = IVFPQConfigManager(config_path)
    query_loader = SiftDataLoader(
        file_path=config_mgr.dataset.query_path, 
        expected_dim=config_mgr.dataset.dimension
    )
    ground_truth = query_loader.load_groundtruth(config_mgr.dataset.groundtruth_path)
    if NUM_QUERIES > 0:
        ground_truth = ground_truth[:NUM_QUERIES]

    # Re-create models and encode to make sure databases match baseline parameter sizes
    print("Re-creating standard benchmark models and encoding...")
    run_main(["create_models"])
    run_main(["encode_dataset"])

    # 1. Plaintext Benchmarks
    plaintext_results = {} # key: (n_probe, top_k) -> {"recall": recall, "latency": latency}
    print("\nRunning Plaintext Benchmarks (C++ core batch mode, jobs=4)...")
    for n_probe in N_PROBE_CANDIDATES_PLAIN:
        for k in TOP_K_CANDIDATES:
            # Update n_probe in config
            config = load_config()
            config["encryption"]["enabled"] = True
            config["encryption"]["use_encryption"] = False
            config["encryption"]["n_probe"] = n_probe
            save_config(config)
            
            # Run test_query with batch and j=4
            stdout, stderr, code = run_main(["test_query", "--top_k", str(k), "--batch", "-j", "4", "-n", str(NUM_QUERIES)])
            if code != 0:
                print(f"Error running plaintext test: {stderr}")
                continue
            recall, latency = parse_recall_and_latency(stdout)
            plaintext_results[(n_probe, k)] = {"recall": recall, "latency": latency}
            
            # Print with sub-millisecond precision formatting
            if latency < 1.0:
                print(f"Plaintext (n_probe={n_probe}, Top-K={k}): Recall: {recall:.2f}%, Latency: {latency * 1000.0:.1f} μs ({latency:.4f} ms)")
            else:
                print(f"Plaintext (n_probe={n_probe}, Top-K={k}): Recall: {recall:.2f}%, Latency: {latency:.2f} ms")

    # 2. Encrypted Benchmarks
    encrypted_results = {} # key: (n_probe, top_k) -> {"recall": recall, "latency": latency}
    print("\nRunning Encrypted Benchmarks (C++ core subprocess mode, jobs=2)...")
    max_k = max(TOP_K_CANDIDATES)

    for n_probe in N_PROBE_CANDIDATES_ENC:
        # Update n_probe and enable FHE
        config = load_config()
        config["encryption"]["enabled"] = True
        config["encryption"]["use_encryption"] = True
        config["encryption"]["n_probe"] = n_probe
        save_config(config)
        
        print(f"Running FHE n_probe={n_probe} (with max Top-K={max_k} for optimized parsing)...")
        stdout, stderr, code = run_main(["test_query", "--top_k", str(max_k), "--batch", "-j", "2", "-n", str(NUM_QUERIES)])
        if code != 0:
            print(f"Error running encrypted test: {stderr}")
            continue
            
        _, latency = parse_recall_and_latency(stdout)
        q_predictions = parse_predicted_neighbors(stdout)
        
        # Calculate sliced recalls locally to save massive execution time
        for k in TOP_K_CANDIDATES:
            hits = 0
            total_elements = len(q_predictions) * k
            for q_idx in range(len(q_predictions)):
                pred_ids = q_predictions.get(q_idx, [])
                true_set = set(ground_truth[q_idx, :k])
                pred_set = set(pred_ids[:k])
                hits += len(true_set.intersection(pred_set))
            recall = (hits / total_elements) * 100.0 if total_elements > 0 else 0.0
            encrypted_results[(n_probe, k)] = {"recall": recall, "latency": latency}
            print(f"   Encrypted (n_probe={n_probe}, Top-K={k}): Recall: {recall:.2f}%, Latency: {latency:.2f} ms")

    # Restore original configuration
    config = load_config()
    config["ivf"] = orig_ivf
    config["pq"] = orig_pq
    config["encryption"] = orig_encryption
    save_config(config)

    # Write results to json
    print("\nWriting result files...")
    results_data = {
        "plaintext": {f"{k[0]}_{k[1]}": v for k, v in plaintext_results.items()},
        "encrypted": {f"{k[0]}_{k[1]}": v for k, v in encrypted_results.items()}
    }
    with open(summary_json_path, "w") as f:
        json.dump(results_data, f, indent=4)

    # ==================== PLOTTING GRAPHS ====================
    
    # Graph 1: Recall comparison Plaintext vs Encrypted
    plt.figure(figsize=(9, 6))
    
    # Plaintext lines (lighter shades)
    plain_colors = {1: 'lightblue', 2: 'lightgreen', 4: 'lightcoral', 8: 'khaki', 16: 'plum', 32: 'lightsalmon'}
    for n_probe in N_PROBE_CANDIDATES_PLAIN:
        recalls = [plaintext_results[(n_probe, k)]["recall"] for k in TOP_K_CANDIDATES]
        plt.plot(TOP_K_CANDIDATES, recalls, marker='o', color=plain_colors[n_probe], label=f"Plaintext (n_probe={n_probe})")
        
    # Encrypted lines (darker shades)
    fhe_colors = {1: 'blue', 2: 'green', 4: 'red', 8: 'gold', 16: 'purple', 32: 'darkorange'}
    for n_probe in N_PROBE_CANDIDATES_ENC:
        recalls = [encrypted_results[(n_probe, k)]["recall"] for k in TOP_K_CANDIDATES]
        plt.plot(TOP_K_CANDIDATES, recalls, marker='x', linestyle='--', color=fhe_colors[n_probe], label=f"Encrypted (n_probe={n_probe})")
        
    plt.title("Recall@K Comparison: Plaintext vs. Encrypted", fontsize=12)
    plt.xlabel("Top-K Value", fontsize=10)
    plt.ylabel("Recall (%)", fontsize=10)
    plt.xticks(TOP_K_CANDIDATES)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(output_image_dir, "fhe_recall_comparison.png"), dpi=300)
    plt.close()
    print("Saved fhe_recall_comparison.png")

    # Graph 2: Latency Comparison (Grouped Bar Chart for last 3 items in top_k)
    plt.figure(figsize=(11, 7))
    plot_ks = TOP_K_CANDIDATES[-3:] # [4, 8, 16]

    # Create groups on the x-axis
    groups = []
    for n_probe in N_PROBE_CANDIDATES_PLAIN:
        groups.append(f"Plain\n(n={n_probe})")
    for n_probe in N_PROBE_CANDIDATES_ENC:
        groups.append(f"FHE\n(n={n_probe})")

    # Collect latencies in seconds for each K
    latencies_by_k = {k: [] for k in plot_ks}
    for k in plot_ks:
        # Add Plaintext values (converted to seconds)
        for n_probe in N_PROBE_CANDIDATES_PLAIN:
            lat_sec = plaintext_results[(n_probe, k)]["latency"] / 1000.0
            latencies_by_k[k].append(lat_sec)
        # Add FHE values (converted to seconds)
        for n_probe in N_PROBE_CANDIDATES_ENC:
            lat_sec = encrypted_results[(n_probe, k)]["latency"] / 1000.0
            latencies_by_k[k].append(lat_sec)

    x = np.arange(len(groups))
    width = 0.25

    # Grouped bars
    r1 = x - width
    r2 = x
    r3 = x + width

    rects1 = plt.bar(r1, latencies_by_k[plot_ks[0]], width=width, label=f"K = {plot_ks[0]}", color='#4f81bd')
    rects2 = plt.bar(r2, latencies_by_k[plot_ks[1]], width=width, label=f"K = {plot_ks[1]}", color='#c0504d')
    rects3 = plt.bar(r3, latencies_by_k[plot_ks[2]], width=width, label=f"K = {plot_ks[2]}", color='#9bbb59')

    plt.title("Query Latency Comparison (Seconds per Query, Log Scale)", fontsize=12)
    plt.ylabel("Latency (Seconds)", fontsize=10)
    plt.xticks(x, groups)
    plt.yscale('log')
    plt.grid(True, which="both", linestyle='--', alpha=0.5)
    plt.legend()

    # Formatter for values on top of bars
    def label_bars(rects, values):
        for i, rect in enumerate(rects):
            val = values[i]
            height = rect.get_height()
            
            # Format sub-millisecond and larger values correctly
            if val < 0.001:  # < 1 ms
                val_ms = val * 1000.0
                if val_ms < 1.0:
                    label = f"{val_ms * 1000.0:.1f} μs"
                else:
                    label = f"{val_ms:.2f} ms"
            elif val < 1.0:  # < 1 s
                label = f"{val * 1000.0:.1f} ms"
            else:
                label = f"{val:.1f} s"
                
            plt.text(rect.get_x() + rect.get_width()/2.0, height * 1.15, label,
                     ha='center', va='bottom', fontsize=7, rotation=30)

    label_bars(rects1, latencies_by_k[plot_ks[0]])
    label_bars(rects2, latencies_by_k[plot_ks[1]])
    label_bars(rects3, latencies_by_k[plot_ks[2]])

    plt.tight_layout()
    plt.savefig(os.path.join(output_image_dir, "fhe_latency_comparison.png"), dpi=300)
    plt.close()
    print("Saved fhe_latency_comparison.png")

    print("\nExperiment execution completed successfully.")
    print("==================================================")

if __name__ == "__main__":
    main()
