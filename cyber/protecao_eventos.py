"""TARS: proteção dos eventos do ESP32 antes da nuvem (Cognitive Cyber Security, GS 2026.2).

Versão 2 do Cyber, para o dispositivo de alerta atual (repositório tars-sensors):
potenciômetro (saída binária simulada do modelo: ALTA ou NAO_ALTA) + posição GPS.
Cada mudança de classe vira uma linha de CSV com 12 colunas, que segue para S3, SQS,
Lambda e MySQL (repositório tars-cloud).

Este programa demonstra, em Python, as práticas que valem ANTES de o arquivo entrar na nuvem:
  1. validar_linha(): só aceita linha com 12 campos, sensor cadastrado, números dentro da
     faixa, classe coerente com a previsão e coordenadas possíveis (mesmas regras da
     ingestão em Go do tars-cloud, reescritas aqui para poder testar e mostrar);
  2. reduzir_precisao_gps(): minimização do dado de localização antes de qualquer uso;
  3. sha256_arquivo() e verificar_integridade(): detectar alteração do arquivo, mesmo
     quando os valores alterados continuam dentro da faixa.

Uso:  python protecao_eventos.py entradas/eventos_real.csv
      python protecao_eventos.py entradas/eventos_real.csv --sha256 <hash esperado>
"""
import csv
import hashlib
import math
import re
import sys
from datetime import datetime

COLUNAS = [
    "sensor_codigo", "sessao_coleta", "evento", "timestamp_ms", "timestamp_utc",
    "origem_gps", "gps_valido", "latitude", "longitude", "previsao_alta", "classe", "adc",
]

# Cadastro de sensores (o mesmo catálogo de teste do tars-cloud). Sensor fora daqui é recusado.
SENSORES_CADASTRADOS = ["TARS-ESP32-001"]
ORIGENS_GPS = ["SIMULADO_WOKWI", "NEO6M_UART"]
CLASSE_POR_PREVISAO = {"0": "NAO_ALTA", "1": "ALTA"}

ADC_MAXIMO = 4095          # o ADC do ESP32 vai de 0 a 4095
EVENTO_MAXIMO = 2 ** 32 - 1
CASAS_GPS_PUBLICAS = 2     # 2 casas decimais são cerca de 1 km: vale para região, não para um endereço

PADRAO_CODIGO = re.compile(r"^[A-Za-z0-9._-]{1,64}$")


def _inteiro(texto):
    """Converte texto para inteiro sem aceitar sinal, espaço ou ponto decimal."""
    if re.fullmatch(r"\d+", texto) is None:
        raise ValueError("nao e inteiro")
    return int(texto)


def _decimal_finito(texto):
    valor = float(texto)
    if math.isnan(valor) or math.isinf(valor):
        raise ValueError("nao finito")
    return valor


def validar_linha(campos):
    """Recebe a linha já separada em dicionário. Retorna (True, "") ou (False, motivo)."""
    if campos["sensor_codigo"] not in SENSORES_CADASTRADOS:
        return False, "sensor nao cadastrado"
    if PADRAO_CODIGO.match(campos["sessao_coleta"]) is None:
        return False, "sessao de coleta fora do padrao"
    if campos["origem_gps"] not in ORIGENS_GPS:
        return False, "origem do GPS desconhecida"

    try:
        evento = _inteiro(campos["evento"])
        _inteiro(campos["timestamp_ms"])
        adc = _inteiro(campos["adc"])
    except ValueError:
        return False, "evento, timestamp_ms ou adc nao e inteiro"
    if evento < 1 or evento > EVENTO_MAXIMO:
        return False, "evento fora de 1 a " + str(EVENTO_MAXIMO)
    if adc > ADC_MAXIMO:
        return False, "adc fora da faixa possivel (0 a " + str(ADC_MAXIMO) + ")"

    if campos["gps_valido"] not in ("0", "1"):
        return False, "gps_valido deve ser 0 ou 1"
    if campos["previsao_alta"] not in ("0", "1"):
        return False, "previsao_alta deve ser 0 ou 1"
    if campos["classe"] != CLASSE_POR_PREVISAO[campos["previsao_alta"]]:
        return False, "classe nao concorda com a previsao"

    if campos["timestamp_utc"] != "":
        try:
            datetime.fromisoformat(campos["timestamp_utc"].replace("Z", "+00:00"))
        except ValueError:
            return False, "timestamp_utc fora do padrao RFC3339"

    latitude, longitude = campos["latitude"], campos["longitude"]
    if campos["gps_valido"] == "1" and (latitude == "" or longitude == ""):
        return False, "gps valido sem latitude ou longitude"
    if latitude != "" or longitude != "":
        try:
            lat = _decimal_finito(latitude)
            lon = _decimal_finito(longitude)
        except ValueError:
            return False, "coordenada nao numerica ou nao finita"
        if lat < -90 or lat > 90 or lon < -180 or lon > 180:
            return False, "coordenada fora dos limites geograficos"

    return True, ""


def reduzir_precisao_gps(latitude, longitude, casas=CASAS_GPS_PUBLICAS):
    """Minimização (LGPD): arredonda a posição antes de mostrar ou guardar para análise.

    Sem coordenada, devolve vazio. Com 2 casas a posição serve para dizer a região,
    mas não aponta a casa de ninguém.
    """
    if latitude == "" or longitude == "":
        return "", ""
    return str(round(float(latitude), casas)), str(round(float(longitude), casas))


def sha256_arquivo(caminho):
    """Impressão digital do arquivo. Qualquer byte alterado muda o resultado."""
    resumo = hashlib.sha256()
    with open(caminho, "rb") as arquivo:
        for bloco in iter(lambda: arquivo.read(65536), b""):
            resumo.update(bloco)
    return resumo.hexdigest()


def verificar_integridade(caminho, hash_esperado):
    """Retorna (True, hash) se o arquivo é o mesmo que foi registrado, senão (False, hash)."""
    atual = sha256_arquivo(caminho)
    return atual == hash_esperado.strip().lower(), atual


def processar_csv(caminho):
    """Lê o CSV de eventos, recusa as linhas ruins e resume o que foi aceito.

    Retorna um dicionário com: validas, recusadas (linha, conteúdo, motivo) e eventos
    (cada evento aceito, já com a posição reduzida).
    """
    recusadas = []
    eventos = []
    ultimo_tempo = {}     # sessão -> último timestamp_ms aceito
    identidades = {}      # (sensor, sessão, evento) -> linha já aceita

    with open(caminho, encoding="utf-8-sig", newline="") as arquivo:
        leitor = csv.reader(arquivo)
        cabecalho = next(leitor, [])
        cabecalho = [c.strip() for c in cabecalho]
        if sorted(cabecalho) != sorted(COLUNAS):
            return {
                "validas": 0,
                "recusadas": [{"linha": 1, "conteudo": ",".join(cabecalho),
                               "motivo": "cabecalho diferente das 12 colunas esperadas"}],
                "eventos": [],
            }

        for numero, valores in enumerate(leitor, start=2):
            if valores == [] or all(v.strip() == "" for v in valores):
                continue
            conteudo = ",".join(valores)

            if len(valores) != len(COLUNAS):
                recusadas.append({"linha": numero, "conteudo": conteudo,
                                  "motivo": "formato errado: esperados 12 campos"})
                continue

            campos = {nome: valor.strip() for nome, valor in zip(cabecalho, valores)}
            ok, motivo = validar_linha(campos)
            if not ok:
                recusadas.append({"linha": numero, "conteudo": conteudo, "motivo": motivo})
                continue

            # Regras que dependem das outras linhas do arquivo
            sessao = campos["sessao_coleta"]
            tempo = int(campos["timestamp_ms"])
            if sessao in ultimo_tempo and tempo < ultimo_tempo[sessao]:
                recusadas.append({"linha": numero, "conteudo": conteudo,
                                  "motivo": "timestamp_ms regrediu dentro da mesma sessao"})
                continue
            identidade = (campos["sensor_codigo"], sessao, campos["evento"])
            if identidade in identidades and identidades[identidade] != campos:
                recusadas.append({"linha": numero, "conteudo": conteudo,
                                  "motivo": "evento repetido com valores diferentes"})
                continue

            ultimo_tempo[sessao] = tempo
            identidades[identidade] = campos
            lat, lon = reduzir_precisao_gps(campos["latitude"], campos["longitude"])
            eventos.append({
                "sensor": campos["sensor_codigo"], "sessao": sessao,
                "evento": int(campos["evento"]), "classe": campos["classe"],
                "adc": int(campos["adc"]), "gps_valido": campos["gps_valido"],
                "latitude_publica": lat, "longitude_publica": lon,
            })

    return {"validas": len(eventos), "recusadas": recusadas, "eventos": eventos}


def imprimir_resultado(resultado):
    print("Linhas validas:", resultado["validas"])
    print("Linhas recusadas:", len(resultado["recusadas"]))
    for r in resultado["recusadas"]:
        print("  linha", r["linha"], "->", r["motivo"], "|", r["conteudo"])
    altas = sum(1 for e in resultado["eventos"] if e["classe"] == "ALTA")
    print("Eventos aceitos:", len(resultado["eventos"]), "| ALTA:", altas)
    for e in resultado["eventos"]:
        print("  evento", e["evento"], e["classe"], "adc", e["adc"],
              "posicao publica", e["latitude_publica"], e["longitude_publica"])


if __name__ == "__main__":
    argumentos = sys.argv[1:]
    if len(argumentos) not in (1, 3) or (len(argumentos) == 3 and argumentos[1] != "--sha256"):
        print("Uso: python protecao_eventos.py caminho_do_csv [--sha256 hash_esperado]")
        sys.exit(1)

    if len(argumentos) == 3:
        integro, hash_atual = verificar_integridade(argumentos[0], argumentos[2])
        print("SHA-256 do arquivo:", hash_atual)
        if not integro:
            print("ARQUIVO ALTERADO: o hash nao confere com o registrado. Nada foi processado.")
            sys.exit(2)
        print("Integridade confere com o hash registrado.")

    imprimir_resultado(processar_csv(argumentos[0]))
