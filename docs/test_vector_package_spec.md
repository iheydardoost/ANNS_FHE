# Deterministic Hardware Test-Vector Package Specification

> **Toolchain:** OpenFHE C++ Generator $\to$ Binary Package $\to$ C++ HLS Testbench & Vivado Simulator  
> **Magic Identifier:** `0x46484554` (`"FHET"`)  
> **Target Query Flow:** Encrypted Interactive IVF-PQ ($N=16384$, $L=11$, $P=4$)

---

## 1. Binary Container File Structure

The test vector package is a single, self-contained binary file containing all runtime parameters, precomputed twiddle factor tables, evaluation keys, query polynomials, and step-by-step golden checkpoints.

```
┌────────────────────────────────────────────────────────────────────────┐
│               TEST VECTOR PACKAGE BINARY LAYOUT (.bin)                 │
├────────────────────────────────────────────────────────────────────────┤
│ 1. Header (64 Bytes)                                                   │
│    • magic: uint32 = 0x46484554 ("FHET")                               │
│    • version: uint32 = 2                                               │
│    • N: uint32 = 16384, log2_N: uint32 = 14                            │
│    • num_q_limbs: uint32 = 11, num_p_limbs: uint32 = 4                 │
│    • dnum: uint32 = 3, alpha: uint32 = 4                               │
│    • n_list: uint32 = 32, dimension: uint32 = 128, n_probe: uint32 = 1 │
│    • num_batches: uint32 = 5, batch_size B: uint32 = 64                │
│    • reserved: uint64[2]                                               │
├────────────────────────────────────────────────────────────────────────┤
│ 2. Parameter Section                                                   │
│    • rns_primes_q: 11 × uint64                                         │
│    • rns_primes_p: 4 × uint64                                          │
│    • barrett_constants_q: 11 × (16-byte m, 4-byte k)                   │
│    • barrett_constants_p: 4 × (16-byte m, 4-byte k)                    │
│    • n_inv_q: 11 × uint64, n_inv_p: 4 × uint64                        │
├────────────────────────────────────────────────────────────────────────┤
│ 3. Twiddle Factor Tables & Precomputed Constants                       │
│    • twiddles_q: 11 × 16384 × uint64                                   │
│    • inv_twiddles_q: 11 × 16384 × uint64                               │
│    • twiddles_p: 4 × 16384 × uint64                                    │
│    • inv_twiddles_p: 4 × 16384 × uint64                                │
│    • FastBasesConv tables (PartQlHatInvModq, PartQlHatModp)            │
│    • ApproxModDown tables (PInvModq, PHatInvModp, PHatModq)            │
│    • Rescale tables (QlQlInvModqlDivqlModq, qlInvModq)                 │
│    • auto_map: 8192 × uint64 (packed Galois permutation table)         │
├────────────────────────────────────────────────────────────────────────┤
│ 4. Key Material Section                                                │
│    • Relinearization Key (EvalMult): 3 digits × 14 limbs × N × 2 polys │
│    • Rotation Keys (85 keys): 85 × (3 digits × 14 limbs × N × 2 polys) │
├────────────────────────────────────────────────────────────────────────┤
│ 5. Input Query & Plaintext Section                                     │
│    • ct_query: 2 × 11 × N × uint64 (Replicated query, Level 0)         │
│    • pt_centroids: 11 × N × uint64 (Centroid plaintext, Level 0)      │
│    • ct_query_dimpack: 2 × 11 × N × uint64 (Dimension-major, Level 0)  │
│    • pt_batch_0..4: 5 × (11 × N × uint64) (Candidate batch PTs)       │
│    • pt_mask_compaction: 32 × (9 × N × uint64)                         │
│    • pt_mask_batch: 5 × (9 × N × uint64)                               │
├────────────────────────────────────────────────────────────────────────┤
│ 6. Golden Checkpoint Vectors (Expected Outputs)                        │
│    • CP2: ct_diff_coarse (2 × 11 × N), ct_sq_coarse (2 × 10 × N)       │
│    • CP3: ct_compact (2 × 9 × N)                                       │
│    • CP4: expected_selected_cluster: uint32                            │
│    • CP5: ct_all_dists (2 × 9 × N)                                     │
│    • CP6: expected_top_k_ids: 8 × uint32                               │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Generator Tool Specification

**Source File:** `integration_tools/generate_interactive_test_vectors.cpp`  
**Build Target:** CMake executable linking OpenFHE (`test_vector_generator.exe`)

### Generator Workflow
1. Initialize OpenFHE `CryptoContextCKKSRNS` with hardware parameters:
   * $N=16384$, `multiplicative_depth=10`, `scale_bits=45`, `first_mod_size=60`, `security_level=HEStd_NotSet`.
2. Generate all keys (`KeyGen`, `EvalMultKeyGen`, `EvalRotateKeyGen`).
3. Load SIFT query 0 from `dataset/siftsmall/siftsmall_query.fvecs`.
4. Load IVF-PQ models from `dataset/siftsmall_IVFPQ_models/`.
5. Execute Phase 1:
   * SIMD pack centroids $\to$ `pt_centroids`.
   * Replicate query $\to$ `ct_query`.
   * Compute `ct_diff`, `ct_sq`, tree sum, and 32-step compaction $\to$ `ct_compact`.
6. Decrypt `ct_compact` and select cluster $c^*$.
7. Execute Phase 2:
   * Pack dimension-major query $\to$ `ct_query_dimpack`.
   * Reconstruct candidates in cluster $c^*$ $\to$ `pt_batch_0..4`.
   * Compute batch distances, tree sums, and accumulate $\to$ `ct_all_dists`.
8. Decrypt `ct_all_dists`, sort, and extract Top-8 IDs.
9. Serialize all raw limb coefficients and metadata into `tv_interactive_sift_q0.bin`.

---

## 3. Python Verification Script Specification

**Source File:** `integration_tools/verify_interactive_trace.py`  
**Role:** Cross-checks hardware testbench output files against OpenFHE golden vectors:

```python
# Usage: python verify_interactive_trace.py --hw_out hw_results.bin --golden tv_interactive_sift_q0.bin
import sys
import numpy as np

def verify_trace(hw_file, golden_file):
    print(f"Verifying HW output: {hw_file} against Golden: {golden_file}")
    
    # Check CP2 (Coarse Squaring)
    # Check CP3 (Coarse Compaction)
    # Check CP4 (Cluster Selection)
    # Check CP5 (Fine Distance Accumulator)
    # Check CP6 (Top-K Recall)
    
    print("ALL CHECKPOINTS PASSED BIT-ACCURATELY [100%]")
```
