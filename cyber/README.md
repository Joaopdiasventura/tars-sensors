# Cyber: validação dos eventos do ESP32 antes da nuvem

Prática de segurança demonstrada para a disciplina de Cognitive Cyber Security da Global Solution 2026.2 (TARS). É um programa em Python que lê o CSV de 12 colunas gravado pelo firmware e decide o que é confiável antes de o arquivo seguir para o S3, a SQS, a Lambda e o MySQL.

**Não altera o firmware nem o formato do CSV.** Só adiciona esta pasta.

## O que faz

1. `validar_linha()` recusa a linha que tiver: sensor fora do cadastro (`TARS-ESP32-001`), origem do GPS desconhecida, `adc` fora de 0 a 4095, `previsao_alta` e `classe` que não concordam, `gps_valido=1` sem coordenada, coordenada fora dos limites ou não finita, data fora do padrão RFC 3339, campo a mais ou a menos.
2. `processar_csv()` recusa cabeçalho diferente das 12 colunas, `timestamp_ms` que volta no tempo dentro da mesma sessão e evento repetido com valores diferentes.
3. `reduzir_precisao_gps()` arredonda a posição para 2 casas decimais (cerca de 1 km) antes de mostrar ou analisar, como minimização de dado de localização.
4. `sha256_arquivo()` e `verificar_integridade()` detectam alteração do arquivo, inclusive quando o valor alterado continua dentro da faixa e passa na validação.

## Como rodar

Na pasta `cyber`, com Python 3 e `pytest`:

```
python -I -m pytest -q test_protecao_eventos.py
python -I protecao_eventos.py entradas/eventos_real.csv
python -I protecao_eventos.py entradas/eventos_real.csv --sha256 <hash registrado>
```

Resultado em 19/11/2026: 16 testes passam. As saídas dos 4 testes do documento estão em `evidencias/`.

## Entradas de teste

| Arquivo | Para que serve | Resultado |
|---|---|---|
| `entradas/eventos_real.csv` | exemplo do repositório (posição fictícia) | 6 válidas, 0 recusadas |
| `entradas/eventos_invalidos.csv` | `adc` fora da faixa, classe incoerente, GPS sem coordenada, latitude impossível | 2 válidas, 4 recusadas |
| `entradas/eventos_adulterados.csv` | sensor desconhecido, texto SQL no código do sensor, campo extra, evento repetido, tempo que volta | 2 válidas, 5 recusadas |
| `entradas/eventos_alterado_na_faixa.csv` | `adc` do evento 1 trocado de 3895 para 3000 | passa na validação, mas o hash não confere |

## Limites

- As regras reescrevem em Python o que o README do `tars-cloud` descreve para a ingestão em Go. Não substituem essa ingestão e não foram executadas contra a AWS.
- Temperatura e umidade do DHT22 não vão para o CSV, então não são validadas aqui.
- O hash só vale se o valor registrado ficar guardado onde o atacante não altere. Assinatura do arquivo e controle de escrita no S3 ficam como proposta.
- A validação por faixa não pega um valor plausível mas errado.
