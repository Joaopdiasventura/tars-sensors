# TARS — Sensores GPS e classificação simulada do arroz

Adaptação independente do projeto [fleet-force-sensors](https://github.com/Joaopdiasventura/fleet-force-sensors), para o **TARS — Tecnologia de Antecipação de Riscos de Suprimentos**.

Esta versão mantém **somente duas entradas lógicas**: localização GPS e um potenciômetro que representa a **saída binária simulada do modelo**. Remove MPU6050, velocidade, rota controlada por potenciômetro, Wi-Fi, HTTP e telemetria de frota. **Não executa um modelo de IA no ESP32**, nem prevê preço em reais.

## O que o protótipo faz

1. Lê o único potenciômetro (GPIO34) como classe binária: **`0 = NÃO ALTA`**, **`1 = ALTA`**.
2. Consulta a localização GPS: no **Wokwi** usa coordenadas fixas de **referência simulada**; no hardware real, pode ler um **GPS NEO-6M** por UART2.
3. Observa a mudança de **classe 0 → 1 ou 1 → 0**. Oscilações do valor analógico que não alteram a classe não geram evento.
4. Quando a nova classe permanece estável por 200 ms, cria **um arquivo CSV por mudança**, por exemplo `E0000001.CSV`.
5. Adiciona a mesma linha ao histórico `EVENTOS.CSV` no **microSD**, para análises posteriores.
6. Exibe o evento no monitor serial. Não depende de API, AWS, SQL ou computador conectado para registrar no cartão, desde que o microSD esteja disponível.

> **Atenção:** a variável simulada é **se a variação mensal do IPCA do arroz no próximo mês será positiva**, não se o preço de um pacote específico vai aumentar. A saída binária do potenciômetro **não provém de treinamento/inferência neste firmware**.

## Circuito no Wokwi

| Componente | Pino | ESP32 DevKit C V4 |
|---|---|---|
| Potenciômetro deslizante | SIG | GPIO34 (ADC) |
| Potenciômetro | VCC / GND | 3V3 / GND |
| microSD SPI | CS | GPIO5 |
| microSD SPI | SCK | GPIO18 |
| microSD SPI | DO (MISO) | GPIO19 |
| microSD SPI | DI (MOSI) | GPIO23 |
| microSD SPI | VCC / GND | 3V3 / GND |

**Sensores de entrada:** um único potenciômetro; a coordenada GPS é simulada pelo firmware durante a demonstração Wokwi. O microSD é apenas **armazenamento**, não um sensor adicional.

A posição usada no Wokwi é **`-23.550520, -46.633308` (São Paulo)** e é **fictícia para fins de teste**. Ela **não identifica a posição real do dispositivo nem do usuário**.

### GPS físico opcional

Para obter coordenadas medidas, conecte um módulo **NEO-6M (interface UART NMEA a 9600 baud)**:

| GPS NEO-6M | ESP32 |
|---|---|
| TX | GPIO16 (RX2) |
| RX (opcional) | GPIO17 (TX2) |
| GND | GND |
| VCC | Alimentação **conforme a placa do módulo** e seus limites elétricos |

No `platformio.ini`, altere `-DTARS_SIMULATED_GPS=1` para `-DTARS_SIMULATED_GPS=0` e conecte o módulo físico. **Não se conecta GPS real automaticamente dentro do Wokwi**: o diagrama incluído usa explicitamente a simulação de posição. Se não houver fix GPS válido, os campos latitude/longitude são deixados em branco (`gps_valido=0`); o CSV da mudança de classificação ainda é criado.

## Executar no VS Code + Wokwi

**Requisitos:** PlatformIO IDE, extensão Wokwi para VS Code **v4.0.0 ou mais recente**, ambiente ESP32 Arduino e biblioteca `TinyGPSPlus` (configurada automaticamente em `platformio.ini`). A simulação Wokwi pode exigir acesso/licença conforme o plano e o ambiente.

1. Extraia o ZIP e abra a pasta `tars-sensores-gps-arroz` no VS Code.
2. Instale PlatformIO e a extensão Wokwi; aguarde a resolução das dependências.
3. Execute **PlatformIO: Build** ou `pio run` na pasta do projeto.
4. Abra `diagram.json` e inicie **Wokwi: Start Simulator** pela paleta de comandos.
5. Observe o estado inicial no **Serial Monitor (115200 baud)**.
6. Deslize o potenciômetro para o outro extremo e mantenha por pelo menos 200 ms.
7. A cada alternância 0↔1, o ESP32 mostra o nome do novo CSV e cria os registros no cartão.
8. **Pare a simulação**. Com `writeback = true` em `wokwi.toml`, o conteúdo do microSD é gravado na pasta local `sdcard/` após parar/reiniciar o Wokwi para VS Code. Não espere visualizar os arquivos locais antes de encerrar a simulação.

O recurso de *writeback* do microSD depende do **Wokwi para VS Code v4.0.0+**. Veja a documentação oficial: <https://docs.wokwi.com/vscode/project-config#sd-card-contents>.

### Como o potenciômetro vira 0 ou 1

A leitura ADC do ESP32 varia aproximadamente entre 0 e 4095. Para evitar alternâncias por ruído:

- No início, a classificação inicial é 0 para ADC abaixo de 2048 e 1 caso contrário. **A inicialização não gera CSV**, pois não aconteceu uma mudança observada.
- Em estado 0, valores **≥ 2500** solicitam mudança para 1.
- Em estado 1, valores **≤ 1600** solicitam mudança para 0.
- A mudança precisa permanecer observável por **200 ms** (debounce).
- No intervalo intermediário, a classe anterior é mantida (histerese).
- A classe só é confirmada quando **o CSV individual** foi gravado. Se a escrita falhar, há aviso serial e tentativa posterior. Falhas de hardware, energia e cartão não podem receber garantia absoluta de persistência.

Portanto, a cada **mudança de classe confirmada** há um CSV. Movimentos que permaneçam dentro da mesma classe não geram arquivos adicionais.

## Arquivos CSV gerados

Arquivos no microSD (`sdcard/` após encerrar a simulação):

```text
sdcard/
├── E0000001.CSV       # primeira mudanca registrada
├── E0000002.CSV       # segunda mudanca registrada
├── E0000003.CSV       # ...
└── EVENTOS.CSV        # historico acumulado
```

Cabeçalho:

```csv
evento,timestamp_ms,timestamp_utc,origem_gps,gps_valido,latitude,longitude,previsao_alta,classe,adc
```

Exemplo **ilustrativo, não uma coleta real** (também disponível em `exemplos/EXEMPLO_EVENTO.csv`):

```csv
evento,timestamp_ms,timestamp_utc,origem_gps,gps_valido,latitude,longitude,previsao_alta,classe,adc
1,4812,,SIMULADO_WOKWI,1,-23.550520,-46.633308,1,ALTA,3850
```

- `evento`: identificador incremental, preservado entre reinicializações ao detectar os arquivos existentes no cartão.
- `timestamp_ms`: milissegundos desde a inicialização atual do ESP32 (não é data/hora civil).
- `timestamp_utc`: informado somente quando há data/hora GPS físicas válidas; fica vazio em modo simulado.
- `origem_gps`: `SIMULADO_WOKWI` ou `NEO6M_UART`.
- `gps_valido`: 1 indica que há coordenadas disponíveis (inclusive simuladas); 0 indica ausência de fix real.
- `latitude`/`longitude`: posição de referência simulada ou posição do módulo real, conforme `origem_gps`.
- `previsao_alta`: saída binária simulada 0/1, não um score de confiança.
- `classe`: `ALTA` ou `NAO_ALTA`.
- `adc`: valor analógico original da transição.

O arquivo `EVENTOS.CSV` é conveniente para futuramente importar dados no SQL ou processá-los em AWS, mas **esta versão não faz integração com banco ou nuvem**. O arquivo individual de cada evento é a evidência primária. Se houver falha isolada ao acrescentar o histórico, o firmware mantém o arquivo do evento e registra aviso no serial.

## Inspecionar os dados

Após encerrar a simulação:

```bash
python tools/conferir_csv.py sdcard
```

O script verifica os cabeçalhos, a consistência entre nome de arquivo e identificador e se há divergência entre os arquivos individuais e `EVENTOS.CSV`.

## Testes

A máquina de estados 0/1 fica em `include/TarsStateTracker.h` e não depende do framework Arduino. Com `g++` ou `clang++` disponível:

```bash
python tools/testar_estado.py
```

O teste cobre 0→1→0, debounce, histerese, ausência de confirmação antes da escrita do CSV, tentativa posterior e estouro de `millis()`.

**Limite da validação:** o teste acima valida a lógica de transições. Ainda é necessário compilar com PlatformIO e executar o Wokwi ou hardware físico para validar o firmware completo, montagem SPI, GPS UART e gravação real no microSD.

## Organização

```text
tars-sensores-gps-arroz/
├── src/main.ino                   # firmware ESP32
├── include/TarsStateTracker.h     # logica das transicoes 0/1
├── diagram.json                   # circuito Wokwi (1 potenciometro + microSD)
├── platformio.ini                 # compilacao ESP32 + TinyGPSPlus
├── wokwi.toml                     # simulador + SD writeback
├── sdcard/                        # CSVs do ESP32 apos simulacao
├── tests/test_state_tracker.cpp   # teste da maquina de estados
├── tools/testar_estado.py         # compilacao e execucao do teste
├── tools/conferir_csv.py          # verificacao dos CSVs
└── exemplos/EXEMPLO_EVENTO.csv    # somente ilustrativo
```

## Papel desta solução no TARS

Este firmware é um **protótipo de dispositivo de alerta**. O potenciômetro permite simular uma resposta de modelo que ainda não foi embarcado. Um possível trabalho futuro é converter e validar um modelo apropriado para execução local (*TinyML*) no ESP32, verificando memória, latência e qualidade preditiva; isso **não está implementado** nesta entrega.

Para o contexto acadêmico, os dados NASA POWER/CERES e os indicadores econômicos continuam no pipeline científico do TARS. **O ESP32 desta demonstração não consulta essas fontes**, não executa o classificador KNN/MLP e não consegue estimar o preço do arroz.

## Diferenças em relação ao fleet-force-sensors original

| Versão original | Versão TARS |
|---|---|
| MPU6050 aceleração/giroscópio | Removido |
| Potenciômetro para progresso GPS e outro para velocidade | Um único potenciômetro para classe 0/1 |
| GPS dependente de rota simulada pelo potenciômetro | GPS de referência fixa no Wokwi; GPS UART real opcional |
| Wi-Fi, HTTPS, envio periódico para API | Removidos |
| Telemetria a cada 5 s | Eventos somente em transições estáveis |
| Dados JSON de frota | Um CSV por evento e histórico CSV no microSD |
