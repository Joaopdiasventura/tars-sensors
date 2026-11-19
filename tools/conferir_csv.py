
"""Validador de arquivos CSV gerados pelo TARS Sensors.

Uso:
    python tools/conferir_csv.py sdcard

Valida:
- Cabeçalho com 12 colunas.
- Identificação do sensor e da sessão.
- Identificadores e nomes dos arquivos.
- Integridade dos campos obrigatórios.
- Classificação binária e classe textual.
- Valores ADC.
- Coordenadas e origem GPS.
- Consistência entre eventos individuais e histórico.
- Eventos duplicados ou ausentes.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

COLUNAS = [
    "sensor_codigo",
    "sessao_coleta",
    "evento",
    "timestamp_ms",
    "timestamp_utc",
    "origem_gps",
    "gps_valido",
    "latitude",
    "longitude",
    "previsao_alta",
    "classe",
    "adc",
]

ARQUIVO_EVENTO = re.compile(r"E(\d{7})\.CSV", re.IGNORECASE)
CODIGO_SESSAO = re.compile(r"^(.+)-S([1-9]\d*)$")
TIMESTAMP_UTC = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$"
)


def ler_csv(arquivo: Path) -> list[dict[str, str]]:
    with arquivo.open(
        "r",
        encoding="utf-8-sig",
        newline="",
    ) as stream:
        leitor = csv.DictReader(stream)

        if leitor.fieldnames != COLUNAS:
            raise ValueError(
                f"{arquivo.name}: cabeçalho inválido.\n"
                f"Esperado: {COLUNAS}\n"
                f"Recebido: {leitor.fieldnames}"
            )

        linhas = list(leitor)

    for numero, linha in enumerate(linhas, start=2):
        validar_linha(arquivo.name, numero, linha)

    return linhas


def validar_linha(
    arquivo: str,
    numero: int,
    linha: dict[str, str],
) -> None:
    prefixo = f"{arquivo}, linha {numero}"

    if None in linha or any(
        valor is None for valor in linha.values()
    ):
        raise ValueError(
            f"{prefixo}: quantidade de colunas inválida"
        )

    sensor = linha["sensor_codigo"].strip()
    sessao = linha["sessao_coleta"].strip()

    if not sensor:
        raise ValueError(
            f"{prefixo}: sensor_codigo vazio"
        )

    match_sessao = CODIGO_SESSAO.fullmatch(sessao)

    if (
        match_sessao is None
        or match_sessao.group(1) != sensor
    ):
        raise ValueError(
            f"{prefixo}: sessão incompatível com sensor"
        )

    try:
        evento = int(linha["evento"])
        timestamp = int(linha["timestamp_ms"])
        adc = int(linha["adc"])
    except ValueError as exc:
        raise ValueError(
            f"{prefixo}: campo numérico inválido"
        ) from exc

    if not 1 <= evento <= 9_999_999:
        raise ValueError(
            f"{prefixo}: identificador de evento inválido"
        )

    if not 0 <= timestamp <= 0xFFFFFFFF:
        raise ValueError(
            f"{prefixo}: timestamp_ms inválido"
        )

    if not 0 <= adc <= 4095:
        raise ValueError(
            f"{prefixo}: ADC fora do intervalo 0–4095"
        )

    previsao = linha["previsao_alta"]

    if previsao not in ("0", "1"):
        raise ValueError(
            f"{prefixo}: previsao_alta deve ser 0 ou 1"
        )

    classe_esperada = (
        "ALTA" if previsao == "1" else "NAO_ALTA"
    )

    if linha["classe"] != classe_esperada:
        raise ValueError(
            f"{prefixo}: classe inconsistente. "
            f"Esperado: {classe_esperada}"
        )

    origem = linha["origem_gps"]
    gps_valido = linha["gps_valido"]

    if origem not in ("SIMULADO_WOKWI", "NEO6M_UART"):
        raise ValueError(
            f"{prefixo}: origem_gps desconhecida"
        )

    if gps_valido not in ("0", "1"):
        raise ValueError(
            f"{prefixo}: gps_valido deve ser 0 ou 1"
        )

    latitude = linha["latitude"]
    longitude = linha["longitude"]
    utc = linha["timestamp_utc"]

    if gps_valido == "0":
        if latitude or longitude:
            raise ValueError(
                f"{prefixo}: coordenadas com gps_valido=0"
            )
    else:
        if not latitude or not longitude:
            raise ValueError(
                f"{prefixo}: coordenadas ausentes"
            )

        try:
            lat = float(latitude)
            lon = float(longitude)
        except ValueError as exc:
            raise ValueError(
                f"{prefixo}: coordenadas inválidas"
            ) from exc

        if not -90 <= lat <= 90:
            raise ValueError(
                f"{prefixo}: latitude fora do intervalo"
            )

        if not -180 <= lon <= 180:
            raise ValueError(
                f"{prefixo}: longitude fora do intervalo"
            )

    if origem == "SIMULADO_WOKWI":
        if gps_valido != "1":
            raise ValueError(
                f"{prefixo}: GPS simulado deve ser válido"
            )

        if utc:
            raise ValueError(
                f"{prefixo}: GPS simulado não fornece UTC"
            )

    if utc and not TIMESTAMP_UTC.fullmatch(utc):
        raise ValueError(
            f"{prefixo}: timestamp_utc inválido"
        )


def validar(pasta: Path) -> int:
    if not pasta.is_dir():
        raise ValueError(
            f"Diretório não encontrado: {pasta}"
        )

    arquivos = sorted(
        arquivo
        for arquivo in pasta.iterdir()
        if arquivo.is_file()
        and ARQUIVO_EVENTO.fullmatch(arquivo.name)
    )

    if not arquivos:
        print(
            "AVISO: nenhum arquivo de evento encontrado.\n"
            "Altere o potenciômetro, encerre o Wokwi "
            "e verifique a pasta sdcard/."
        )
        return 0

    eventos: dict[int, dict[str, str]] = {}
    sensores: set[str] = set()
    sessoes: set[str] = set()

    for arquivo in arquivos:
        match = ARQUIVO_EVENTO.fullmatch(arquivo.name)
        assert match is not None

        identificador = int(match.group(1))
        linhas = ler_csv(arquivo)

        if len(linhas) != 1:
            raise ValueError(
                f"{arquivo.name}: esperado exatamente "
                f"1 evento, encontrados {len(linhas)}"
            )

        linha = linhas[0]
        evento = int(linha["evento"])

        if evento != identificador:
            raise ValueError(
                f"{arquivo.name}: ID do CSV não corresponde "
                f"ao nome do arquivo"
            )

        if evento in eventos:
            raise ValueError(
                f"{arquivo.name}: evento duplicado"
            )

        eventos[evento] = linha
        sensores.add(linha["sensor_codigo"])
        sessoes.add(linha["sessao_coleta"])

    historico = pasta / "EVENTOS.CSV"

    if historico.exists():
        linhas_historico = ler_csv(historico)

        registros: dict[int, dict[str, str]] = {}

        for linha in linhas_historico:
            evento = int(linha["evento"])

            if evento in registros:
                raise ValueError(
                    f"EVENTOS.CSV: ID duplicado: {evento}"
                )

            registros[evento] = linha

        faltantes = sorted(
            set(eventos) - set(registros)
        )

        extras = sorted(
            set(registros) - set(eventos)
        )

        divergentes = sorted(
            evento
            for evento in set(eventos) & set(registros)
            if eventos[evento] != registros[evento]
        )

        if faltantes or extras or divergentes:
            raise ValueError(
                "Divergência no histórico:\n"
                f"  Faltantes: {faltantes}\n"
                f"  Extras: {extras}\n"
                f"  Divergentes: {divergentes}"
            )

        print(
            "OK: arquivos individuais e EVENTOS.CSV "
            "consistentes."
        )
    else:
        print(
            "AVISO: EVENTOS.CSV não encontrado. "
            "Os arquivos individuais foram validados."
        )

    print()
    print("========== TARS | VALIDAÇÃO ==========")
    print(f"Diretório: {pasta.resolve()}")
    print(f"Eventos válidos: {len(eventos)}")
    print(f"Sensores: {len(sensores)}")
    print(f"Sessões: {len(sessoes)}")

    altas = sum(
        linha["previsao_alta"] == "1"
        for linha in eventos.values()
    )

    print(f"Eventos ALTA: {altas}")
    print(f"Eventos NAO_ALTA: {len(eventos) - altas}")

    for sensor in sorted(sensores):
        print(f"Sensor: {sensor}")

    for sessao in sorted(sessoes):
        print(f"Sessão: {sessao}")

    print("======================================")

    return len(eventos)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validador CSV do TARS Sensors"
    )

    parser.add_argument(
        "pasta",
        type=Path,
        nargs="?",
        default=Path("sdcard"),
        help="Diretório dos arquivos CSV",
    )

    args = parser.parse_args()

    try:
        validar(args.pasta)
        return 0
    except (ValueError, OSError, csv.Error) as exc:
        print(f"ERRO: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
