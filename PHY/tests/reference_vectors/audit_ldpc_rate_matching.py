"""Compare legacy and corrected SCH rate matching with py3gpp 0.6.0.

Diagnostic only: prints mismatches, does not alter production code. The adapter
is an experimental Python composition, not an implemented PHY repair.
Requires the same isolated Python environment as generate_codecs.py.
"""
import ctypes as ct
import hashlib
import importlib
from importlib.metadata import version
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np
import py3gpp as nr

assert version('py3gpp') == '0.6.0'
phy = Path(__file__).resolve().parents[2]
source = phy / 'src/nr_rate_matching.c'
reference = Path(importlib.import_module('py3gpp.nrRateMatchLDPC').__file__)
rng = np.random.default_rng(38212542)
results = []
with tempfile.TemporaryDirectory(prefix='lls-rm-audit-') as tmp:
    library = Path(tmp) / 'rate_matching.so'
    subprocess.run(['cc', '-std=c99', '-O2', '-shared', '-fPIC', '-I', str(phy / 'include'),
                    str(source), '-o', str(library)], check=True)
    lib = ct.CDLL(str(library))
    ptr = ct.POINTER(ct.c_int)
    fn = lib.nr_ldpc_rate_match_select
    fn.argtypes = [ptr] + [ct.c_int] * 7 + [ptr]
    fn.restype = None
    corrected_fn = lib.nr_sch_rate_match_select
    corrected_fn.argtypes = [ptr] + [ct.c_int] * 8 + [ptr]
    corrected_fn.restype = None
    for bg, z, cols in [(1, 8, 22), (2, 72, 10)]:
        full_n = (68 if bg == 1 else 52) * z
        full_k = cols * z
        # Synthetic bits isolate selection and permutation from encoder behavior.
        full = rng.integers(0, 2, full_n, dtype=np.int32)
        full[full_k-7:full_k] = 0
        ext = full[2*z:].copy()
        ext[full_k-7-2*z:full_k-2*z] = -1
        for rv in range(4):
            for mod, qm in [('QPSK', 2), ('16QAM', 4), ('64QAM', 6), ('256QAM', 8)]:
                for repetition in [False, True]:
                    E = qm * ((len(ext)//qm) * (2 if repetition else 1)//(1 if repetition else 2) + 1)
                    expected = nr.nrRateMatchLDPC(ext[:, None], E, rv, mod, 1)
                    current = np.empty(E, dtype=np.int32)
                    fn(full.ctypes.data_as(ptr), full_n, bg, z, full_k-7, full_k,
                       rv, E, current.ctypes.data_as(ptr))
                    shifted = full[2*z:].copy()
                    selected = np.empty(E, dtype=np.int32)
                    fn(shifted.ctypes.data_as(ptr), len(shifted), bg, z, full_k-7-2*z,
                       full_k-2*z, rv, E, selected.ctypes.data_as(ptr))
                    adapted = selected.reshape(qm, -1).ravel(order='F')
                    corrected = np.empty(E, dtype=np.int32)
                    corrected_fn(full.ctypes.data_as(ptr), full_n, bg, z, full_k-7, full_k,
                                 rv, E, qm, corrected.ctypes.data_as(ptr))
                    results.append(dict(bg=bg, Zc=z, rv=rv, qm=qm, E=E, repetition=repetition,
                                        legacy_call_mismatches=int(np.count_nonzero(current != expected)),
                                        corrected_mismatches=int(np.count_nonzero(corrected != expected)),
                                        adapted_mismatches=int(np.count_nonzero(adapted != expected))))
assert all(r['adapted_mismatches'] == 0 for r in results), 'experimental adapter differs from reference'
assert all(r['corrected_mismatches'] == 0 for r in results), 'corrected C implementation differs from reference'
print(json.dumps(dict(seed=38212542, reference='py3gpp 0.6.0 nrRateMatchLDPC',
                      packages={p: version(p) for p in ['py3gpp', 'numpy']},
                      source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                      reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
                      cases=results), indent=2))
