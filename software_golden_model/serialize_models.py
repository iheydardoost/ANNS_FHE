import numpy as np
import os

def save_preprocessing_artifacts(ivf_centroids: np.ndarray, pq_codebooks: np.ndarray, output_dir: str = "models"):
    """Saves the trained IVF centroids and PQ codebooks as binary files."""
    os.makedirs(output_dir, exist_ok=True)
    
    ivf_path = os.path.join(output_dir, "ivf_centroids.npy")
    pq_path = os.path.join(output_dir, "pq_codebooks.npy")
    
    np.save(ivf_path, ivf_centroids)
    np.save(pq_path, pq_codebooks)
    
    # Save raw binaries for C++ core
    ivf_centroids.astype(np.float32).tofile(os.path.join(output_dir, "ivf_centroids.bin"))
    pq_codebooks.astype(np.float32).tofile(os.path.join(output_dir, "pq_codebooks.bin"))
    
    print(f"[OK] Saved IVF centroids to {ivf_path} and .bin format")
    print(f"[OK] Saved PQ codebooks to {pq_path} and .bin format")


