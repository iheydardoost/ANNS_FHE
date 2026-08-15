# Data Format Specification

> Hardware representation of CKKS ciphertexts, plaintexts, and IVF-PQ index structures
> for the ANNS_FHE accelerator.

---

## 1. Hardware Word Definition

| Property | Value |
|----------|-------|
| **One hardware word** | 64-bit unsigned integer |
| **Represents** | One RNS residue of one polynomial coefficient |
| **Notation** | $a_j^{(i)}$ = coefficient $j$ modulo prime $q_i$ |
| **Byte order** | Little-endian (matches x86 host and OpenFHE serialization) |
| **Alignment** | 8-byte aligned |

Every arithmetic operation in the accelerator operates on 64-bit words.
A 64-bit modular multiply produces a 128-bit intermediate that is reduced
back to 64 bits via Barrett reduction.

---

## 2. RNS Prime Chain

CKKS uses the Residue Number System (RNS) to represent large integers
as tuples of small residues. The modulus chain is:

$$Q = q_0 \cdot q_1 \cdot q_2 \cdots q_L$$

| Index | Role | Bit-width | Notes |
|-------|------|-----------|-------|
| $q_0$ | First/special modulus | 60 bits | Hardcoded in `fhe_context_manager.cpp` |
| $q_1 \ldots q_L$ | Scaling moduli | 45 bits each | `scale_bits = 45` from config.json |

### Parameter Configurations

| Config | $N$ | Slots | $L+1$ primes | mult_depth | Poly size | CT size |
|--------|-----|-------|--------------|------------|-----------|---------|
| **Software (config.json)** | 65536 | 32768 | 36 | 35 | 18.0 MB | 36.0 MB |
| **HW Prototype (recommended)** | 16384 | 8192 | 11 | 10 | 1.4 MB | 2.8 MB |
| **HW Minimal (debug)** | 4096 | 2048 | 6 | 5 | 192 KB | 384 KB |

---

## 3. Polynomial Memory Layout

A polynomial $a(X) \in R_{Q_\ell} = \mathbb{Z}_{Q_\ell}[X]/(X^N+1)$ at level $\ell$
has $N$ coefficients and $L_\ell = L+1-\ell$ active RNS limbs.

### 3.1 Limb-Major Layout (Primary)

Memory is organized **limb-major**: all $N$ coefficients of limb 0, then limb 1, etc.
This matches OpenFHE's internal `DCRTPoly` layout and enables efficient per-limb NTT.

```
Base Address: POLY_BASE

Offset (bytes)                          Content
─────────────────────────────────────────────────────
0                                       a_0^(0)     ← coeff 0, limb 0
8                                       a_1^(0)     ← coeff 1, limb 0
...
(N-1) × 8                              a_{N-1}^(0) ← coeff N-1, limb 0
N × 8                                  a_0^(1)     ← coeff 0, limb 1
(N+1) × 8                              a_1^(1)     ← coeff 1, limb 1
...
(i × N + j) × 8                        a_j^(i)     ← coeff j, limb i
...
(L_ℓ × N - 1) × 8                     a_{N-1}^(L_ℓ-1)
─────────────────────────────────────────────────────
Total: L_ℓ × N × 8 bytes
```

### 3.2 Address Calculation

```
word_addr(limb_i, coeff_j) = POLY_BASE + (limb_i * N + coeff_j) * 8
```

### 3.3 NTT Domain vs Coefficient Domain

| Domain | Storage | When used |
|--------|---------|-----------|
| **NTT (evaluation)** | Same layout, coefficients are NTT-transformed | Default for multiply |
| **Coefficient** | Raw polynomial coefficients | Required for automorphism |

A 1-bit domain tag per polynomial tracks the current representation.

---

## 4. Ciphertext Memory Layout

A CKKS ciphertext $\text{ct} = (c_0, c_1)$ consists of **two polynomials**:

```
Base Address: CT_BASE

Polynomial c_0:  CT_BASE
Polynomial c_1:  CT_BASE + L_ℓ × N × 8

Total: 2 × L_ℓ × N × 8 bytes
```

### 4.1 Ciphertext Metadata (stored in control registers or header)

```c
struct ct_metadata {
    uint32_t level;           // Current level ℓ (0 = freshest)
    uint32_t num_limbs;       // L_ℓ = L + 1 - level
    uint32_t noise_scale_deg; // CKKS noise scaling degree
    uint32_t poly_degree;     // N
    uint64_t scaling_factor;  // Δ = 2^scale_bits at this level
};
```

### 4.2 Size Table

| Configuration | $N$ | $L_\ell$ | c_0 size | c_1 size | CT total |
|--------------|------|---------|----------|----------|----------|
| HW Proto (fresh, ℓ=0) | 16384 | 11 | 1.375 MB | 1.375 MB | 2.75 MB |
| HW Proto (after 1 rescale, ℓ=1) | 16384 | 10 | 1.25 MB | 1.25 MB | 2.5 MB |
| HW Proto (after 3 rescales, ℓ=3) | 16384 | 8 | 1.0 MB | 1.0 MB | 2.0 MB |

---

## 5. Plaintext Memory Layout

A CKKS plaintext is a **single polynomial** at a specific level.
Used for: encoded centroids, codebooks, masks.

```
Size: L_ℓ × N × 8 bytes (half of a ciphertext)
```

Plaintexts are prepared by the host CPU via OpenFHE's `MakeCKKSPackedPlaintext`
and transferred to the accelerator as pre-encoded polynomials.

---

## 6. Evaluation Key Layout

For HYBRID key switching with decomposition number $d_{\text{num}}$:

Each evaluation key component is a pair of polynomials at the full level.
Total components per key: $d_{\text{num}}$ pairs.

```
evk[d] = (evk[d].b, evk[d].a)   for d = 0, ..., d_num - 1

Each evk[d].b and evk[d].a: N × (L+1) × 8 bytes
Total per eval key: 2 × d_num × N × (L+1) × 8 bytes
```

| Config | $d_{\text{num}}$ | Per-key size | Relin key | Per rotation key | Total (85 keys) |
|--------|---------|-------------|-----------|-----------------|-----------------|
| HW Proto ($N$=16384, $L$=11) | ~3 | ~8.25 MB | ~8.25 MB | ~8.25 MB | ~700 MB |

> [!WARNING]
> Evaluation keys are too large for on-chip storage. They must be streamed
> from host DDR/HBM during key-switch operations.

---

## 7. CKKS Slot Packing Layouts

The relationship between CKKS "slots" and polynomial coefficients involves
a DFT-like encoding. The hardware accelerator does NOT perform encoding/decoding
— it operates on already-encoded polynomials. However, understanding the slot
layout is essential for verifying correctness.

### 7.1 Coarse Centroid / Replicated Query Layout

```
Slot index:  [0]  [1]  ...  [127]  [128]  [129]  ...  [255]  ...  [4095]  [4096] ... [S-1]
Content:     c0_d0 c0_d1 ... c0_d127 c1_d0  c1_d1  ... c1_d127 ... c31_d127  0    ...  0

             ├── centroid 0 ──────┤ ├── centroid 1 ──────┤       ├─ c31 ─┤ ├─ padding ─┤
```

- $n_{\text{list}} \times D = 32 \times 128 = 4096$ active slots
- Query is packed identically: 32 copies of the 128-dim query vector
- Slots = $N/2$: 32768 (software) or 8192 (HW prototype)

### 7.2 Dimension-Major Packing (Fine Distance)

```
B = Slots / D = number of candidates per batch

Slot index:  [0]    [1]    ... [B-1]   [B]    [B+1]  ... [2B-1]  ...
Content:     x0[0]  x1[0]  ... xB-1[0] x0[1]  x1[1]  ... xB-1[1] ...
             ├─ dim 0, all B cands ────┤├─ dim 1, all B cands ────┤

B = 256 (N=65536) or 64 (N=16384)
```

### 7.3 Compact Distance Layout

```
Slot index:  [0]     [1]     ... [31]    [32] ... [S-1]
Content:     dist_0  dist_1  ... dist_31  0   ...  0

             ├── 32 coarse distances ──┤ ├── padding ──┤
```

---

## 8. IVF-PQ Index Data (Plaintext, Server-Side)

These are NOT encrypted. They reside in host memory and are used to
construct plaintext polynomials for homomorphic operations.

| Structure | Shape | Element Type | Size | Usage |
|-----------|-------|-------------|------|-------|
| `ivf_centroids` | (32, 128) | float32 | 16 KB | Encoded into plaintext for coarse distance |
| `pq_codebooks` | (8, 256, 16) | float32 | 128 KB | Used to reconstruct candidate vectors |
| `ivf_assignments` | (10000,) | int32 | 40 KB | Maps vector ID → cluster ID |
| `pq_codes` | (10000, 8) | uint8 | 80 KB | Maps vector ID → PQ code per subspace |
| `cluster_vector_ids` | jagged[32] | int32 | ~40 KB | Inverted list: cluster → vector IDs |

---

## 9. Host-to-Accelerator Data Transfer Format

### 9.1 Polynomial Transfer

Polynomials are transferred as contiguous blocks of 64-bit words in
limb-major order via AXI4 burst transfers:

```
Transfer order: limb 0 coefficients [0..N-1], limb 1 coefficients [0..N-1], ...
Word width: 64 bits (single word) or 512 bits (AXI burst, 8 words per beat)
```

### 9.2 Ciphertext Transfer

```
1. Transfer c_0 polynomial (L_ℓ × N words)
2. Transfer c_1 polynomial (L_ℓ × N words)
3. Transfer metadata (level, num_limbs, noise_scale_deg)
```

### 9.3 Evaluation Key Transfer (Streaming)

During key-switch operations, evaluation key components are streamed
from host memory on demand:

```
For each digit d in [0, d_num):
    Stream evk[d].b polynomial (L × N words)
    Stream evk[d].a polynomial (L × N words)
    Compute partial inner product immediately
    Discard evk[d] (no on-chip storage needed)
```

### 9.4 Command Transfer

Operations are dispatched via AXI4-Lite register writes:

```
Register         Width    Description
────────────────────────────────────────────
OP_CODE          8 bits   Operation type
SRC_ADDR_A       32 bits  On-chip address of operand A
SRC_ADDR_B       32 bits  On-chip address of operand B
DST_ADDR         32 bits  On-chip address of result
PRIME_IDX        8 bits   RNS prime index (for per-limb ops)
NUM_LIMBS        8 bits   Number of active RNS limbs
GALOIS_ELT       32 bits  Galois element (for automorphism)
STATUS           8 bits   0=idle, 1=busy, 2=done, 3=error
```

---

## 10. Test Vector File Format

Binary files for verification, generated by the test vector generator:

```
File header (32 bytes):
    magic:      uint32  = 0x46484554 ("FHET")
    version:    uint32  = 1
    N:          uint32  = polynomial degree
    num_limbs:  uint32  = number of RNS limbs
    num_vectors:uint32  = number of test vectors in file
    reserved:   uint8[12]

Per test vector:
    input_a:    uint64[N]     (one limb of polynomial A)
    input_b:    uint64[N]     (one limb of polynomial B, if applicable)
    expected:   uint64[N]     (expected output, one limb)
    prime:      uint64        (the RNS prime q_i used)
```

For multi-limb operations, vectors are stored limb-by-limb.

---

## 11. Summary: Software-to-Hardware Data Mapping

| Software Concept | Hardware Representation | Storage Location |
|-----------------|----------------------|-----------------|
| CKKS slot value (float64) | N/A — encoding done on CPU | Host only |
| Polynomial coefficient (big integer) | $L$ × 64-bit RNS residues | URAM |
| Ciphertext $(c_0, c_1)$ | $2 \times L \times N$ words | URAM |
| Plaintext (encoded) | $L \times N$ words | URAM (loaded from host) |
| Evaluation key | $2 \times d_{\text{num}} \times L \times N$ words per key | DDR/HBM (streamed) |
| NTT twiddle factor | 64-bit word per coefficient per prime | BRAM ROM |
| Barrett constant | 128-bit $(m, k)$ pair per prime | Registers |
| RNS prime $q_i$ | 64-bit word | Registers |
| IVF centroids | float32 array | Host memory (encoded to polynomial before transfer) |
| PQ codebooks | float32 array | Host memory (encoded to polynomial before transfer) |
