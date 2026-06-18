import numpy as np
import os

def save_preprocessing_artifacts(ivf_centroids: np.ndarray, pq_codebooks: np.ndarray, output_dir: str = "models"):
    """Saves the trained IVF centroids and PQ codebooks as binary files."""
    os.makedirs(output_dir, exist_ok=True)
    
    ivf_path = os.path.join(output_dir, "ivf_centroids.npy")
    pq_path = os.path.join(output_dir, "pq_codebooks.npy")
    
    np.save(ivf_path, ivf_centroids)
    np.save(pq_path, pq_codebooks)
    
    print(f"✓ Saved IVF centroids to {ivf_path} {ivf_centroids.shape}")
    print(f"✓ Saved PQ codebooks to {pq_path} {pq_codebooks.shape}")


