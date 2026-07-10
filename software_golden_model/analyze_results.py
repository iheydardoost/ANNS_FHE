import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os

# Define paths
script_dir = os.path.dirname(os.path.abspath(__file__))
csv_path = os.path.abspath(os.path.join(script_dir, "../../results/siftsmall_ivfpq_experiment_results.csv"))
output_image_dir = os.path.abspath(os.path.join(script_dir, "../../reports/template_report/thesis_progress_report_1/content/images"))
os.makedirs(output_image_dir, exist_ok=True)

# Load the experiment results
df = pd.read_csv(csv_path)

# Verify the shape and columns
print("CSV columns:", df.columns)
print("CSV shape:", df.shape)

# Let's write a function to calculate the compression ratio
# Raw vector: 128 dimensions * 4 bytes/float = 512 bytes
# Compressed: M subvectors, each index fits in log2(k_subcentroids) bits
# Bytes = M * log2(k) / 8
def calc_compression_ratio(M, k):
    bits_per_code = np.log2(k)
    bytes_per_vector = M * bits_per_code / 8.0
    return 512.0 / bytes_per_vector

# Add compression ratio column
df['compression_ratio'] = df.apply(lambda r: calc_compression_ratio(r['m_subvectors'], r['k_subcentroids']), axis=1)

# Find top configurations based on Mean Recall@8 (or overall mean recall)
# We sort by mean_recall_pct descending
best_configs = df.sort_values(by="mean_recall_pct", ascending=False)
print("\nTop 10 configurations by mean recall:")
print(best_configs[['n_list', 'm_subvectors', 'k_subcentroids', 'top_k', 'mean_recall_pct', 'compression_ratio']].head(10))

# ----------------- PLOTTING GRAPHS -----------------

# Graph 1: Recall vs Top-K for different number of IVF centroids (n_list)
# Let's fix M=8, k_subcentroids=256 and plot for different n_list values
plt.figure(figsize=(8, 5))
m_val = 8
k_val = 256
df_n_list = df[(df['m_subvectors'] == m_val) & (df['k_subcentroids'] == k_val)]
for n_list in sorted(df_n_list['n_list'].unique()):
    subset = df_n_list[df_n_list['n_list'] == n_list].sort_values(by="top_k")
    if len(subset) > 0:
        plt.plot(subset['top_k'], subset['mean_recall_pct'], marker='o', label=f"n_list={n_list}")
plt.title(f"Recall vs. Top-K for different IVF Centroids (n_list)\n(Fixed M={m_val}, K_pq={k_val})", fontsize=12)
plt.xlabel("Top-K Value", fontsize=10)
plt.ylabel("Mean Recall (%)", fontsize=10)
plt.grid(True, linestyle='--', alpha=0.7)
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(output_image_dir, "recall_vs_topk_nlist.png"), dpi=300)
plt.close()
print("Saved recall_vs_topk_nlist.png")

# Graph 2: Recall vs Top-K for different PQ subvectors (M)
# Let's fix n_list=32, k_subcentroids=256 and plot for different M values
plt.figure(figsize=(8, 5))
n_list_val = 32
k_val = 256
df_m = df[(df['n_list'] == n_list_val) & (df['k_subcentroids'] == k_val)]
for m in sorted(df_m['m_subvectors'].unique()):
    subset = df_m[df_m['m_subvectors'] == m].sort_values(by="top_k")
    if len(subset) > 0:
        plt.plot(subset['top_k'], subset['mean_recall_pct'], marker='s', label=f"M={m}")
plt.title(f"Recall vs. Top-K for different PQ Subspaces (M)\n(Fixed n_list={n_list_val}, K_pq={k_val})", fontsize=12)
plt.xlabel("Top-K Value", fontsize=10)
plt.ylabel("Mean Recall (%)", fontsize=10)
plt.grid(True, linestyle='--', alpha=0.7)
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(output_image_dir, "recall_vs_topk_m.png"), dpi=300)
plt.close()
print("Saved recall_vs_topk_m.png")

# Graph 3: Recall vs Top-K for different PQ subcentroids (k_subcentroids)
# Let's fix n_list=32, M=8 and plot for different k_subcentroids values
plt.figure(figsize=(8, 5))
n_list_val = 32
m_val = 8
df_k = df[(df['n_list'] == n_list_val) & (df['m_subvectors'] == m_val)]
for k in sorted(df_k['k_subcentroids'].unique()):
    subset = df_k[df_k['k_subcentroids'] == k].sort_values(by="top_k")
    if len(subset) > 0:
        plt.plot(subset['top_k'], subset['mean_recall_pct'], marker='^', label=f"K_pq={k}")
plt.title(f"Recall vs. Top-K for different PQ Subcentroids (K_pq)\n(Fixed n_list={n_list_val}, M={m_val})", fontsize=12)
plt.xlabel("Top-K Value", fontsize=10)
plt.ylabel("Mean Recall (%)", fontsize=10)
plt.grid(True, linestyle='--', alpha=0.7)
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(output_image_dir, "recall_vs_topk_k.png"), dpi=300)
plt.close()
print("Saved recall_vs_topk_k.png")

# Let's print out the compression ratios for various configurations explicitly
print("\nCompression Ratios Table:")
print("| M (Subspaces) | K_pq (Subcentroids) | Bytes per Vector | Compression Ratio |")
print("| :--- | :--- | :--- | :--- |")
for m in [2, 4, 8, 16, 32]:
    for k in [16, 64, 256, 1024]:
        ratio = calc_compression_ratio(m, k)
        bytes_vec = m * np.log2(k) / 8
        print(f"| {m} | {k} | {bytes_vec:.2f} B | {ratio:.2f}x |")
