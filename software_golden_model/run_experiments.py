import os
import sys
import json
import subprocess
import re
import csv
import itertools

# ==============================================================================
# CONFIGURATION CONSTANTS (Set these as needed)
# ==============================================================================
CONFIG_PATH = "config.json"
MAIN_PATH = "main.py"
OUTPUT_CSV_PATH = "../../results/siftsmall_ivfpq_experiment_results.csv"

# Parameter combinations to test
IVF_N_LIST = [4, 8, 16, 32, 64, 128, 256]
PQ_M_SUBVECTORS = [2, 4, 8, 16, 32]
PQ_K_SUBCENTROIDS = [2**4, 2**6, 2**8, 2**10]
TOP_K_VALUES = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 28, 32]
# ==============================================================================

def run_command(args, cwd=None):
    """Runs a shell command and returns its stdout."""
    # Use the same python interpreter executing this script
    cmd = [sys.executable] + args

    # Copy current environment and force UTF-8 for Python I/O
    current_env = os.environ.copy()
    current_env["PYTHONIOENCODING"] = "utf-8"

    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=current_env)
    if result.returncode != 0:
        print(f"Error executing command: {' '.join(cmd)}")
        print(f"Stdout:\n{result.stdout}")
        print(f"Stderr:\n{result.stderr}")
        result.check_returncode()
    return result.stdout

def parse_metrics(stdout):
    """Parses recall metrics from main.py test_query output."""
    mean_recall = 0.0
    std_recall = 0.0
    min_recall = 0.0
    max_recall = 0.0

    for line in stdout.splitlines():
        if "Validated Recall@" in line:
            match = re.search(r"(\d+\.\d+)%", line)
            if match:
                mean_recall = float(match.group(1))
        elif "Recall Standard Deviation:" in line:
            match = re.search(r"(\d+\.\d+)%", line)
            if match:
                std_recall = float(match.group(1))
        elif "Recall Range:" in line:
            match = re.search(r"\[(\d+\.\d+)%\s*-\s*(\d+\.\d+)%\]", line)
            if match:
                min_recall = float(match.group(1))
                max_recall = float(match.group(2))
                
    return mean_recall, std_recall, min_recall, max_recall

def main():
    # Resolve paths relative to where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_abs = os.path.abspath(os.path.join(script_dir, CONFIG_PATH))
    main_abs = os.path.abspath(os.path.join(script_dir, MAIN_PATH))
    output_abs = os.path.abspath(os.path.join(script_dir, OUTPUT_CSV_PATH))
    
    # Check if files exist
    if not os.path.exists(config_abs):
        print(f"Error: config.json not found at {config_abs}")
        sys.exit(1)
    if not os.path.exists(main_abs):
        print(f"Error: main.py not found at {main_abs}")
        sys.exit(1)

    print("==============================================================================")
    print("                     STARTING ANNS-FHE EXPERIMENT RUNNER                      ")
    print("==============================================================================")
    print(f"Config File: {config_abs}")
    print(f"Main Script: {main_abs}")
    print(f"Output CSV:  {output_abs}")
    print("==============================================================================")

    # Initialize CSV file with headers
    headers = [
        "n_list", 
        "m_subvectors", 
        "k_subcentroids", 
        "top_k", 
        "mean_recall_pct", 
        "std_recall_pct", 
        "min_recall_pct", 
        "max_recall_pct"
    ]
    
    with open(output_abs, mode="w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(headers)

    # Load base configuration to modify
    with open(config_abs, "r") as f:
        config_data = json.load(f)

    # Generate all test parameter combinations
    combinations = list(itertools.product(IVF_N_LIST, PQ_M_SUBVECTORS, PQ_K_SUBCENTROIDS))
    total_combinations = len(combinations)
    
    print(f"Found {total_combinations} parameter combinations to test.")
    
    # Iterate over combinations
    for idx, (n_list, m_subvectors, k_subcentroids) in enumerate(combinations, 1):
        print(f"\n[{idx}/{total_combinations}] Testing: n_list={n_list}, M={m_subvectors}, k_subcentroids={k_subcentroids}...")
        
        # Check divisibility before running to avoid errors
        dimension = config_data["dataset"]["dimension"]
        if dimension % m_subvectors != 0:
            print(f"   Skipping combination: Dimension {dimension} is not divisible by M={m_subvectors}.")
            continue
            
        # Update config dictionary
        config_data["ivf"]["n_list"] = n_list
        config_data["pq"]["m_subvectors"] = m_subvectors
        config_data["pq"]["k_subcentroids"] = k_subcentroids
        
        # Write config back to file
        with open(config_abs, "w") as f:
            json.dump(config_data, f, indent=4)
            
        # 1. Create Models (coarse quantization centroids & PQ subspaces)
        print("   -> Creating IVF & PQ models...")
        run_command([main_abs, "create_models"])
        
        # 2. Encode Dataset (compress vectors into indices)
        print("   -> Encoding database vectors...")
        run_command([main_abs, "encode_dataset"])
        
        # 3. Test queries for all top_k values
        for top_k in TOP_K_VALUES:
            print(f"   -> Querying top_k={top_k}...")
            stdout = run_command([main_abs, "test_query", "--top_k", str(top_k), "-n", "-1", "--batch", "-j", "4"])
            
            # Parse metrics from stdout
            mean_rec, std_rec, min_rec, max_rec = parse_metrics(stdout)
            print(f"      Mean Recall@{top_k}: {mean_rec:.2f}%")
            
            # Append result row to CSV
            row = [n_list, m_subvectors, k_subcentroids, top_k, mean_rec, std_rec, min_rec, max_rec]
            with open(output_abs, mode="a", newline="", encoding="utf-8") as csv_file:
                writer = csv.writer(csv_file)
                writer.writerow(row)

    print("\n==============================================================================")
    print("                      ALL EXPERIMENTS COMPLETED SUCCESSFULLY                  ")
    print(f"Results written to: {output_abs}")
    print("==============================================================================")

if __name__ == "__main__":
    main()
