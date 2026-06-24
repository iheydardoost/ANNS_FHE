import os
import numpy as np

class SiftDataLoader:
    
    def __init__(self, file_path: str, expected_dim: int):
        self.file_path = file_path
        self.expected_dim = expected_dim


    def load_dataset(self) -> np.ndarray:
        """
        Parses the .fvecs file and returns a NumPy array of shape (N, dimension).
        
        Returns:
            np.ndarray: A 2D array of float32 values.
        """
        if not os.path.exists(self.file_path):
            raise FileNotFoundError(f"Dataset file not found at: {self.file_path}")

        print(f"Reading dataset from {self.file_path}...")
        
        try:
            with open(self.file_path, 'rb') as f:
                # Read everything into a single 1D numpy array first
                raw_data = np.fromfile(f, dtype=np.float32)
                
            if raw_data.size == 0:
                raise ValueError("The dataset file is empty.")

            # In .fvecs, the first element is an int32 representing the dimension.
            # Because it's read as float32, we cast the first element's bit pattern back to int.
            detected_dim = raw_data[0].view(np.int32)
            
            if detected_dim != self.expected_dim:
                raise ValueError(
                    f"Dimension mismatch! Expected {self.expected_dim}, "
                    f"but detected {detected_dim} in the file."
                )

            # Each vector record takes up (1 + dimension) float32 slots
            record_length = 1 + detected_dim
            num_vectors = raw_data.size // record_length
            
            # Reshape and strip out the leading dimension integers
            dataset = raw_data.reshape(num_vectors, record_length)[:, 1:]
            
            print(f"✓ Successfully loaded {num_vectors} vectors with dimension {detected_dim}.")
            return dataset

        except Exception as e:
            raise IOError(f"Failed to parse .fvecs file: {str(e)}")


    def load_groundtruth(self, file_path: str) -> np.ndarray:
        """
        Parses the .ivecs file and returns a NumPy array of shape (N, dimension).
        
        Returns:
            np.ndarray: A 2D array of int32 values.
        """
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"Groundtruth file not found at: {file_path}")

        print(f"Reading groundtruth from {file_path}...")
        
        try:
            with open(file_path, 'rb') as f:
                # Read everything into a single 1D numpy array first
                raw_data = np.fromfile(f, dtype=np.int32)
                
            if raw_data.size == 0:
                raise ValueError("The groundtruth file is empty.")

            # In .ivecs, the first element is an int32 representing the dimension.
            detected_dim = raw_data[0]
            
            # Each vector record takes up (1 + dimension) int32 slots
            record_length = 1 + detected_dim
            num_vectors = raw_data.size // record_length
            
            # Reshape and strip out the leading dimension integers
            dataset = raw_data.reshape(num_vectors, record_length)[:, 1:]
            
            print(f"✓ Successfully loaded {num_vectors} groundtruth vectors with dimension {detected_dim}.")
            return dataset

        except Exception as e:
            raise IOError(f"Failed to parse .ivecs file: {str(e)}")