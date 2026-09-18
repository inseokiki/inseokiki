# Independent reference vectors

The committed headers let the C tests run without Python dependencies.
Generators must not import or invoke the simulator to calculate expected data.
These cases cross-check selected behavior, not complete standards conformance.

## MU-MIMO

`generate_mumimo.py` uses NumPy `linalg.pinv` (SVD) followed by per-column
normalization to total column power 1/4. The C implementation instead inverts
H H^H. Three fixed complex 4x4 channels cover differing phases, row order,
power imbalance and moderate conditioning (condition numbers about 2.45,
4.21 and 36.30). Absolute complex coefficient tolerance: 1e-10.
The generation NumPy version is recorded in the header. Reproduce with:

```bash
python3 PHY/tests/reference_vectors/generate_mumimo.py
```

Algorithm API: https://numpy.org/doc/stable/reference/generated/numpy.linalg.pinv.html

## Polar and LDPC

`generate_codecs.py` requires py3gpp 0.6.0; the manifest records dependency
versions and hashes of installed reference Python/table sources and outputs.
Regenerate in a separate virtualenv (`pip install py3gpp==0.6.0`). No simulator
code or tables are used to compute expected bits.

Polar: four N=128/512/1024 cases, input interleaving on/off, E>=N, n_PC=0.
Checks mother-code bits, subblock interleaving/repetition, and SC decoding of
external bits. Puncturing, shortening and UCI parity-check bits remain outside
this external coverage because this py3gpp encoder/rate matcher lacks those
matching features.

LDPC: BG1 and BG2 each cover all eight lifting sets, with seven filler bits.
Both py3gpp encoders (sionna and thangaraj) must agree and reference parity
checks must pass. BG2 uses K>=720 to avoid the known small-K issue:
https://github.com/catkira/py3gpp/issues/78
The C full mother code is compared after removing its first 2Z bits; external
-1 filler markers are converted to zero for codeword comparison. Decoding uses
external punctured bits (first 2Z unknown), known-zero fillers and finite ±30
LLRs. This does not externally validate TB CRC selection, BG/Z selection,
segmentation. LDPC modulation-bit interleaving/rate matching is covered by
the separate SCH vectors below.

During generation on NumPy 2.0.2/macOS, py3gpp's Sionna permutation-matrix
matmul emitted floating-point RuntimeWarnings. Outputs remained valid bits,
matched the separate Thangaraj encoder exactly, passed reference parity
checks, and matched C. Warnings are not suppressed by the generator; reproduce
on another numerical backend if these cases are to become conformance evidence.

Source: https://github.com/catkira/py3gpp/tree/v0.6.0

## SCH rate-matching diagnostic

`audit_ldpc_rate_matching.py` compiles the real C functions into a temporary
library. It records the legacy call convention's mismatches and requires the
corrected SCH API to match py3gpp in 64 cases (BG1/Z8, BG2/Z72).
See `docs/analysis/ldpc_rate_matching_gap.md` for the diagnosed integration gap
and repair. The old JSON is preserved as pre-correction evidence.

`generate_rate_matching.py` separately creates 64 input-position vectors for
BG1/BG2 Z8, RV 0..3, Qm 2/4/6/8, selection and repetition, seven filler bits.
The external rate matcher receives distinct position labels: because rate
matching is a selection/permutation, its outputs identify exact mother-code
coordinates. C uses those independent coordinates to check bits, soft feedback
and cumulative LLR scatter (IR/Chase), including untouched punctured/filler
positions. Four three-CB cases cover unequal E_r allocation for Nl=1. The
header and reference-module hashes are in `sch_rate_matching_manifest.json`.
LBRM, CBGTI, external multi-layer E_r allocation and TB segmentation/CRC are
outside this external coverage.
