import subprocess
import os
import json
import numpy as np

class FHESearcherWrapper:
    """
    Python wrapper that calls the compiled C++ FHE core binary to perform
    asymmetric distance computation (ADC) searches either in plaintext or CKKS ciphertext space.
    """
    def __init__(self, config_path: str):
        self.config_path = os.path.abspath(config_path)
        with open(self.config_path, 'r') as f:
            self.config = json.load(f)
            
        script_dir = os.path.dirname(os.path.abspath(__file__))
        self.bin_path = os.path.join(script_dir, "fhe_core", "build", "fhe_core_bin.exe")
        
        if not os.path.exists(self.bin_path):
            raise FileNotFoundError(
                f"FHE C++ core binary not found at: {self.bin_path}\n"
                f"Please compile the C++ core first."
            )

    def search(self, query_idx: int, top_k: int):
        """
        Runs search for a single query using its index in the query dataset.
        Returns:
            Tuple[np.ndarray, str]: (predicted_neighbor_ids, latency_report_string)
        """
        cmd = [self.bin_path, self.config_path, str(query_idx), str(top_k)]
        
        env = os.environ.copy()
        openfhe_lib = r"C:\\OpenFHE\\lib"
        env["PATH"] = openfhe_lib + os.pathsep + env.get("PATH", "")
        
        result = subprocess.run(cmd, capture_output=True, text=True, check=False, env=env)
        
        if result.returncode != 0:
            raise RuntimeError(
                f"C++ FHE Search execution failed with exit code {result.returncode}.\n"
                f"STDOUT:\n{result.stdout}\n"
                f"STDERR:\n{result.stderr}"
            )
            
        pred_ids = []
        latency_str = "N/A"
        results_found = False
        
        for line in result.stdout.strip().split('\n'):
            if line.startswith("RESULTS:"):
                results_found = True
                content = line[len("RESULTS:"):].strip()
                if content:
                    parts = content.split(',')
                    for part in parts:
                        idx_str, dist_str = part.split(':')
                        pred_ids.append(int(idx_str))
            elif line.startswith("LATENCY:"):
                latency_str = line[len("LATENCY:"):].strip()
                
        if not results_found:
            raise RuntimeError(f"FHE search did not return structured results. Output:\n{result.stdout}")
            
        return np.array(pred_ids, dtype=np.int32), latency_str

    def search_batch(self, top_k: int, jobs: int = 1, limit: int = -1):
        """
        Runs search for ALL queries in the query dataset using batch mode (query_idx = -1).
        Returns:
            Tuple[List[np.ndarray], float, float, float]: 
                (list_of_predicted_neighbor_ids, total_batch_latency_ms, avg_active_latency_ms, throughput_latency_ms)
        """
        cmd = [self.bin_path, self.config_path, "-1", str(top_k)]
        if jobs > 1:
            cmd.extend(["-j", str(jobs)])
        if limit > 0:
            cmd.extend(["-n", str(limit)])
        
        env = os.environ.copy()
        openfhe_lib = r"C:\\OpenFHE\\lib"
        env["PATH"] = openfhe_lib + os.pathsep + env.get("PATH", "")
        
        result = subprocess.run(cmd, capture_output=True, text=True, check=False, env=env)
        
        if result.returncode != 0:
            raise RuntimeError(
                f"C++ FHE Search execution failed with exit code {result.returncode}.\n"
                f"STDOUT:\n{result.stdout}\n"
                f"STDERR:\n{result.stderr}"
            )
            
        all_pred_ids = {}
        batch_latency = 0.0
        avg_active_latency = 0.0
        throughput_latency = 0.0
        
        for line in result.stdout.strip().split('\n'):
            if line.startswith("RESULTS_"):
                # Format: RESULTS_0: 123:0.45,456:0.78
                prefix, content = line.split(':', 1)
                q_idx = int(prefix.split('_')[1])
                pred_ids = []
                content = content.strip()
                if content:
                    parts = content.split(',')
                    for part in parts:
                        idx_str, dist_str = part.split(':')
                        pred_ids.append(int(idx_str))
                all_pred_ids[q_idx] = np.array(pred_ids, dtype=np.int32)
            elif line.startswith("BATCH_LATENCY:"):
                part = line[len("BATCH_LATENCY:"):].replace("ms", "").strip()
                batch_latency = float(part)
            elif line.startswith("AVG_ACTIVE_LATENCY:"):
                part = line[len("AVG_ACTIVE_LATENCY:"):].replace("ms", "").strip()
                avg_active_latency = float(part)
            elif line.startswith("THROUGHPUT_LATENCY:"):
                part = line[len("THROUGHPUT_LATENCY:"):].replace("ms", "").strip()
                throughput_latency = float(part)
                
        # Sort by query index to return in order
        ordered_pred_ids = [all_pred_ids[i] for i in sorted(all_pred_ids.keys())]
        return ordered_pred_ids, batch_latency, avg_active_latency, throughput_latency

