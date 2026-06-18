import numpy as np
import os
from typing import Tuple, List

class IVFPQSearcher:
    """
    Handles Asymmetric Distance Computation (ADC) queries over an IVF-PQ index
    and calculates validation metrics (Recall@K).
    """
    def __init__(self, models_dir: str, index_dir: str, m_subvectors: int):
        self.m_subvectors = m_subvectors
        
        # Load Preprocessing Artifacts
        self.ivf_centroids = np.load(os.path.join(models_dir, "ivf_centroids.npy"))
        self.pq_codebooks = np.load(os.path.join(models_dir, "pq_codebooks.npy"))
        
        # Load Population Database Arrays
        self.ivf_assignments = np.load(os.path.join(index_dir, "ivf_assignments.npy"))
        self.pq_codes = np.load(os.path.join(index_dir, "pq_codes.npy"))
        
        self.dimension = self.ivf_centroids.shape[1]
        self.sub_dim = self.dimension // self.m_subvectors


    def query(self, q: np.ndarray, n_probe: int, top_k: int) -> Tuple[np.ndarray, np.ndarray]:
        """
        Executes a single top-k search using Asymmetric Distance Computation (ADC).
        
        Args:
            q (np.ndarray): Single query vector of shape (dimension,).
            n_probe (int): Number of IVF coarse clusters to inspect.
            top_k (int): Number of nearest neighbors to retrieve.
            
        Returns:
            Tuple[np.ndarray, np.ndarray]: (top_k_indices, estimated_distances)
        """
        # Coarse Quantization: Find the closest n_probe IVF centroids
        q_centroids_dist = np.sum((self.ivf_centroids - q) ** 2, axis=1)
        target_clusters = np.argsort(q_centroids_dist)[:n_probe]

        # Find all vector IDs belonging to these target clusters
        candidate_mask = np.isin(self.ivf_assignments, target_clusters)
        candidate_ids = np.where(candidate_mask)[0]
        
        if len(candidate_ids) == 0:
            return np.array([]), np.array([])

        # Build ADC Distance Lookup Tables for this query
        # For each subspace, compute distance from query slice to all 256 sub-centroids
        lookup_tables = np.zeros((self.m_subvectors, 256), dtype=np.float32)
        
        for m in range(self.m_subvectors):
            start_col = m * self.sub_dim
            end_col = start_col + self.sub_dim
            q_slice = q[start_col:end_col]
            
            # Distance from query subvector to the 256 sub-centroids (on residual space)
            # Query needs its current coarse cluster contribution subtracted to align with residual space
            # For simplicity in ADC, we can compute distance directly between raw query slice and codebooks
            sub_codebook = self.pq_codebooks[m]
            lookup_tables[m] = np.sum((sub_codebook - q_slice) ** 2, axis=1)

        # Distance Estimation via Lookups
        candidate_codes = self.pq_codes[candidate_ids] # Shape: (Num_Candidates, M)
        
        # Fast vectorized row lookup aggregation
        estimated_distances = np.zeros(len(candidate_ids), dtype=np.float32)
        for m in range(self.m_subvectors):
            estimated_distances += lookup_tables[m, candidate_codes[:, m]]

        # Top-K Selection
        if len(estimated_distances) <= top_k:
            sorted_local_idx = np.argsort(estimated_distances)
        else:
            sorted_local_idx = np.argpartition(estimated_distances, top_k)[:top_k]
            sorted_local_idx = sorted_local_idx[np.argsort(estimated_distances[sorted_local_idx])]

        return candidate_ids[sorted_local_idx], estimated_distances[sorted_local_idx]


    @staticmethod
    def evaluate_recall(ground_truth: np.ndarray, predictions: np.ndarray, k: int) -> float:
        """
        Computes the Recall@K metric.
        Checks what fraction of the true nearest neighbors are captured in our predictions.
        """
        num_queries = ground_truth.shape[0]
        hits = 0
        
        for i in range(num_queries):
            true_set = set(ground_truth[i, :k])
            pred_set = set(predictions[i, :k])
            hits += len(true_set.intersection(pred_set))
            
        return hits / (num_queries * k)