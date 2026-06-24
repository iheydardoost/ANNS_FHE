import numpy as np
from typing import List
from config_manager import IVFConfig

class IVFCoarseQuantizer:
    """
    Handles the generation of global centroids via K-Means clustering 
    to partition the vector space for the Inverted File (IVF) index.
    """
    def __init__(self, config: IVFConfig):
        self.n_list = config.n_list
        self.max_iter = config.max_iter
        self.seed = config.seed
        self.tolerance = config.tolerance
        self.centroids = None
        self.wcss_history: List[float] = []  # Stores WCSS value per iteration


    def fit(self, X: np.ndarray) -> np.ndarray:
        """
        Computes the IVF coarse centroids using K-Means clustering.
        
        Args:
            X (np.ndarray): The input dataset of shape (N, dimension).
            
        Returns:
            np.ndarray: Computed centroids of shape (n_list, dimension).
        """
        np.random.seed(self.seed)
        num_vectors, dimension = X.shape
        self.wcss_history = []  # Reset history on new fit
        
        if num_vectors < self.n_list:
            raise ValueError("Number of dataset vectors must be greater than n_list cluster centers.")

        print(f"Training IVF Coarse Quantizer: Clustering {num_vectors} vectors into {self.n_list} regions...")

        # Initialization: Pick random vectors as initial cluster centers (K-Means++)
        initial_indices = np.random.choice(num_vectors, size=self.n_list, replace=False)
        self.centroids = X[initial_indices].copy()

        for iteration in range(self.max_iter):
            # Assignment Step: Compute Euclidean distances from every vector to every centroid
            # Using the identity: ||a - b||^2 = ||a||^2 + ||b||^2 - 2<a, b>
            X_sq = np.sum(X**2, axis=1, keepdims=True)                  # Shape: (N, 1)
            c_sq = np.sum(self.centroids**2, axis=1, keepdims=True).T   # Shape: (1, n_list)
            # Clipping to 0 to prevent tiny negative floating-point artifacts
            distances_sq = np.clip(X_sq + c_sq - 2 * np.dot(X, self.centroids.T), a_min=0, a_max=None) # Shape: (N, n_list)
            
            # Label each vector based on the minimum distance
            labels = np.argmin(distances_sq, axis=1)

            # Evaluation: Compute WCSS (Inertia) for the current assignment
            min_distances_sq = distances_sq[np.arange(num_vectors), labels]
            current_wcss = float(np.sum(min_distances_sq))
            self.wcss_history.append(current_wcss)

            # Print tracking update
            if iteration > 0:
                improvement = self.wcss_history[-2] - current_wcss
                print(f"   Iteration {iteration + 1}/{self.max_iter} -> WCSS: {current_wcss:.2f} (Dropped by: {improvement:.2f})")
            else:
                print(f"   Iteration {iteration + 1}/{self.max_iter} -> WCSS: {current_wcss:.2f}")

            # Update Step: Recalculate centroids as the mean of assigned vectors
            new_centroids = np.zeros_like(self.centroids)
            for i in range(self.n_list):
                assigned_vectors = X[labels == i]
                if len(assigned_vectors) > 0:
                    new_centroids[i] = np.mean(assigned_vectors, axis=0)
                else:
                    new_centroids[i] = X[np.random.choice(num_vectors)]

            # Check for convergence (if centroids stop moving)
            if np.allclose(self.centroids, new_centroids, atol=self.tolerance):
                print(f"✓ K-Means converged early at iteration {iteration + 1}.")
                break
                
            self.centroids = new_centroids

        print("✓ IVF Centroids successfully generated.")
        return self.centroids


    def get_wcss_history(self) -> List[float]:
        """Returns the WCSS metric history tracked across iterations."""
        return self.wcss_history