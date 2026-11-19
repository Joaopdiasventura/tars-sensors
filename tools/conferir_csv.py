"""Verifica arquivos E0000001.CSV e o EVENTOS.CSV gerados pelo ESP32.

Uso: python tools/conferir_csv.py sdcard
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
import re

COLUNAS = [
    'evento', 'timestamp_ms', 'timestamp_utc', 'origem_gps', 'gps_valido',
    'latitude', 'longitude', 'previsao_alta', 'classe', 'adc',
]


def ler_csv(arquivo: Path):
    with arquivo.open('r', newline='', encoding='utf-8-sig') as stream:
        leitor = csv.DictReader(stream)
        if leitor.fieldnames != COLUNAS:
            raise ValueError(f'{arquivo.name}: cabecalho inesperado: {leitor.fieldnames!r}')
        linhas = list(leitor)
    for linha in linhas:
        if any(v is None for v in linha.values()):
            raise ValueError(f'{arquivo.name}: campo ausente numa linha CSV')
        if linha['previsao_alta'] not in ('0', '1'):
            raise ValueError(f'{arquivo.name}: classificacao fora de 0/1')
        esperado = 'ALTA' if linha['previsao_alta'] == '1' else 'NAO_ALTA'
        if linha['classe'] != esperado:
            raise ValueError(f'{arquivo.name}: classe textual inconsistente')
        if linha['gps_valido'] not in ('0', '1'):
            raise ValueError(f'{arquivo.name}: gps_valido fora de 0/1')
        if linha['gps_valido'] == '0' and (linha['latitude'] or linha['longitude']):
            raise ValueError(f'{arquivo.name}: coordenadas com gps_valido=0')
        if linha['gps_valido'] == '1' and (not linha['latitude'] or not linha['longitude']):
            raise ValueError(f'{arquivo.name}: coordenadas ausentes apesar de gps_valido=1')
    return linhas


def validar(pasta: Path):
    files = sorted(p for p in pasta.iterdir() if p.is_file() and re.fullmatch(r'E\d{7}\.CSV', p.name, re.I))
    if not files:
        print('Nenhum evento encontrado. Altere o potenciometro, pare o Wokwi e confira sdcard/.')
        return 0
    vistos = {}
    for path in files:
        linhas = ler_csv(path)
        if len(linhas) != 1:
            raise ValueError(f'{path.name}: esperado exatamente 1 evento, recebido {len(linhas)}')
        item = linhas[0]
        event_id = int(path.stem[1:])
        if int(item['evento']) != event_id:
            raise ValueError(f'{path.name}: nome e ID de evento diferentes')
        if event_id in vistos:
            raise ValueError(f'{path.name}: ID duplicado')
        vistos[event_id] = item
    history = pasta / 'EVENTOS.CSV'
    if history.exists():
        linhas = ler_csv(history)
        historico = {int(item['evento']): item for item in linhas}
        if len(historico) != len(linhas):
            raise ValueError('EVENTOS.CSV: IDs duplicados')
        faltantes = [i for i in vistos if i not in historico]
        divergentes = [i for i in vistos if i in historico and vistos[i] != historico[i]]
        extras = [i for i in historico if i not in vistos]
        if faltantes or divergentes or extras:
            raise ValueError(f'Divergencia no historico: faltantes={faltantes}, '
                             f'divergentes={divergentes}, extras={extras}')
        print(f'OK: {len(files)} CSVs individuais e EVENTOS.CSV consistentes.')
    else:
        print(f'AVISO: {len(files)} eventos individuais encontrados, sem EVENTOS.CSV.')
    return len(files)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pasta', type=Path, nargs='?', default=Path('sdcard'))
    args = parser.parse_args()
    if not args.pasta.is_dir():
        parser.error(f'Diretorio nao encontrado: {args.pasta}')
    validar(args.pasta)
