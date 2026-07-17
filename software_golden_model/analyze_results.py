"""
analyze_results.py — Visualize IVF-PQ experiment results from run_experiments.py.

Reads the CSV produced by run_experiments.py (which sweeps n_list, M, k_subcentroids,
and top_k), generates three recall-vs-top_k plots, and prints a compression ratio table.

Note: top_k in the CSV is the search parameter (number of neighbors returned);
it is no longer stored in config.json but is recorded per-row in the results CSV.
"""

import argparse
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os
import sys


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
script_dir = os.path.dirname(os.path.abspath(__file__))

DEFAULT_CSV_PATH = os.path.abspath(
    os.path.join(script_dir, "../../results/siftsmall_ivfpq_experiment_results.csv"))

DEFAULT_OUTPUT_DIR = os.path.abspath(
    os.path.join(script_dir,
                 "../../reports/template_report/thesis_progress_report_1/content/images"))


# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

def calc_compression_ratio(M: int, k: int) -> float:
    """
    Compression ratio relative to raw float32 vectors (128-dim = 512 bytes).
    Compressed size = M * log2(k) / 8 bytes.
    """
    bits_per_code = np.log2(k)
    bytes_per_vector = M * bits_per_code / 8.0
    return 512.0 / bytes_per_vector


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Analyze IVF-PQ experiment results and generate plots.")
    parser.add_argument(
        "--csv", default=DEFAULT_CSV_PATH,
        help=f"Path to results CSV (default: {DEFAULT_CSV_PATH})")
    parser.add_argument(
        "--output_dir", default=DEFAULT_OUTPUT_DIR,
        help=f"Directory to save plots (default: {DEFAULT_OUTPUT_DIR})")
    args = parser.parse_args()

    csv_path   = args.csv
    output_dir = args.output_dir

    if not os.path.exists(csv_path):
        print(f"Error: CSV not found at: {csv_path}")
        print("Run run_experiments.py first to generate results.")
        sys.exit(1)

    os.makedirs(output_dir, exist_ok=True)

    # ── Load data ────────────────────────────────────────────────────────────
    df = pd.read_csv(csv_path)
    print(f"Loaded {len(df)} rows from: {csv_path}")
    print("Columns:", list(df.columns))

    # Add compression ratio column
    df['compression_ratio'] = df.apply(
        lambda r: calc_compression_ratio(r['m_subvectors'], r['k_subcentroids']), axis=1)

    # Top-10 configurations by mean recall
    best = df.sort_values("mean_recall_pct", ascending=False)
    print("\nTop-10 configurations by mean recall:")
    display_cols = ['n_list', 'm_subvectors', 'k_subcentroids', 'top_k',
                    'mean_recall_pct', 'compression_ratio']
    print(best[[c for c in display_cols if c in df.columns]].head(10).to_string(index=False))

    # ── Plot 1: Recall vs Top-K for different n_list ──────────────────────────
    m_val, k_val = 8, 256
    plt.figure(figsize=(8, 5))
    subset_df = df[(df['m_subvectors'] == m_val) & (df['k_subcentroids'] == k_val)]
    for n_list in sorted(subset_df['n_list'].unique()):
        sub = subset_df[subset_df['n_list'] == n_list].sort_values('top_k')
        if len(sub) > 0:
            plt.plot(sub['top_k'], sub['mean_recall_pct'], marker='o', label=f"n_list={n_list}")
    plt.title(f"Recall vs. Top-K — varying IVF Centroids\n(M={m_val}, K_pq={k_val})",
              fontsize=12)
    plt.xlabel("Top-K", fontsize=10)
    plt.ylabel("Mean Recall (%)", fontsize=10)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    out = os.path.join(output_dir, "recall_vs_topk_nlist.png")
    plt.savefig(out, dpi=300)
    plt.close()
    print(f"\nSaved: {out}")

    # ── Plot 2: Recall vs Top-K for different M ───────────────────────────────
    n_list_val, k_val = 32, 256
    plt.figure(figsize=(8, 5))
    subset_df = df[(df['n_list'] == n_list_val) & (df['k_subcentroids'] == k_val)]
    for m in sorted(subset_df['m_subvectors'].unique()):
        sub = subset_df[subset_df['m_subvectors'] == m].sort_values('top_k')
        if len(sub) > 0:
            plt.plot(sub['top_k'], sub['mean_recall_pct'], marker='s', label=f"M={m}")
    plt.title(f"Recall vs. Top-K — varying PQ Subspaces (M)\n"
              f"(n_list={n_list_val}, K_pq={k_val})", fontsize=12)
    plt.xlabel("Top-K", fontsize=10)
    plt.ylabel("Mean Recall (%)", fontsize=10)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    out = os.path.join(output_dir, "recall_vs_topk_m.png")
    plt.savefig(out, dpi=300)
    plt.close()
    print(f"Saved: {out}")

    # ── Plot 3: Recall vs Top-K for different K_pq ───────────────────────────
    n_list_val, m_val = 32, 8
    plt.figure(figsize=(8, 5))
    subset_df = df[(df['n_list'] == n_list_val) & (df['m_subvectors'] == m_val)]
    for k in sorted(subset_df['k_subcentroids'].unique()):
        sub = subset_df[subset_df['k_subcentroids'] == k].sort_values('top_k')
        if len(sub) > 0:
            plt.plot(sub['top_k'], sub['mean_recall_pct'], marker='^', label=f"K_pq={k}")
    plt.title(f"Recall vs. Top-K — varying PQ Subcentroids (K_pq)\n"
              f"(n_list={n_list_val}, M={m_val})", fontsize=12)
    plt.xlabel("Top-K", fontsize=10)
    plt.ylabel("Mean Recall (%)", fontsize=10)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    out = os.path.join(output_dir, "recall_vs_topk_k.png")
    plt.savefig(out, dpi=300)
    plt.close()
    print(f"Saved: {out}")

    # ── Compression ratio table ───────────────────────────────────────────────
    print("\nCompression Ratios Table:")
    print(f"{'M':>5} {'K_pq':>8} {'Bytes/Vec':>12} {'Ratio':>12}")
    print("-" * 42)
    for m in [2, 4, 8, 16, 32]:
        for k in [16, 64, 256, 1024]:
            ratio = calc_compression_ratio(m, k)
            bytes_vec = m * np.log2(k) / 8
            print(f"{m:>5} {k:>8} {bytes_vec:>11.2f}B {ratio:>11.2f}x")


if __name__ == "__main__":
    main()
