import numpy as np
import os

class IVFPQEncoder:
    """
    Loads pre-trained IVF-PQ components and encodes a high-dimensional dataset
    into coarse cluster assignments and quantized low-dimensional PQ codes.
    """
    def __init__(self, models_dir: str, m_subvectors: int, k_subcentroids: int):
        self.m_subvectors = m_subvectors
        self.k_subcentroids = k_subcentroids
        self.ivf_centroids = np.load(os.path.join(models_dir, "ivf_centroids.npy"))
        self.pq_codebooks = np.load(os.path.join(models_dir, "pq_codebooks.npy"))
        
        self.n_list = self.ivf_centroids.shape[0]
        self.dimension = self.ivf_centroids.shape[1]
        self.sub_dim = self.dimension // self.m_subvectors


    def encode_dataset(self, X: np.ndarray) -> tuple:
        """
        Encodes the entire dataset.
        
        Args:
            X (np.ndarray): Shape (N, dimension)
            
        Returns:
            tuple: 
                - ivf_assignments: (N,) array of cluster IDs (int32)
                - pq_codes: (N, M) array of quantized codes (uint8 -> values 0-255)
        """
        num_vectors, dim = X.shape
        print(f"Encoding {num_vectors} vectors...")

        # Assign to nearest IVF coarse centroid
        X_sq = np.sum(X**2, axis=1, keepdims=True)
        c_sq = np.sum(self.ivf_centroids**2, axis=1, keepdims=True).T
        distances_sq = np.clip(X_sq + c_sq - 2 * np.dot(X, self.ivf_centroids.T), a_min=0, a_max=None)
        
        ivf_assignments = np.argmin(distances_sq, axis=1).astype(np.int32)
        print("✓ Coarse IVF assignments complete.")

        # Compute Residuals
        residuals = X - self.ivf_centroids[ivf_assignments]
        print("✓ Residuals calculated.")

        # Quantize residuals into PQ codes
        # Use proper data_type
        data_type = np.uint8
        if self.k_subcentroids <= 2**8:
            data_type = np.uint8
        elif self.k_subcentroids <= 2**16:
            data_type = np.uint16
        elif self.k_subcentroids <= 2**32:
            data_type = np.uint32
        elif self.k_subcentroids <= 2**64:
            data_type = np.uint64
        pq_codes = np.zeros((num_vectors, self.m_subvectors), dtype=data_type)

        for m in range(self.m_subvectors):
            start_col = m * self.sub_dim
            end_col = start_col + self.sub_dim
            res_subspace = residuals[:, start_col:end_col] # Shape: (N, sub_dim)
            
            # Fetch the codebook slice for this specific subspace
            codebook_subspace = self.pq_codebooks[m] # Shape: (k_subcentroids, sub_dim)
            
            # Compute distances between residuals and codebook sub-centroids
            r_sq = np.sum(res_subspace**2, axis=1, keepdims=True)
            code_sq = np.sum(codebook_subspace**2, axis=1, keepdims=True).T
            sub_distances_sq = np.clip(r_sq + code_sq - 2 * np.dot(res_subspace, codebook_subspace.T), a_min=0, a_max=None)
            
            # Save the closest sub-centroid index
            pq_codes[:, m] = np.argmin(sub_distances_sq, axis=1).astype(np.uint8)
            
        print("✓ IVF-PQ encoding complete.")
        return ivf_assignments, pq_codes