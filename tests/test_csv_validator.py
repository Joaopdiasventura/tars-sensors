"""Confere leitura de CSVs individuais e historico usando unittest stdlib."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from conferir_csv import COLUNAS, validar


class TestCsvValidator(unittest.TestCase):
    def test_eventos_e_historico(self):
        line1 = '1,4000,,SIMULADO_WOKWI,1,-23.550520,-46.633308,1,ALTA,3900\n'
        line2 = '2,7000,,SIMULADO_WOKWI,1,-23.550520,-46.633308,0,NAO_ALTA,500\n'
        header = ','.join(COLUNAS) + '\n'
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            (base / 'E0000001.CSV').write_text(header + line1)
            (base / 'E0000002.CSV').write_text(header + line2)
            (base / 'EVENTOS.CSV').write_text(header + line1 + line2)
            self.assertEqual(validar(base), 2)
            (base / 'EVENTOS.CSV').write_text(header + line1)
            with self.assertRaisesRegex(ValueError, 'faltantes'):
                validar(base)

    def test_dados_invalidos(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            (base / 'E0000001.CSV').write_text(
                ','.join(COLUNAS) + '\n'
                + '1,10,,NEO6M_UART,0,-23.0,-46.0,1,ALTA,3500\n'
            )
            with self.assertRaisesRegex(ValueError, 'coordenadas com gps_valido=0'):
                validar(base)


if __name__ == '__main__':
    unittest.main()
