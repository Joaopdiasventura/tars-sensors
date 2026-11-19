"""Compila e executa a maquina de estados no computador, sem precisar de um ESP32.
Uso: python tools/testar_estado.py
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shutil.which('g++') or shutil.which('clang++')
if not compiler:
    raise SystemExit('Instale g++ ou clang++ para compilar o teste nativo.')
with tempfile.TemporaryDirectory(prefix='tars-test-') as temp:
    exe = Path(temp) / 'tars_state_test'
    subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'include'), str(root / 'tests/test_state_tracker.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
