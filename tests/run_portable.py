"""Build the SDK-free bitwise regression/benchmark against the pre-optimization git revision.
Linux/macOS or VS x64 Developer Command Prompt: python tests/run_portable.py [--bench [width height]] [--sanitize]
Requires Python, git and a C++17 compiler (CXX or g++). No AE SDK or Python packages.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = '88ab06d4b25a15b91eb0ade1756ceb00579a6802'
args = sys.argv[1:]
sanitize = '--sanitize' in args
args = [a for a in args if a != '--sanitize']
with tempfile.TemporaryDirectory(prefix='aohue-check-') as directory:
    tmp = Path(directory)
    reference = tmp / 'reference.h'
    reference.write_bytes(subprocess.check_output(['git', 'show', BASE + ':src/core.h'], cwd=ROOT))
    exe = tmp / ('check_performance.exe' if os.name == 'nt' else 'check_performance')
    compiler = os.environ.get('CXX', 'cl' if os.name == 'nt' else 'g++')
    if Path(compiler).stem.lower() == 'cl':
        if sanitize:
            raise SystemExit('--sanitize uses GCC/Clang address+undefined sanitizers')
        cmd = [compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/fp:precise',
               '/DAOHUE_REFERENCE_HEADER="' + reference.as_posix() + '"',
               str(ROOT / 'tests/check_performance.cpp'), '/Fe' + str(exe),
               '/Fo' + str(tmp / 'check.obj')]
    else:
        flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitize else ['-O2']
        cmd = [compiler, '-std=c++17', '-pthread', '-ffp-contract=off', *flags,
               '-DAOHUE_REFERENCE_HEADER="' + reference.as_posix() + '"',
               str(ROOT / 'tests/check_performance.cpp'), '-o', str(exe)]
    subprocess.run(cmd, check=True, cwd=ROOT)
    subprocess.run([str(exe), *args], check=True, cwd=ROOT)
