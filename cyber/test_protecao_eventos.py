"""Testes da proteção dos eventos do ESP32 (Cyber, TARS). Rodar: python -I -m pytest -q"""
import os

import protecao_eventos as p

PASTA = os.path.join(os.path.dirname(__file__), "entradas")


def caminho(nome):
    return os.path.join(PASTA, nome)


def linha_valida():
    return {
        "sensor_codigo": "TARS-ESP32-001", "sessao_coleta": "TARS-ESP32-001-S1",
        "evento": "1", "timestamp_ms": "10028", "timestamp_utc": "",
        "origem_gps": "SIMULADO_WOKWI", "gps_valido": "1",
        "latitude": "-23.550520", "longitude": "-46.633308",
        "previsao_alta": "1", "classe": "ALTA", "adc": "3895",
    }


def recusa(**mudancas):
    campos = linha_valida()
    campos.update(mudancas)
    return p.validar_linha(campos)


# Teste 1: dados reais do firmware passam sem recusa
def test_csv_real_todo_aceito():
    r = p.processar_csv(caminho("eventos_real.csv"))
    assert r["validas"] == 6
    assert r["recusadas"] == []


def test_linha_valida_passa():
    assert p.validar_linha(linha_valida()) == (True, "")


# Teste 2: medição incorreta (R1)
def test_adc_fora_da_faixa_recusado():
    ok, motivo = recusa(adc="5000")
    assert not ok and "adc fora da faixa" in motivo


def test_classe_incoerente_recusada():
    ok, motivo = recusa(previsao_alta="1", classe="NAO_ALTA")
    assert not ok and "classe nao concorda" in motivo


def test_gps_valido_sem_coordenada_recusado():
    ok, motivo = recusa(latitude="", longitude="")
    assert not ok and "sem latitude" in motivo


def test_coordenada_fora_dos_limites_recusada():
    ok, motivo = recusa(latitude="123.4")
    assert not ok and "limites geograficos" in motivo


def test_coordenada_nan_recusada():
    ok, _ = recusa(latitude="nan")
    assert not ok


def test_csv_invalido_recusa_quatro_e_aceita_duas():
    r = p.processar_csv(caminho("eventos_invalidos.csv"))
    assert r["validas"] == 2
    assert len(r["recusadas"]) == 4


# Teste 3: entrada adulterada (R2)
def test_sensor_desconhecido_recusado():
    ok, motivo = recusa(sensor_codigo="HACK-001")
    assert not ok and motivo == "sensor nao cadastrado"


def test_texto_sql_no_codigo_do_sensor_recusado():
    ok, _ = recusa(sensor_codigo="TARS-ESP32-001'; DROP TABLE MEDICAO;--")
    assert not ok


def test_csv_adulterado_recusa_cinco_e_aceita_duas():
    r = p.processar_csv(caminho("eventos_adulterados.csv"))
    motivos = [x["motivo"] for x in r["recusadas"]]
    assert r["validas"] == 2
    assert "sensor nao cadastrado" in motivos
    assert "formato errado: esperados 12 campos" in motivos
    assert "evento repetido com valores diferentes" in motivos
    assert "timestamp_ms regrediu dentro da mesma sessao" in motivos


def test_cabecalho_diferente_recusa_o_arquivo(tmp_path):
    arquivo = tmp_path / "ruim.csv"
    arquivo.write_text("a,b,c\n1,2,3\n", encoding="utf-8")
    r = p.processar_csv(str(arquivo))
    assert r["validas"] == 0 and len(r["recusadas"]) == 1


# Teste 4: integridade (alteração que continua dentro da faixa)
def test_hash_detecta_alteracao_dentro_da_faixa():
    original = p.sha256_arquivo(caminho("eventos_real.csv"))
    assert p.verificar_integridade(caminho("eventos_real.csv"), original)[0]
    # o arquivo alterado passa na validação por faixa...
    assert p.processar_csv(caminho("eventos_alterado_na_faixa.csv"))["recusadas"] == []
    # ...mas o hash denuncia a alteração
    assert not p.verificar_integridade(caminho("eventos_alterado_na_faixa.csv"), original)[0]


# Privacidade (R3)
def test_reduzir_precisao_gps_arredonda_a_dois_decimais():
    assert p.reduzir_precisao_gps("-23.550520", "-46.633308") == ("-23.55", "-46.63")


def test_reduzir_precisao_sem_coordenada():
    assert p.reduzir_precisao_gps("", "") == ("", "")


def test_eventos_aceitos_nao_trazem_coordenada_precisa():
    r = p.processar_csv(caminho("eventos_real.csv"))
    for e in r["eventos"]:
        assert e["latitude_publica"] == "-23.55" and e["longitude_publica"] == "-46.63"
        assert "latitude" not in e and "longitude" not in e
