import subprocess
import os
import json
import numpy as np


class FHESearcherWrapper:
    """
    Python wrapper that calls the compiled C++ FHE core binary to perform
    IVF-PQ ANNS searches, either in plaintext or CKKS encrypted mode.

    Subcommand interface:
        fhe_core_bin preprocess <config.json>
        fhe_core_bin search <config.json> [query_idx] [top_k]
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

        # Common environment for all subprocess calls
        self.env = os.environ.copy()
        openfhe_lib = r"C:\\OpenFHE\\lib"
        self.env["PATH"] = openfhe_lib + os.pathsep + self.env.get("PATH", "")

    # ---------------------------------------------------------------------------
    # Offline preprocessing: keygen + encrypt centroids/codebooks + serialize
    # ---------------------------------------------------------------------------
    def preprocess(self) -> str:
        """
        Run offline preprocessing: generate keys, encrypt centroids and codebooks,
        serialize everything to the serialization_dir specified in config.

        Returns:
            str: stdout from the C++ binary (informational messages).
        Raises:
            RuntimeError: if the binary exits with a non-zero code.
        """
        cmd = [self.bin_path, "preprocess", self.config_path]
        result = subprocess.run(
            cmd, capture_output=True, text=True, check=False, env=self.env)

        if result.returncode != 0:
            raise RuntimeError(
                f"Preprocessing failed (exit code {result.returncode}).\n"
                f"STDOUT:\n{result.stdout}\n"
                f"STDERR:\n{result.stderr}"
            )
        return result.stdout

    # ---------------------------------------------------------------------------
    # Online search: single query
    # ---------------------------------------------------------------------------
    def search(self, query_idx: int, top_k: int):
        """
        Runs encrypted (or plaintext) search for a single query.

        Args:
            query_idx: Index of the query in the query dataset.
            top_k:     Number of nearest neighbors to return.

        Returns:
            Tuple[np.ndarray, str]: (predicted_neighbor_ids, latency_report_string)
        """
        cmd = [self.bin_path, "search", self.config_path, str(query_idx), str(top_k)]
        result = subprocess.run(
            cmd, capture_output=True, text=True, check=False, env=self.env)

        if result.returncode != 0:
            raise RuntimeError(
                f"C++ FHE Search failed (exit code {result.returncode}).\n"
                f"STDOUT:\n{result.stdout}\n"
                f"STDERR:\n{result.stderr}"
            )

        pred_ids = []
        latency_str = "N/A"
        results_found = False

        for line in result.stdout.strip().split('\n'):
            line = line.strip()
            # Format: "RESULTS <query_idx>: <id0> <id1> ..."
            if line.startswith("RESULTS"):
                results_found = True
                parts = line.split(':', 1)
                if len(parts) > 1:
                    content = parts[1].strip()
                    if content:
                        pred_ids = [int(x) for x in content.split()]
            # Format: "LATENCY <query_idx>: coarse=...ms fine=...ms total=...ms"
            elif line.startswith("LATENCY"):
                latency_str = line

        if not results_found:
            raise RuntimeError(
                f"FHE search did not return structured results.\nOutput:\n{result.stdout}"
            )

        return np.array(pred_ids, dtype=np.int32), latency_str

    # ---------------------------------------------------------------------------
    # Online search: batch (all queries)
    # ---------------------------------------------------------------------------
    def search_batch(self, top_k: int, jobs: int = 1, limit: int = -1):
        """
        Runs search for ALL queries in the query dataset (or up to 'limit' queries).

        Args:
            top_k:  Number of nearest neighbors per query.
            jobs:   Unused (kept for API compatibility; parallelism handled by C++).
            limit:  If > 0, run only the first 'limit' queries.

        Returns:
            Tuple[List[np.ndarray], float, float, float]:
                (list_of_predicted_neighbor_ids,
                 total_batch_latency_ms,
                 avg_active_latency_ms,
                 throughput_qps)
        """
        # query_idx = -1 → batch mode in C++
        cmd = [self.bin_path, "search", self.config_path, "-1", str(top_k)]
        if jobs > 1:
            cmd.extend(["-j", str(jobs)])
        if limit > 0:
            cmd.extend(["-n", str(limit)])

        result = subprocess.run(
            cmd, capture_output=True, text=True, check=False, env=self.env)

        if result.returncode != 0:
            raise RuntimeError(
                f"C++ FHE batch search failed (exit code {result.returncode}).\n"
                f"STDOUT:\n{result.stdout}\n"
                f"STDERR:\n{result.stderr}"
            )

        all_pred_ids: dict[int, np.ndarray] = {}
        batch_latency        = 0.0
        avg_active_latency   = 0.0
        throughput_latency   = 0.0

        for line in result.stdout.strip().split('\n'):
            line = line.strip()
            # Single-query results: "RESULTS <qi>: <id0> <id1> ..."
            if line.startswith("RESULTS"):
                parts = line.split(':', 1)
                if len(parts) > 1:
                    # Extract query index from "RESULTS <qi>"
                    header = parts[0].split()
                    q_idx = int(header[1]) if len(header) > 1 else 0
                    content = parts[1].strip()
                    pred_ids = [int(x) for x in content.split()] if content else []
                    all_pred_ids[q_idx] = np.array(pred_ids, dtype=np.int32)
            # Timing summary lines from the statistics block
            elif "Wall-clock:" in line:
                try:
                    batch_latency = float(line.split(':')[1].replace('ms', '').strip())
                except (ValueError, IndexError):
                    pass
            elif "Avg latency:" in line:
                try:
                    avg_active_latency = float(line.split(':')[1].replace('ms', '').strip())
                except (ValueError, IndexError):
                    pass
            elif "Throughput:" in line:
                try:
                    throughput_latency = float(line.split(':')[1].replace('queries/sec', '').strip())
                except (ValueError, IndexError):
                    pass

        # Return in sorted query-index order
        ordered_pred_ids = [all_pred_ids[i] for i in sorted(all_pred_ids.keys())]
        return ordered_pred_ids, batch_latency, avg_active_latency, throughput_latency
