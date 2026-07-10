import numpy as np
from typing import List, Dict
from config_manager import PQConfig

class ProductQuantizer:
    """
    Handles the creation of Product Quantization (PQ) codebooks by decomposing
    the vector space into orthogonal subspaces and clustering each subspace independently.
    Tracks optimization progress using WCSS (Inertia) per subspace.
    """
    def __init__(self, config: PQConfig):
        self.m_subvectors = config.m_subvectors
        self.k_subcentroids = config.k_subcentroids
        self.max_iter = config.max_iter
        self.seed = config.seed
        self.tolerance = config.tolerance
        
        # Codebooks will hold the trained cluster centers for each subspace
        # Shape will eventually be: (M, k_subcentroids, sub_dimension)
        self.codebooks = None
        
        # History dictionary to track WCSS progress per subspace
        # Format: {subspace_index: [wcss_iter1, wcss_iter2, ...]}
        self.wcss_history: Dict[int, List[float]] = {}

    def fit(self, X: np.ndarray) -> np.ndarray:
        """
        Trains the PQ codebooks across all sub-spaces while logging evaluation metrics.
        
        Args:
            X (np.ndarray): The input dataset (or residuals) of shape (N, dimension).
            
        Returns:
            np.ndarray: Codebooks array of shape (m_subvectors, k_subcentroids, sub_dimension).
        """
        np.random.seed(self.seed)
        num_vectors, dimension = X.shape
        self.wcss_history = {} 
        
        if (dimension % self.m_subvectors) != 0:
            raise ValueError(
                f"Dimension {dimension} is not perfectly divisible by "
                f"m_subvectors {self.m_subvectors}."
            )
            
        sub_dim = dimension // self.m_subvectors
        print(f"Training PQ Codebooks: Splitting {dimension}-dim space into {self.m_subvectors} "
              f"subspaces (each of {sub_dim}-dim)...")

        # Initialize the codebooks structure
        self.codebooks = np.zeros((self.m_subvectors, self.k_subcentroids, sub_dim), dtype=np.float32)

        # Train K-Means independently for each subspace slice
        for m in range(self.m_subvectors):
            print(f"\n   --- Training Subspace {m + 1}/{self.m_subvectors} ---")
            self.wcss_history[m] = []
            
            # Slice out the m-th subvector space components for all items
            start_col = m * sub_dim
            end_col = start_col + sub_dim
            X_subspace = X[:, start_col:end_col]
            
            # Train and track this subspace
            sub_centroids = self._run_subspace_kmeans(X_subspace, m, sub_dim)
            self.codebooks[m] = sub_centroids

        print("\n[OK] PQ Codebooks successfully trained.")
        return self.codebooks

    def _run_subspace_kmeans(self, X_sub: np.ndarray, subspace_idx: int, sub_dim: int) -> np.ndarray:
        """Internal helper running mini-K-Means on a single subspace with WCSS logging."""
        num_vectors = X_sub.shape[0]
        
        # Initialize sub-centroids randomly
        initial_indices = np.random.choice(num_vectors, size=self.k_subcentroids, replace=False)
        sub_centroids = X_sub[initial_indices].copy()

        for iteration in range(self.max_iter):
            # Compute squared distances inside the subspace
            X_sq = np.sum(X_sub**2, axis=1, keepdims=True)
            c_sq = np.sum(sub_centroids**2, axis=1, keepdims=True).T
            distances_sq = np.clip(X_sq + c_sq - 2 * np.dot(X_sub, sub_centroids.T), a_min=0, a_max=None)
            
            labels = np.argmin(distances_sq, axis=1)

            # Compute WCSS for this iteration in this subspace
            min_distances_sq = distances_sq[np.arange(num_vectors), labels]
            current_wcss = float(np.sum(min_distances_sq))
            self.wcss_history[subspace_idx].append(current_wcss)

            # Periodic status logging (Log start, middle, and end to avoid excessive printing)
            if iteration == 0:
                print(f"      Iter {iteration + 1}/{self.max_iter} -> Subspace WCSS: {current_wcss:.2f}")
            elif (iteration + 1) % 10 == 0 or iteration == self.max_iter - 1:
                improvement = self.wcss_history[subspace_idx][-2] - current_wcss
                print(f"      Iter {iteration + 1}/{self.max_iter} -> Subspace WCSS: {current_wcss:.2f} (Delta: -{improvement:.2f})")

            # Centroid repositioning updates
            new_sub_centroids = np.zeros_like(sub_centroids)
            for i in range(self.k_subcentroids):
                assigned = X_sub[labels == i]
                if len(assigned) > 0:
                    new_sub_centroids[i] = np.mean(assigned, axis=0)
                else:
                    new_sub_centroids[i] = X_sub[np.random.choice(num_vectors)]

            if np.allclose(sub_centroids, new_sub_centroids, atol=1e-5):
                print(f"      [OK] Converged early at iteration {iteration + 1}. Final Subspace WCSS: {current_wcss:.2f}")
                break
                
            sub_centroids = new_sub_centroids

        return sub_centroids


    def get_global_pq_wcss(self) -> float:
        """Returns the combined final WCSS across all trained subspaces."""
        return sum(history[-1] for history in self.wcss_history.values())