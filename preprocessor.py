import numpy as np
from config_manager import IVFPQConfigManager
from ivf_quantizer import IVFCoarseQuantizer
from product_quantizer import ProductQuantizer

class IVFPQPreprocessor:
    """
    Orchestrates the entire IVF-PQ preprocessing phase:
    1. Fits the IVF Coarse Quantizer (K-Means) to find global centroids.
    2. Computes residual vectors.
    3. Fits the Product Quantizer (PQ) on those residuals to generate codebooks.
    """
    def __init__(self, config_manager: IVFPQConfigManager):
        self.config = config_manager
        self.ivf_quantizer = IVFCoarseQuantizer(self.config.ivf)
        self.product_quantizer = ProductQuantizer(self.config.pq)
        
        # Artifacts generated during preprocessing
        self.ivf_centroids = None
        self.pq_codebooks = None


    def run_preprocessing(self, X: np.ndarray) -> tuple:
        """
        Executes the dual-stage quantization training pipeline.
        
        Args:
            X (np.ndarray): The raw dataset of shape (N, dimension).
            
        Returns:
            tuple: (ivf_centroids, pq_codebooks)
        """
        print("=== STARTING IVF-PQ PREPROCESSING PHASE ===\n")
        
        # Compute global IVF coarse centroids
        self.ivf_centroids = self.ivf_quantizer.fit(X)
        print("")

        # Compute Residuals
        # To do this efficiently, we find the closest IVF centroid for every vector in X
        print("Computing residual vectors for PQ training...")
        X_sq = np.sum(X**2, axis=1, keepdims=True)
        c_sq = np.sum(self.ivf_centroids**2, axis=1, keepdims=True).T
        distances_sq = np.clip(X_sq + c_sq - 2 * np.dot(X, self.ivf_centroids.T), a_min=0, a_max=None)
        
        # Get closest cluster index for each vector
        closest_centroid_ids = np.argmin(distances_sq, axis=1)
        
        # Subtract assigned centroid from each vector
        residuals = X - self.ivf_centroids[closest_centroid_ids]
        print("✓ Residual vectors successfully calculated.")
        print("")

        # Train PQ codebooks strictly on these computed residuals
        self.pq_codebooks = self.product_quantizer.fit(residuals)
        
        print("\n=== IVF-PQ PREPROCESSING PHASE COMPLETE ===")
        return self.ivf_centroids, self.pq_codebooks