"""
run_experiments.py — Sweep IVF/PQ hyperparameters and record Recall@K metrics.

Runs create_models → encode_dataset → test_query for every combination of
  (n_list, m_subvectors, k_subcentroids) × (top_k),
writes results to a CSV, and prints progress as it goes.

Top-K is passed as a CLI argument to main.py; it is NOT read from config.json.
"""

import os
import sys
import json
import subprocess
import re
import csv
import itertools

# ==============================================================================
# CONFIGURATION CONSTANTS — adjust as needed
# ==============================================================================
CONFIG_PATH = "config.json"
MAIN_PATH = "main.py"
OUTPUT_CSV_PATH = "../../results/siftsmall_ivfpq_experiment_results.csv"

# Parameter grid
IVF_N_LIST         = [4, 8, 16, 32, 64, 128, 256]
PQ_M_SUBVECTORS    = [2, 4, 8, 16, 32]
PQ_K_SUBCENTROIDS  = [2**4, 2**6, 2**8, 2**10]
TOP_K_VALUES       = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24, 28, 32]
# ==============================================================================


def run_command(args, cwd=None):
    """Run a Python sub-command and return its stdout."""
    cmd = [sys.executable] + args
    current_env = os.environ.copy()
    current_env["PYTHONIOENCODING"] = "utf-8"
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=current_env)
    if result.returncode != 0:
        print(f"Error executing: {' '.join(cmd)}")
        print(f"Stdout:\n{result.stdout}")
        print(f"Stderr:\n{result.stderr}")
        result.check_returncode()
    return result.stdout


def parse_metrics(stdout):
    """Parse recall metrics from main.py test_query output."""
    mean_recall = 0.0
    std_recall = 0.0
    min_recall = 0.0
    max_recall = 0.0

    for line in stdout.splitlines():
        if "Validated Recall@" in line:
            m = re.search(r"([\d.]+)%", line)
            if m:
                mean_recall = float(m.group(1))
        elif "Recall Standard Deviation:" in line:
            m = re.search(r"([\d.]+)%", line)
            if m:
                std_recall = float(m.group(1))
        elif "Recall Range:" in line:
            m = re.search(r"\[([\d.]+)%\s*-\s*([\d.]+)%\]", line)
            if m:
                min_recall = float(m.group(1))
                max_recall = float(m.group(2))

    return mean_recall, std_recall, min_recall, max_recall


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_abs = os.path.abspath(os.path.join(script_dir, CONFIG_PATH))
    main_abs   = os.path.abspath(os.path.join(script_dir, MAIN_PATH))
    output_abs = os.path.abspath(os.path.join(script_dir, OUTPUT_CSV_PATH))

    if not os.path.exists(config_abs):
        print(f"Error: config.json not found at {config_abs}")
        sys.exit(1)
    if not os.path.exists(main_abs):
        print(f"Error: main.py not found at {main_abs}")
        sys.exit(1)

    os.makedirs(os.path.dirname(output_abs), exist_ok=True)

    print("=" * 78)
    print("              STARTING ANNS-FHE EXPERIMENT RUNNER (plaintext mode)")
    print("=" * 78)
    print(f"Config : {config_abs}")
    print(f"Script : {main_abs}")
    print(f"Output : {output_abs}")
    print("=" * 78)

    # CSV header — no top_k column in config; it is a sweep variable here
    headers = [
        "n_list", "m_subvectors", "k_subcentroids", "top_k",
        "mean_recall_pct", "std_recall_pct", "min_recall_pct", "max_recall_pct"
    ]
    with open(output_abs, mode="w", newline="", encoding="utf-8") as csv_file:
        csv.writer(csv_file).writerow(headers)

    with open(config_abs, "r") as f:
        config_data = json.load(f)

    # Make sure we run in plaintext mode for this sweep
    config_data.setdefault("encryption", {})
    config_data["encryption"]["enabled"] = False

    combinations = list(itertools.product(IVF_N_LIST, PQ_M_SUBVECTORS, PQ_K_SUBCENTROIDS))
    total = len(combinations)
    print(f"Parameter combinations: {total}\n")

    for idx, (n_list, m_subvectors, k_subcentroids) in enumerate(combinations, 1):
        print(f"[{idx}/{total}] n_list={n_list}, M={m_subvectors}, k_subcentroids={k_subcentroids}")

        dimension = config_data["dataset"]["dimension"]
        if dimension % m_subvectors != 0:
            print(f"   Skipping: {dimension} not divisible by M={m_subvectors}.")
            continue

        # Patch config for this combination
        config_data["ivf"]["n_list"]          = n_list
        config_data["pq"]["m_subvectors"]     = m_subvectors
        config_data["pq"]["k_subcentroids"]   = k_subcentroids

        with open(config_abs, "w") as f:
            json.dump(config_data, f, indent=4)

        print("   -> create_models ...")
        run_command([main_abs, "create_models"])

        print("   -> encode_dataset ...")
        run_command([main_abs, "encode_dataset"])

        for top_k in TOP_K_VALUES:
            print(f"   -> test_query --top_k {top_k} ...")
            # top_k is passed as a CLI arg — not from config.json
            stdout = run_command(
                [main_abs, "test_query", "--top_k", str(top_k), "-n", "-1"])

            mean_rec, std_rec, min_rec, max_rec = parse_metrics(stdout)
            print(f"      Recall@{top_k}: {mean_rec:.2f}%")

            row = [n_list, m_subvectors, k_subcentroids, top_k,
                   mean_rec, std_rec, min_rec, max_rec]
            with open(output_abs, mode="a", newline="", encoding="utf-8") as csv_file:
                csv.writer(csv_file).writerow(row)

    print("\n" + "=" * 78)
    print("           ALL EXPERIMENTS COMPLETED")
    print(f"Results: {output_abs}")
    print("=" * 78)


if __name__ == "__main__":
    main()
