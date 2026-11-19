# TARS — Sistema Inteligente de Alerta para Riscos Alimentares

**Global Solution 2026.2 — FIAP**  
**Disciplina:** AI Computer Systems & Sensors (AICSS)  
**Tema:** CitizenTech — Tecnologia para melhorar a qualidade de vida

## 1. Visão geral

O TARS (Tecnologia de Antecipação de Riscos de Suprimentos) é uma solução voltada ao planejamento alimentar familiar, com foco na antecipação de possíveis aumentos nos preços de alimentos essenciais.

A proposta considera a utilização de indicadores econômicos, dados agrícolas e informações ambientais para apoiar modelos de Inteligência Artificial capazes de identificar tendências de variação de preços.

Este repositório apresenta o protótipo embarcado do TARS, desenvolvido com ESP32 e simulado no Wokwi.

O dispositivo combina uma saída simulada de um modelo preditivo com medições ambientais de temperatura e umidade, classificando o cenário em três níveis de alerta e notificando o usuário localmente por LEDs.

O protótipo também registra mudanças na classificação simulada em arquivos CSV armazenados em cartão microSD.

**Limitação:** nesta versão, o ESP32 não executa um modelo de IA. A saída binária é representada por um potenciômetro, permitindo demonstrar como o dispositivo reagiria ao resultado de uma inferência.

## 2. Problema e público beneficiado

A oscilação dos preços de alimentos essenciais pode prejudicar o planejamento financeiro de famílias, especialmente daquelas com menor margem orçamentária.

O TARS busca contribuir com a redução da assimetria de informação, tornando sinais de possíveis aumentos mais acessíveis.

O público beneficiado inclui:

- Famílias que precisam planejar compras de alimentos essenciais.
- Pessoas com orçamento doméstico limitado.
- Comunidades interessadas em informações preventivas sobre riscos de abastecimento e preços.

O dispositivo representa a camada de alerta local da solução, permitindo comunicar situações relevantes sem depender continuamente de uma interface web.

## 3. Objetivos do protótipo

- Capturar periodicamente uma classificação binária simulada de variação do preço do arroz.
- Medir temperatura e umidade utilizando um DHT22.
- Combinar as entradas em três níveis de alerta: NORMAL, ATENÇÃO e CRÍTICO.
- Comunicar imediatamente mudanças de nível por LEDs.
- Disponibilizar medições e estados no Monitor Serial.
- Registrar mudanças confirmadas da classificação binária em arquivos CSV.
- Associar os eventos a coordenadas GPS simuladas ou, opcionalmente, obtidas de um módulo físico.

## 4. Componentes utilizados

| Componente               | Função                               |
| ------------------------ | ------------------------------------ |
| ESP32 DevKit C V4        | Processamento e controle             |
| Potenciômetro deslizante | Simulação da saída binária do modelo |
| DHT22                    | Medição de temperatura e umidade     |
| LED verde                | Indicação NORMAL                     |
| LED amarelo              | Indicação ATENÇÃO                    |
| LED vermelho             | Indicação CRÍTICO                    |
| 3 resistores de 220 Ω    | Limitação de corrente dos LEDs       |
| Módulo microSD SPI       | Armazenamento dos eventos            |
| GPS NEO-6M (opcional)    | Localização física por UART          |
| Monitor Serial           | Observabilidade e diagnóstico        |

O potenciômetro funciona como entrada analógica que simula um resultado externo, e não como sensor econômico real.

O DHT22 fornece medições ambientais efetivas dentro da simulação.

As coordenadas GPS do Wokwi são fixas e simuladas, não correspondendo a uma leitura real de posicionamento.

## 5. Arquitetura

O funcionamento é dividido em cinco etapas:

1. Aquisição das entradas pelo ESP32.
2. Tratamento da classificação simulada com histerese e debounce.
3. Classificação do nível de alerta utilizando temperatura, umidade e resultado binário.
4. Comunicação do alerta por LEDs e Monitor Serial.
5. Persistência das mudanças binárias em arquivos CSV no microSD.

Fluxo simplificado:

```text
Potenciômetro ──> ADC GPIO34 ──> Histerese/Debounce ──┐
                                                       │
DHT22 ──────────> Temperatura/Umidade ──────────────────┤
                                                       ▼
                                                Classificação
                                                       │
                                          ┌────────────┼────────────┐
                                          ▼            ▼            ▼
                                       NORMAL       ATENÇÃO       CRÍTICO
                                      LED verde   LED amarelo   LED vermelho

Mudanças binárias confirmadas:
Potenciômetro → ESP32 → GPS → CSV → microSD

Monitoramento:
ESP32 → Monitor Serial
```

O GPS fornece contexto geográfico para os eventos persistidos, mas não participa das regras de classificação de alerta.

## 6. Sensores e justificativa

### 6.1 Potenciômetro — classificação simulada

O potenciômetro representa a saída de um modelo preditivo externo.

- `0`: não há previsão de alta.
- `1`: há previsão de alta.

A variável representada é a possível variação positiva do IPCA do arroz no mês seguinte, e não o preço de um produto específico.

O ESP32 lê valores ADC entre aproximadamente 0 e 4095.

Para reduzir oscilações:

| Condição            | Regra                        |
| ------------------- | ---------------------------- |
| Estado inicial      | ADC >= 2048 representa 1     |
| Mudança de 0 para 1 | ADC >= 2500                  |
| Mudança de 1 para 0 | ADC <= 1600                  |
| Confirmação         | Candidato estável por 200 ms |

A região intermediária mantém a classificação anterior, aplicando histerese.

A mudança de classe é confirmada somente depois que seu CSV individual é gravado com sucesso.

### 6.2 DHT22 — temperatura e umidade

O DHT22 mede duas variáveis ambientais:

- Temperatura em graus Celsius.
- Umidade relativa em porcentagem.

Essas variáveis são utilizadas como indicadores ambientais complementares para a classificação de alertas.

Temperatura e umidade podem ser relevantes para condições de armazenamento e conservação, mas não determinam isoladamente a variação futura dos preços do arroz.

Os limites de alerta são regras experimentais do protótipo e não representam limites agronômicos ou previsões econômicas cientificamente validadas.

### 6.3 GPS — contexto de localização

No modo Wokwi, o dispositivo utiliza a posição fictícia:

- Latitude: -23.550520
- Longitude: -46.633308

Essa localização é uma referência simulada na cidade de São Paulo.

O código também disponibiliza suporte a um módulo GPS NEO-6M conectado por UART2.

Para utilizar o GPS físico, alterar no `platformio.ini`:

```ini
build_flags =
    -DTARS_SIMULATED_GPS=0
```

A montagem atual do Wokwi não inclui módulo GPS físico ou virtual conectado por UART.

## 7. Classificação dos alertas

O sistema utiliza três níveis:

### NORMAL — LED verde

Ativado quando:

- Previsão de alta = 0.
- Temperatura abaixo de 30 °C.
- Umidade acima de 35% e abaixo de 70%.
- Medições ambientais válidas.

Representa ausência de condições de alerta pelas regras configuradas.

### ATENÇÃO — LED amarelo

Ativado quando ocorre pelo menos uma das condições:

- Previsão de alta = 1, sem condição ambiental severa.
- Temperatura >= 30 °C.
- Umidade >= 70%.
- Umidade <= 35%.

Também é utilizado quando o DHT22 não apresenta leitura válida.

Uma condição ambiental severa acompanhada de previsão de alta passa a ser CRÍTICO.

### CRÍTICO — LED vermelho

Ativado quando existe previsão de alta (`1`) combinada com pelo menos uma condição ambiental severa:

- Temperatura >= 35 °C.
- Umidade >= 85%.
- Umidade <= 25%.

Essa combinação representa o nível máximo de atenção definido para o protótipo.

### Exemplos de classificação

| Previsão | Temperatura | Umidade | Alerta  |
| -------- | ----------- | ------- | ------- |
| 0        | 25 °C       | 55%     | NORMAL  |
| 1        | 25 °C       | 55%     | ATENÇÃO |
| 0        | 32 °C       | 55%     | ATENÇÃO |
| 0        | 36 °C       | 55%     | ATENÇÃO |
| 1        | 36 °C       | 55%     | CRÍTICO |
| 1        | 25 °C       | 90%     | CRÍTICO |
| 1        | 25 °C       | 20%     | CRÍTICO |

A classificação não equivale a uma probabilidade de aumento do preço, nem a uma previsão validada de perda agrícola.

## 8. Periodicidade das medições

O projeto emprega intervalos independentes para acompanhar cada variável conforme seu comportamento.

| Operação                       | Intervalo |
| ------------------------------ | --------- |
| Leitura ADC                    | 40 ms     |
| Confirmação da transição       | 200 ms    |
| Leitura DHT22                  | 2.000 ms  |
| Relatório Serial               | 5.000 ms  |
| Nova tentativa de persistência | 1.000 ms  |

### ADC — 40 ms

A leitura ocorre 25 vezes por segundo, proporcionando resposta rápida a alterações feitas no potenciômetro durante a demonstração.

Como a entrada representa um resultado binário, não há necessidade de amostragem analógica de frequência elevada.

### Debounce — 200 ms

A transição precisa permanecer estável por 200 ms antes de ser considerada válida.

Esse mecanismo evita registrar eventos ocasionados por oscilações momentâneas ou ruído do ADC.

O intervalo corresponde a aproximadamente cinco ciclos de leitura de 40 ms.

### DHT22 — 2.000 ms

O DHT22 possui frequência limitada de atualização, sendo adequado utilizar um intervalo de pelo menos aproximadamente dois segundos entre novas leituras.

Temperatura e umidade normalmente apresentam mudanças mais lentas do que a interação do usuário com o potenciômetro.

Por isso, o intervalo preserva a qualidade da amostragem sem consultar o sensor desnecessariamente.

### Serial — 5.000 ms

O relatório periódico a cada cinco segundos reduz o volume de mensagens e facilita a interpretação dos dados.

Mudanças confirmadas de classificação e alterações no nível de alerta também podem produzir mensagens adicionais, sem aguardar o próximo relatório periódico.

### Persistência — 1.000 ms

Quando a gravação de um evento falha, o firmware utiliza intervalo de um segundo para uma nova tentativa.

Isso evita repetir continuamente a operação de escrita em caso de indisponibilidade do cartão.

## 9. Monitor Serial

O projeto utiliza comunicação UART a 115200 baud.

Os relatórios periódicos apresentam dados em formato textual `chave=valor`, facilitando a leitura humana e a identificação dos campos.

Exemplo ilustrativo:

```text
========== TARS ==========
sensor_codigo=TARS-ESP32-001
sessao_coleta=TARS-ESP32-001-S1
timestamp_ms=5000
previsao_alta=1
classe=ALTA
nivel_alerta=ATENCAO
temperatura_c=25.00
umidade_pct=55.00
origem_gps=SIMULADO_WOKWI
gps_valido=1
latitude=-23.550520
longitude=-46.633308
sd_disponivel=1
==========================
```

O formato foi escolhido por ser simples, legível e adequado para demonstração e diagnóstico.

Não é necessário transmitir dados pela rede para acompanhar as medições locais.

## 10. Armazenamento em CSV

O módulo microSD armazena as transições confirmadas da classificação binária.

Cada mudança gera um arquivo individual:

```text
sdcard/
├── E0000001.CSV
├── E0000002.CSV
├── E0000003.CSV
├── EVENTOS.CSV
└── SESSAO.TXT
```

- `E0000001.CSV`: dados da primeira mudança registrada.
- `EVENTOS.CSV`: histórico consolidado dos eventos.
- `SESSAO.TXT`: identificador incremental de sessões de coleta.

O cabeçalho atual é:

```csv
sensor_codigo,sessao_coleta,evento,timestamp_ms,timestamp_utc,origem_gps,gps_valido,latitude,longitude,previsao_alta,classe,adc
```

O timestamp em milissegundos representa o tempo desde a inicialização do ESP32.

A data e hora UTC somente estarão disponíveis quando houver informações válidas de um GPS físico.

**Observação:** os arquivos CSV representam mudanças da classificação simulada. As medições de temperatura, umidade e o nível de alerta são exibidos no Serial, mas não fazem parte do CSV atual.

O armazenamento é local e não depende de conectividade com AWS ou banco de dados.

## 11. Montagem e execução

### Ferramentas

- Visual Studio Code.
- PlatformIO.
- Extensão Wokwi para VS Code.
- Ambiente ESP32 Arduino.
- Bibliotecas TinyGPSPlus, DHT Sensor Library e Adafruit Unified Sensor.

### Compilação

Na pasta do projeto:

```bash
pio run
```

### Simulação

1. Abrir o projeto no VS Code.
2. Compilar com PlatformIO.
3. Executar o simulador Wokwi.
4. Abrir o Monitor Serial.
5. Observar o LED correspondente à classificação inicial.
6. Alterar o potenciômetro.
7. Modificar temperatura e umidade do DHT22.
8. Verificar a mudança dos LEDs.
9. Verificar as mensagens no Serial.
10. Encerrar a simulação para recuperar os dados do microSD.

O `wokwi.toml` habilita `writeback = true`, permitindo disponibilizar os arquivos do cartão no diretório local após a finalização da simulação, conforme o suporte da versão utilizada do Wokwi.

## 12. Cenários de teste

Os seguintes cenários permitem demonstrar os requisitos funcionais.

| Cenário | Previsão | Temperatura | Umidade | Resultado esperado |
| ------- | -------- | ----------- | ------- | ------------------ |
| 1       | 0        | 25 °C       | 55%     | LED verde          |
| 2       | 1        | 25 °C       | 55%     | LED amarelo        |
| 3       | 1        | 36 °C       | 55%     | LED vermelho       |
| 4       | 0        | 36 °C       | 55%     | LED amarelo        |
| 5       | 1        | 25 °C       | 90%     | LED vermelho       |
| 6       | 0        | 25 °C       | 55%     | LED verde          |

**Esses são cenários previstos para validação, não evidências de testes executados.**

Para avaliar a gravação CSV, alternar o potenciômetro entre os extremos e verificar a criação de arquivos individuais.

A alteração isolada de temperatura e umidade pode modificar o LED, mas não gera um novo arquivo CSV.

## 13. Testes automatizados

O projeto contém testes para a lógica de transição binária.

Executar:

```bash
python tools/testar_estado.py
```

O teste verifica:

- Transições 0 → 1 → 0.
- Histerese.
- Debounce.
- Confirmação condicionada à persistência.
- Comportamento com estouro de `millis()`.

A validação completa depende adicionalmente da compilação do firmware e execução do circuito.

O validador CSV presente no repositório precisa ser atualizado para o cabeçalho atual antes de ser utilizado como evidência da versão entregue.

## 14. Relação com os ODS

### ODS 2 — Fome Zero e Agricultura Sustentável

O TARS busca apoiar o planejamento alimentar por meio da disponibilização de informações preventivas sobre possíveis aumentos nos preços de alimentos essenciais.

### ODS 12 — Consumo e Produção Responsáveis

A solução busca contribuir para decisões de compra mais planejadas e para a conscientização sobre condições que podem influenciar o abastecimento e a conservação dos alimentos.

Essas relações representam a contribuição pretendida pelo projeto, não impactos já mensurados.

## 15. Inovação e evolução futura

O diferencial proposto é integrar informações econômicas e ambientais a um dispositivo embarcado simples e de baixo custo.

O protótipo demonstra o funcionamento local de alertas, utilizando uma saída preditiva simulada.

Uma evolução possível consiste em embarcar um modelo de Machine Learning compacto no ESP32, após avaliar:

- Compatibilidade do modelo com o microcontrolador.
- Consumo de memória.
- Latência da inferência.
- Qualidade preditiva.
- Consumo energético.
- Custo de operação.

A execução de inferência embarcada não faz parte desta implementação.

## 16. Limitações conhecidas

- A saída do modelo é simulada por potenciômetro.
- O ESP32 não realiza treinamento ou inferência de IA.
- O GPS do Wokwi utiliza coordenadas fixas.
- A simulação não representa dados ambientais medidos em campo.
- Os limiares ambientais são experimentais.
- As regras de alerta não representam um modelo validado de previsão econômica ou risco agrícola.
- As mudanças de nível ambiental não são registradas como eventos CSV.
- A confirmação da transição binária depende da disponibilidade do armazenamento.
- O sistema não possui envio direto para APIs ou serviços em nuvem.
- Falhas elétricas ou interrupções durante a escrita podem comprometer dados.
- A validação final requer execução do circuito no Wokwi ou hardware físico.

## 17. Estrutura do repositório

```text
tars-sensors/
├── src/
│   └── main.ino
├── include/
│   └── TarsStateTracker.h
├── tests/
│   ├── test_state_tracker.cpp
│   └── test_csv_validator.py
├── tools/
│   ├── testar_estado.py
│   └── conferir_csv.py
├── exemplos/
│   └── EXEMPLO_EVENTO.csv
├── diagram.json
├── platformio.ini
├── wokwi.toml
└── README.md
```

## 18. Considerações finais

O TARS Sensors demonstra um fluxo embarcado de aquisição de entradas, tratamento de sinais, classificação de situações e alerta local.

O protótipo utiliza ESP32, potenciômetro, DHT22, LEDs e armazenamento microSD para representar uma aplicação CitizenTech voltada ao planejamento alimentar.

A solução implementada demonstra a infraestrutura necessária para receber uma classificação preditiva e transformá-la em alertas locais, preservando a distinção entre dados ambientais, resultados simulados e funcionalidades futuras de Inteligência Artificial.
