"""
run_fhe_benchmarks.py — Compare plaintext vs. encrypted ANNS recall and latency.

Workflow:
  1. Sets baseline IVF/PQ parameters (n_list=32, M=8, K=256) in config.json.
  2. Plaintext sweep: iterate over n_probe × top_k, measure recall & latency.
  3. Encrypted sweep: iterate over n_probe, run FHE search for max_k,
     then slice predictions to compute recall for each top_k locally.
  4. Plot recall comparison and latency bar charts.
  5. Write summary JSON.

top_k is passed as a CLI argument (--top_k <N>) — NOT read from config.json.
"""

import os
import json
import re
import time
import subprocess
import sys
import numpy as np
import matplotlib.pyplot as plt
from data_loader import SiftDataLoader
from config_manager import IVFPQConfigManager

# ==============================================================================
# BENCHMARK KNOBS
# ==============================================================================
TOP_K_CANDIDATES           = [1, 2, 4, 8, 16]
N_PROBE_CANDIDATES_PLAIN   = [1, 2, 4, 8, 16, 32]
N_PROBE_CANDIDATES_ENC     = [1, 2, 4]
NUM_QUERIES                = 10
# ==============================================================================

script_dir         = os.path.dirname(os.path.abspath(__file__))
config_path        = os.path.abspath(os.path.join(script_dir, "config.json"))
main_path          = os.path.abspath(os.path.join(script_dir, "main.py"))
output_image_dir   = os.path.abspath(os.path.join(
    script_dir, "../../reports/template_report/thesis_progress_report_1/content/images"))
summary_json_path  = os.path.abspath(os.path.join(
    script_dir, "../../results/fhe_experiment_summary.json"))

os.makedirs(output_image_dir, exist_ok=True)
os.makedirs(os.path.dirname(summary_json_path), exist_ok=True)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def run_main(args: list) -> tuple[str, str, int]:
    """Run main.py with the given args and return (stdout, stderr, returncode)."""
    cmd = [sys.executable, main_path] + [str(a) for a in args]
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    result = subprocess.run(cmd, capture_output=True, text=True, env=env)
    return result.stdout, result.stderr, result.returncode


def load_config() -> dict:
    with open(config_path, "r") as f:
        return json.load(f)


def save_config(config: dict) -> None:
    with open(config_path, "w") as f:
        json.dump(config, f, indent=4)


def parse_recall_and_latency(stdout: str) -> tuple[float, float]:
    """Extract mean recall (%) and effective query latency (ms) from main.py output."""
    mean_recall = 0.0
    latency = 0.0
    for line in stdout.splitlines():
        if "Validated Recall@" in line:
            m = re.search(r"([\d.]+)%", line)
            if m:
                mean_recall = float(m.group(1))
        elif "Effective Throughput Latency" in line or "Average C++ Core Query Latency" in line:
            m = re.search(r"([\d.]+)\s*ms", line)
            if m:
                latency = float(m.group(1))
    return mean_recall, latency


def parse_predicted_neighbors(stdout: str) -> dict[int, list[int]]:
    """
    Parse per-query predicted neighbor IDs from main.py test_query output.

    Expected format for each query:
        Query NN:
           Pred Neighbors: [idx0, idx1, ...]
    Returns dict: {query_idx (0-based) -> [pred_id, ...]}
    """
    q_predictions: dict[int, list[int]] = {}
    current_q_idx = None

    for line in stdout.splitlines():
        line = line.strip()

        if line.startswith("Query"):
            try:
                current_q_idx = int(line.split()[1].rstrip(':')) - 1  # Convert to 0-based
            except (IndexError, ValueError):
                current_q_idx = None

        elif line.startswith("Pred Neighbors:") and current_q_idx is not None:
            # Match both numpy-repr integers and plain integers
            pred_ids = [int(x) for x in re.findall(r'\b(\d+)\b', line.split(':', 1)[-1])]
            q_predictions[current_q_idx] = pred_ids

    return q_predictions


# ---------------------------------------------------------------------------
# Main benchmark runner
# ---------------------------------------------------------------------------

def main():
    print("=" * 52)
    print("        STARTING FHE BENCHMARK RUNNER")
    print("=" * 52)
    print(f"Top-K Candidates:              {TOP_K_CANDIDATES}")
    print(f"n_probe Candidates (plaintext): {N_PROBE_CANDIDATES_PLAIN}")
    print(f"n_probe Candidates (encrypted): {N_PROBE_CANDIDATES_ENC}")
    print(f"Number of Queries:              {NUM_QUERIES}")
    print("=" * 52)

    # ---- Save original config to restore at the end ----
    config = load_config()
    orig_config = json.loads(json.dumps(config))   # deep copy

    # ---- Baseline parameters for fair comparison ----
    config["ivf"]["n_list"]         = 32
    config["pq"]["m_subvectors"]    = 8
    config["pq"]["k_subcentroids"]  = 256
    config.setdefault("encryption", {})
    config["encryption"]["enabled"]        = True
    config["encryption"]["use_encryption"] = False
    # top_k is NOT set in config — it is passed via CLI
    save_config(config)

    # Load ground truth once
    config_mgr = IVFPQConfigManager(config_path)
    query_loader = SiftDataLoader(
        file_path=config_mgr.dataset.query_path,
        expected_dim=config_mgr.dataset.dimension
    )
    ground_truth = query_loader.load_groundtruth(config_mgr.dataset.groundtruth_path)
    if NUM_QUERIES > 0:
        ground_truth = ground_truth[:NUM_QUERIES]

    # Rebuild models and encoding for consistent baseline
    print("\nBuilding baseline models and encoding...")
    run_main(["create_models"])
    run_main(["encode_dataset"])

    # ── 1. Plaintext Benchmarks ──────────────────────────────────────────────
    plaintext_results: dict[tuple, dict] = {}
    print("\n--- Plaintext Benchmarks (batch mode, jobs=4) ---")

    for n_probe in N_PROBE_CANDIDATES_PLAIN:
        config = load_config()
        config["encryption"]["enabled"]        = True
        config["encryption"]["use_encryption"] = False
        config["encryption"]["n_probe"]        = n_probe
        save_config(config)

        for k in TOP_K_CANDIDATES:
            # top_k passed as --top_k <k> argument
            stdout, stderr, code = run_main(
                ["test_query", "--top_k", k, "--batch", "-j", "4", "-n", NUM_QUERIES])
            if code != 0:
                print(f"  [WARN] Plaintext test failed (n_probe={n_probe}, k={k}): {stderr[:200]}")
                plaintext_results[(n_probe, k)] = {"recall": 0.0, "latency": 0.0}
                continue

            recall, latency = parse_recall_and_latency(stdout)
            plaintext_results[(n_probe, k)] = {"recall": recall, "latency": latency}

            if latency < 1.0:
                lat_str = f"{latency * 1000.0:.1f} μs"
            else:
                lat_str = f"{latency:.2f} ms"
            print(f"  n_probe={n_probe}, K={k}: Recall={recall:.2f}%, Latency={lat_str}")

    # ── 2. Encrypted (FHE) Benchmarks ────────────────────────────────────────
    encrypted_results: dict[tuple, dict] = {}
    max_k = max(TOP_K_CANDIDATES)
    print(f"\n--- Encrypted (FHE) Benchmarks (single-query mode, max_k={max_k}) ---")

    for n_probe in N_PROBE_CANDIDATES_ENC:
        config = load_config()
        config["encryption"]["enabled"]        = True
        config["encryption"]["use_encryption"] = True
        config["encryption"]["n_probe"]        = n_probe
        save_config(config)

        print(f"  FHE n_probe={n_probe}, top_k={max_k} (slicing for all K locally)...")
        stdout, stderr, code = run_main(
            ["test_query", "--top_k", max_k, "-n", NUM_QUERIES])
        if code != 0:
            print(f"  [WARN] FHE test failed (n_probe={n_probe}): {stderr[:200]}")
            for k in TOP_K_CANDIDATES:
                encrypted_results[(n_probe, k)] = {"recall": 0.0, "latency": 0.0}
            continue

        _, latency = parse_recall_and_latency(stdout)
        q_predictions = parse_predicted_neighbors(stdout)

        # Compute Recall@k for each k by slicing the max_k predictions
        for k in TOP_K_CANDIDATES:
            hits = 0
            n_valid = len(q_predictions)
            for q_idx in range(n_valid):
                pred_ids = q_predictions.get(q_idx, [])
                true_set = set(int(x) for x in ground_truth[q_idx, :k])
                pred_set = set(pred_ids[:k])
                hits += len(true_set.intersection(pred_set))
            recall = (hits / (n_valid * k)) * 100.0 if n_valid > 0 else 0.0
            encrypted_results[(n_probe, k)] = {"recall": recall, "latency": latency}
            print(f"    K={k}: Recall={recall:.2f}%, Latency={latency:.2f} ms")

    # ── Restore original config ───────────────────────────────────────────────
    save_config(orig_config)

    # ── Write summary JSON ────────────────────────────────────────────────────
    print("\nWriting result files...")
    results_data = {
        "plaintext": {f"{k[0]}_{k[1]}": v for k, v in plaintext_results.items()},
        "encrypted": {f"{k[0]}_{k[1]}": v for k, v in encrypted_results.items()},
    }
    with open(summary_json_path, "w") as f:
        json.dump(results_data, f, indent=4)
    print(f"Summary JSON: {summary_json_path}")

    # ── Plot 1: Recall comparison ─────────────────────────────────────────────
    plt.figure(figsize=(9, 6))
    plain_colors = {1: 'lightblue', 2: 'lightgreen', 4: 'lightcoral',
                    8: 'khaki', 16: 'plum', 32: 'lightsalmon'}
    fhe_colors   = {1: 'blue', 2: 'green', 4: 'red', 8: 'gold', 16: 'purple', 32: 'darkorange'}

    for n_probe in N_PROBE_CANDIDATES_PLAIN:
        recalls = [plaintext_results.get((n_probe, k), {}).get("recall", 0.0)
                   for k in TOP_K_CANDIDATES]
        plt.plot(TOP_K_CANDIDATES, recalls, marker='o',
                 color=plain_colors.get(n_probe, 'gray'),
                 label=f"Plaintext (n_probe={n_probe})")

    for n_probe in N_PROBE_CANDIDATES_ENC:
        recalls = [encrypted_results.get((n_probe, k), {}).get("recall", 0.0)
                   for k in TOP_K_CANDIDATES]
        plt.plot(TOP_K_CANDIDATES, recalls, marker='x', linestyle='--',
                 color=fhe_colors.get(n_probe, 'black'),
                 label=f"Encrypted (n_probe={n_probe})")

    plt.title("Recall@K: Plaintext vs. Encrypted", fontsize=12)
    plt.xlabel("Top-K", fontsize=10)
    plt.ylabel("Recall (%)", fontsize=10)
    plt.xticks(TOP_K_CANDIDATES)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    out = os.path.join(output_image_dir, "fhe_recall_comparison.png")
    plt.savefig(out, dpi=300)
    plt.close()
    print(f"Saved {out}")

    # ── Plot 2: Latency bar chart ─────────────────────────────────────────────
    plot_ks = TOP_K_CANDIDATES[-3:]  # last 3 values
    groups = ([f"Plain\n(n={n})" for n in N_PROBE_CANDIDATES_PLAIN] +
              [f"FHE\n(n={n})"   for n in N_PROBE_CANDIDATES_ENC])

    latencies_by_k: dict[int, list[float]] = {k: [] for k in plot_ks}
    for k in plot_ks:
        for n_probe in N_PROBE_CANDIDATES_PLAIN:
            lat_ms = plaintext_results.get((n_probe, k), {}).get("latency", 0.0)
            latencies_by_k[k].append(lat_ms / 1000.0)  # convert to seconds
        for n_probe in N_PROBE_CANDIDATES_ENC:
            lat_ms = encrypted_results.get((n_probe, k), {}).get("latency", 0.0)
            latencies_by_k[k].append(lat_ms / 1000.0)

    x = np.arange(len(groups))
    width = 0.25
    plt.figure(figsize=(11, 7))
    rects1 = plt.bar(x - width, latencies_by_k[plot_ks[0]], width=width,
                     label=f"K={plot_ks[0]}", color='#4f81bd')
    rects2 = plt.bar(x,         latencies_by_k[plot_ks[1]], width=width,
                     label=f"K={plot_ks[1]}", color='#c0504d')
    rects3 = plt.bar(x + width, latencies_by_k[plot_ks[2]], width=width,
                     label=f"K={plot_ks[2]}", color='#9bbb59')

    plt.title("Query Latency: Plaintext vs. Encrypted (log scale)", fontsize=12)
    plt.ylabel("Latency (seconds)", fontsize=10)
    plt.xticks(x, groups)
    plt.yscale('log')
    plt.grid(True, which="both", linestyle='--', alpha=0.5)
    plt.legend()

    def label_bars(rects, vals):
        for rect, val in zip(rects, vals):
            h = rect.get_height()
            if val <= 0:
                continue
            if val < 0.001:
                label = f"{val * 1e6:.1f} μs"
            elif val < 1.0:
                label = f"{val * 1000:.1f} ms"
            else:
                label = f"{val:.1f} s"
            plt.text(rect.get_x() + rect.get_width() / 2, h * 1.15,
                     label, ha='center', va='bottom', fontsize=7, rotation=30)

    label_bars(rects1, latencies_by_k[plot_ks[0]])
    label_bars(rects2, latencies_by_k[plot_ks[1]])
    label_bars(rects3, latencies_by_k[plot_ks[2]])

    plt.tight_layout()
    out = os.path.join(output_image_dir, "fhe_latency_comparison.png")
    plt.savefig(out, dpi=300)
    plt.close()
    print(f"Saved {out}")

    print("\nAll benchmarks completed.")
    print("=" * 52)


if __name__ == "__main__":
    main()
