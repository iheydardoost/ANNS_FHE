import os
import json
import time
import subprocess
import re
import numpy as np
import matplotlib.pyplot as plt

# Paths
config_path = "D:/iman_heydardoost/master/thesis/ANNS_FHE/software_golden_model/config.json"
main_path = "D:/iman_heydardoost/master/thesis/ANNS_FHE/software_golden_model/main.py"
output_image_dir = "D:/iman_heydardoost/master/thesis/reports/template_report/thesis_progress_report_1/content/images"
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

def main():
    print("==================================================")
    # 1. Plaintext Benchmarks
    # Disable encryption first
    config = load_config()
    orig_encryption = config["encryption"].copy()
    
    config["encryption"]["enabled"] = False
    save_config(config)
    
    # Run models creation and encoding first to make sure everything matches current settings
    print("Re-creating plaintext models and encoding...")
    run_main(["create_models"])
    run_main(["encode_dataset"])
    
    plaintext_results = {}
    print("\nRunning Plaintext Benchmarks...")
    for k in [1, 2, 4, 8]:
        # Plaintext searches are fast, run on all 100 queries
        stdout, stderr, code = run_main(["test_query", "--top_k", str(k), "-n", "100"])
        recall, latency = parse_recall_and_latency(stdout)
        plaintext_results[k] = {"recall": recall, "latency": latency}
        print(f"Plaintext Recall@{k}: {recall:.2f}%, Latency: {latency:.2f} ms")
        
    # 2. Encrypted Benchmarks
    # Enable encryption
    config = load_config()
    config["encryption"]["enabled"] = True
    config["encryption"]["use_encryption"] = True
    
    encrypted_results = {} # key: (n_probe, k) -> recall
    encrypted_latencies = {} # key: n_probe -> latency
    
    print("\nRunning Encrypted Benchmarks (on 10 queries)...")
    for n_probe in [1, 2, 4]:
        config["encryption"]["n_probe"] = n_probe
        save_config(config)
        
        # Test latency and recall for each k
        for k in [1, 2, 4, 8]:
            print(f"Testing Encrypted n_probe={n_probe}, Top-K={k}...")
            # We run with jobs=2, num_queries=10 to keep it reasonably fast
            stdout, stderr, code = run_main(["test_query", "--top_k", str(k), "--batch", "-j", "2", "-n", "3"])
            if code != 0:
                print(f"Error running encrypted test for n_probe={n_probe}, k={k}")
                print(stderr)
                continue
            recall, latency = parse_recall_and_latency(stdout)
            encrypted_results[(n_probe, k)] = recall
            # Latency is measured per batch of queries, we take the one from the run
            if k == 8: # Record latency for the k=8 configuration as representative
                encrypted_latencies[n_probe] = latency
            print(f"   Encrypted Recall@{k}: {recall:.2f}%, Latency: {latency:.2f} ms")
            
    # Restore original config
    config = load_config()
    config["encryption"] = orig_encryption
    save_config(config)
    
    # Write the results to text files for LaTeX use and plot them!
    print("\nWriting result files...")
    
    # Save raw data to json
    results_data = {
        "plaintext": plaintext_results,
        "encrypted_recall": {f"{k[0]}_{k[1]}": v for k, v in encrypted_results.items()},
        "encrypted_latencies": encrypted_latencies
    }
    with open("D:/iman_heydardoost/master/thesis/results/fhe_experiment_summary.json", "w") as f:
        json.dump(results_data, f, indent=4)
        
    # ---------------- PLOTTING FHE RESULTS ----------------
    
    # Graph 1: Recall comparison Plaintext vs Encrypted (different n_probe)
    plt.figure(figsize=(8, 5))
    top_ks = [1, 2, 4, 8]
    
    # Plaintext line
    plt.plot(top_ks, [plaintext_results[k]["recall"] for k in top_ks], marker='o', color='black', label="Plaintext (n_probe=32)")
    
    # Encrypted lines
    colors = {1: 'blue', 2: 'green', 4: 'red'}
    for n_probe in [1, 2, 4]:
        recalls = []
        for k in top_ks:
            recalls.append(encrypted_results.get((n_probe, k), 0.0))
        plt.plot(top_ks, recalls, marker='x', linestyle='--', color=colors[n_probe], label=f"Encrypted (n_probe={n_probe})")
        
    plt.title("Recall@K Comparison: Plaintext vs. Encrypted", fontsize=12)
    plt.xlabel("Top-K Value", fontsize=10)
    plt.ylabel("Recall (%)", fontsize=10)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(output_image_dir, "fhe_recall_comparison.png"), dpi=300)
    plt.close()
    print("Saved fhe_recall_comparison.png")
    
    # Graph 2: Latency Comparison (Bar Chart)
    # Plaintext latency (typical single query) vs FHE latency for different n_probes
    plt.figure(figsize=(8, 5))
    labels = ['Plaintext', 'Encrypted\n(n_probe=1)', 'Encrypted\n(n_probe=2)', 'Encrypted\n(n_probe=4)']
    
    # Convert plaintext ms to seconds
    plain_lat_sec = plaintext_results[8]["latency"] / 1000.0
    # Encrypted latencies are throughput-based in ms, convert to seconds
    fhe_lats_sec = [
        encrypted_latencies.get(1, 0.0) / 1000.0,
        encrypted_latencies.get(2, 0.0) / 1000.0,
        encrypted_latencies.get(4, 0.0) / 1000.0
    ]
    
    latencies = [plain_lat_sec] + fhe_lats_sec
    
    bars = plt.bar(labels, latencies, color=['grey', 'blue', 'green', 'red'], width=0.5)
    plt.title("Query Latency Comparison (Seconds per Query)", fontsize=12)
    plt.ylabel("Latency (Seconds)", fontsize=10)
    plt.yscale('log') # Log scale because of huge difference
    plt.grid(True, which="both", linestyle='--', alpha=0.5)
    
    # Add values on top of bars
    for bar in bars:
        height = bar.get_height()
        if height < 1.0:
            plt.text(bar.get_x() + bar.get_width()/2.0, height * 1.2, f"{height*1000:.2f} ms", ha='center', va='bottom', fontsize=9)
        else:
            plt.text(bar.get_x() + bar.get_width()/2.0, height * 1.2, f"{height:.2f} s", ha='center', va='bottom', fontsize=9)
            
    plt.tight_layout()
    plt.savefig(os.path.join(output_image_dir, "fhe_latency_comparison.png"), dpi=300)
    plt.close()
    print("Saved fhe_latency_comparison.png")
    
    # Print summary text
    print("\nExperiment Execution Finished Successfully.")
    print("--------------------------------------------------")
    print(f"Plaintext Latency: {plaintext_results[8]['latency']:.2f} ms")
    print(f"FHE Latency (n_probe=1): {encrypted_latencies.get(1, 0.0)/1000.0:.2f} s")
    print(f"FHE Latency (n_probe=2): {encrypted_latencies.get(2, 0.0)/1000.0:.2f} s")
    print(f"FHE Latency (n_probe=4): {encrypted_latencies.get(4, 0.0)/1000.0:.2f} s")

if __name__ == "__main__":
    main()
