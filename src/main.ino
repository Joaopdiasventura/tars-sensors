
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGPS++.h>
#include <DHT.h>
#include <math.h>

#include "TarsStateTracker.h"

#ifndef TARS_SIMULATED_GPS
#define TARS_SIMULATED_GPS 1
#endif

namespace Config
{
  constexpr char SENSOR_CODIGO[] = "TARS-ESP32-001";

  constexpr uint8_t POT_PIN = 34;
  constexpr uint8_t DHT_PIN = 27;

  constexpr uint8_t LED_NORMAL = 25;
  constexpr uint8_t LED_ATTENTION = 26;
  constexpr uint8_t LED_CRITICAL = 32;

  constexpr uint8_t SD_CS = 5;
  constexpr uint8_t SD_SCK = 18;
  constexpr uint8_t SD_MISO = 19;
  constexpr uint8_t SD_MOSI = 23;

  constexpr uint8_t GPS_RX = 16;
  constexpr uint8_t GPS_TX = 17;

  constexpr uint32_t READ_INTERVAL_MS = 40;

  constexpr uint32_t ENV_INTERVAL_MS = 2000;

  constexpr uint32_t SERIAL_INTERVAL_MS = 5000;

  constexpr uint32_t RETRY_INTERVAL_MS = 1000;

  constexpr double SIMULATED_LAT = -23.550520;
  constexpr double SIMULATED_LON = -46.633308;

  constexpr uint32_t MAX_EVENT_ID = 9999999UL;

  constexpr char EVENTS_PATH[] = "/EVENTOS.CSV";
  constexpr char SESSION_PATH[] = "/SESSAO.TXT";

  constexpr char CSV_HEADER[] =
      "sensor_codigo,"
      "sessao_coleta,"
      "evento,"
      "timestamp_ms,"
      "timestamp_utc,"
      "origem_gps,"
      "gps_valido,"
      "latitude,"
      "longitude,"
      "previsao_alta,"
      "classe,"
      "adc\n";
}

enum AlertLevel : uint8_t
{
  NORMAL = 0,
  ATTENTION = 1,
  CRITICAL = 2
};

struct LocationSnapshot
{
  bool valid;
  double latitude;
  double longitude;
  String utc;
  const char *source;
};

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
DHT dht(Config::DHT_PIN, DHT22);
TarsStateTracker stateTracker;

uint32_t nextEventId = 1;
uint32_t sessionNumber = 0;

uint32_t lastReadMs = 0;
uint32_t lastEnvironmentMs = 0;
uint32_t lastSerialMs = 0;
uint32_t lastPersistAttemptMs = 0;

String sessionCode;

float temperature = NAN;
float humidity = NAN;

bool environmentValid = false;
bool sdReady = false;
bool pendingHistory = false;

String pendingHistoryRow;

AlertLevel alertLevel = ATTENTION;

const char *alertName(AlertLevel level)
{
  switch (level)
  {
  case NORMAL:
    return "NORMAL";

  case ATTENTION:
    return "ATENCAO";

  case CRITICAL:
    return "CRITICO";
  }

  return "DESCONHECIDO";
}

void eventPath(uint32_t eventId, char (&path)[16])
{
  snprintf(
      path,
      sizeof(path),
      "/E%07lu.CSV",
      static_cast<unsigned long>(eventId));
}

bool initializeSession()
{
  uint32_t previousSession = 0;

  if (SD.exists(Config::SESSION_PATH))
  {
    File file = SD.open(
        Config::SESSION_PATH,
        FILE_READ);

    if (!file)
      return false;

    previousSession = file.parseInt();
    file.close();
  }

  if (previousSession == UINT32_MAX)
  {
    return false;
  }

  sessionNumber = previousSession + 1;

  if (SD.exists(Config::SESSION_PATH))
  {
    if (!SD.remove(Config::SESSION_PATH))
    {
      return false;
    }
  }

  File file = SD.open(
      Config::SESSION_PATH,
      FILE_WRITE);

  if (!file)
    return false;

  String value = String(sessionNumber);

  bool success =
      file.print(value) == value.length();

  file.flush();
  file.close();

  if (!success)
    return false;

  sessionCode =
      String(Config::SENSOR_CODIGO) +
      "-S" +
      String(sessionNumber);

  return true;
}

bool prepareSd()
{
  SPI.begin(
      Config::SD_SCK,
      Config::SD_MISO,
      Config::SD_MOSI,
      Config::SD_CS);

  if (!SD.begin(
          Config::SD_CS,
          SPI,
          4000000U))
  {
    return false;
  }

  for (
      uint32_t id = 1;
      id <= Config::MAX_EVENT_ID;
      ++id)
  {

    char path[16];
    eventPath(id, path);

    if (!SD.exists(path))
    {
      nextEventId = id;
      return initializeSession();
    }
  }

  return false;
}

void readGpsBytes()
{
#if !TARS_SIMULATED_GPS
  while (gpsSerial.available() > 0)
  {
    gps.encode(gpsSerial.read());
  }
#endif
}

LocationSnapshot readLocation()
{
#if TARS_SIMULATED_GPS

  return {
      true,
      Config::SIMULATED_LAT,
      Config::SIMULATED_LON,
      "",
      "SIMULADO_WOKWI"};

#else

  LocationSnapshot location = {
      false,
      0.0,
      0.0,
      "",
      "NEO6M_UART"};

  if (
      gps.location.isValid() &&
      gps.location.age() < 5000)
  {

    location.valid = true;
    location.latitude = gps.location.lat();
    location.longitude = gps.location.lng();
  }

  if (
      gps.date.isValid() &&
      gps.time.isValid() &&
      gps.date.age() < 5000 &&
      gps.time.age() < 5000)
  {

    char buffer[25];

    snprintf(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02uT%02u:%02u:%02uZ",
        gps.date.year(),
        gps.date.month(),
        gps.date.day(),
        gps.time.hour(),
        gps.time.minute(),
        gps.time.second());

    location.utc = buffer;
  }

  return location;

#endif
}

void readEnvironment()
{
  float newTemperature = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  if (
      isnan(newTemperature) ||
      isnan(newHumidity))
  {

    environmentValid = false;

    Serial.println(
        "AVISO: leitura DHT22 invalida.");

    return;
  }

  temperature = newTemperature;
  humidity = newHumidity;

  environmentValid = true;
}
AlertLevel classifyAlert(int prediction)
{
  if (!environmentValid)
  {
    return ATTENTION;
  }

  const bool severeEnvironment =
      temperature >= 35.0f ||
      humidity >= 85.0f ||
      humidity <= 25.0f;

  const bool moderateEnvironment =
      temperature >= 30.0f ||
      humidity >= 70.0f ||
      humidity <= 35.0f;

  if (prediction == 1 && severeEnvironment)
  {
    return CRITICAL;
  }

  if (
      prediction == 1 ||
      severeEnvironment ||
      moderateEnvironment)
  {

    return ATTENTION;
  }

  return NORMAL;
}

void updateActuators()
{
  digitalWrite(
      Config::LED_NORMAL,
      alertLevel == NORMAL ? HIGH : LOW);

  digitalWrite(
      Config::LED_ATTENTION,
      alertLevel == ATTENTION ? HIGH : LOW);

  digitalWrite(
      Config::LED_CRITICAL,
      alertLevel == CRITICAL ? HIGH : LOW);
}

void refreshAlert()
{
  AlertLevel newLevel =
      classifyAlert(stateTracker.state());

  if (newLevel != alertLevel)
  {
    alertLevel = newLevel;

    Serial.print("ALERTA_LOCAL:");
    Serial.println(alertName(alertLevel));
  }

  updateActuators();
}

String buildCsvRow(
    uint32_t eventId,
    uint32_t elapsedMs,
    uint16_t adc,
    int modelOutput,
    const LocationSnapshot &location)
{

  String row;
  row.reserve(240);

  row += Config::SENSOR_CODIGO;
  row += ',';

  row += sessionCode;
  row += ',';

  row += String(eventId);
  row += ',';

  row += String(elapsedMs);
  row += ',';

  row += location.utc;
  row += ',';

  row += location.source;
  row += ',';

  row += location.valid ? "1" : "0";
  row += ',';

  if (location.valid)
  {
    row += String(location.latitude, 6);
  }

  row += ',';

  if (location.valid)
  {
    row += String(location.longitude, 6);
  }

  row += ',';

  row += String(modelOutput);
  row += ',';

  row += modelOutput == 1
             ? "ALTA"
             : "NAO_ALTA";

  row += ',';

  row += String(adc);
  row += '\n';

  return row;
}

bool writeEventFile(
    uint32_t eventId,
    const String &row)
{

  char path[16];
  eventPath(eventId, path);

  if (SD.exists(path))
  {
    Serial.println(
        "ERRO: arquivo de evento ja existente.");

    return false;
  }

  File file = SD.open(path, FILE_WRITE);

  if (!file)
    return false;

  bool headerOk =
      file.print(Config::CSV_HEADER) ==
      strlen(Config::CSV_HEADER);

  bool rowOk =
      file.print(row) == row.length();

  file.flush();
  file.close();

  if (!headerOk || !rowOk)
  {
    SD.remove(path);
    return false;
  }

  Serial.print("CSV salvo: ");
  Serial.println(path);

  return true;
}

bool appendHistory(const String &row)
{
  bool newFile =
      !SD.exists(Config::EVENTS_PATH);

  File history = SD.open(
      Config::EVENTS_PATH,
      FILE_APPEND);

  if (!history)
    return false;

  bool success = true;

  if (newFile)
  {
    success =
        history.print(Config::CSV_HEADER) ==
        strlen(Config::CSV_HEADER);
  }

  if (success)
  {
    success =
        history.print(row) == row.length();
  }

  history.flush();
  history.close();

  return success;
}

void printStatus()
{
  LocationSnapshot location = readLocation();

  Serial.println();
  Serial.println("========== TARS ==========");

  Serial.print("sensor_codigo=");
  Serial.println(Config::SENSOR_CODIGO);

  Serial.print("sessao_coleta=");
  Serial.println(sessionCode);

  Serial.print("timestamp_ms=");
  Serial.println(millis());

  Serial.print("previsao_alta=");
  Serial.println(stateTracker.state());

  Serial.print("classe=");
  Serial.println(
      stateTracker.state() == 1
          ? "ALTA"
          : "NAO_ALTA");

  Serial.print("nivel_alerta=");
  Serial.println(alertName(alertLevel));

  Serial.print("temperatura_c=");
  if (environmentValid)
  {
    Serial.println(temperature, 2);
  }
  else
  {
    Serial.println("INDISPONIVEL");
  }

  Serial.print("umidade_pct=");
  if (environmentValid)
  {
    Serial.println(humidity, 2);
  }
  else
  {
    Serial.println("INDISPONIVEL");
  }

  Serial.print("origem_gps=");
  Serial.println(location.source);

  Serial.print("gps_valido=");
  Serial.println(location.valid ? 1 : 0);

  if (location.valid)
  {
    Serial.print("latitude=");
    Serial.println(location.latitude, 6);

    Serial.print("longitude=");
    Serial.println(location.longitude, 6);
  }

  Serial.print("sd_disponivel=");
  Serial.println(sdReady ? 1 : 0);

  Serial.println("==========================");
  Serial.println();
}

bool persistTransition(
    uint16_t adc,
    uint32_t nowMs)
{

  if (!sdReady)
    return false;

  if (nextEventId > Config::MAX_EVENT_ID)
  {
    Serial.println("ERRO: limite de eventos.");
    return false;
  }

  int modelOutput =
      stateTracker.pendingState();

  if (modelOutput != 0 && modelOutput != 1)
  {
    return false;
  }

  LocationSnapshot location =
      readLocation();

  String row = buildCsvRow(
      nextEventId,
      nowMs,
      adc,
      modelOutput,
      location);

  if (!writeEventFile(nextEventId, row))
  {
    return false;
  }

  if (!appendHistory(row))
  {
    Serial.println(
        "AVISO: historico nao atualizado.");

    pendingHistory = true;
    pendingHistoryRow = row;
  }

  Serial.print("EVENTO_CONFIRMADO:");
  Serial.println(nextEventId);

  Serial.print("CSV_ROW:");
  Serial.print(row);

  ++nextEventId;

  return true;
}

void retryHistory()
{
  if (!sdReady || !pendingHistory)
  {
    return;
  }

  if (appendHistory(pendingHistoryRow))
  {
    pendingHistory = false;
    pendingHistoryRow = "";

    Serial.println(
        "HISTORICO_RECUPERADO");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(150);

  analogReadResolution(12);

  pinMode(Config::POT_PIN, INPUT);

  pinMode(Config::LED_NORMAL, OUTPUT);
  pinMode(Config::LED_ATTENTION, OUTPUT);
  pinMode(Config::LED_CRITICAL, OUTPUT);

  digitalWrite(Config::LED_NORMAL, LOW);
  digitalWrite(Config::LED_ATTENTION, LOW);
  digitalWrite(Config::LED_CRITICAL, LOW);

  dht.begin();

#if !TARS_SIMULATED_GPS

  gpsSerial.begin(
      9600,
      SERIAL_8N1,
      Config::GPS_RX,
      Config::GPS_TX);

#endif

  Serial.println();
  Serial.println("TARS | AICSS Global Solution");
  Serial.println("ESP32 | CitizenTech");
  Serial.println("Modelo de IA: SIMULADO");
  Serial.println("Alimento: arroz");
  Serial.println();

  sdReady = prepareSd();

  if (!sdReady)
  {
    Serial.println(
        "AVISO: microSD indisponivel.");
    Serial.println(
        "Alertas locais continuam ativos.");
  }
  else
  {
    Serial.print("Sessao: ");
    Serial.println(sessionCode);

    Serial.print("Proximo evento: ");
    Serial.println(nextEventId);
  }

#if TARS_SIMULATED_GPS

  Serial.println(
      "GPS: coordenadas ficticias do Wokwi.");

#else

  Serial.println(
      "GPS: modulo NEO-6M via UART2.");

#endif

  uint32_t now = millis();

  uint16_t initialAdc =
      analogRead(Config::POT_PIN);

  stateTracker.begin(initialAdc, now);

  readEnvironment();
  refreshAlert();

  lastReadMs = now;
  lastEnvironmentMs = now;
  lastSerialMs = now;

  Serial.println(
      "ADC: leitura a cada 40 ms.");

  Serial.println(
      "DHT22: leitura a cada 2000 ms.");

  Serial.println(
      "Serial: estado a cada 5000 ms.");

  Serial.println(
      "Debounce da classificacao: 200 ms.");

  Serial.println(
      "O estado inicial nao gera CSV.");

  printStatus();
}

void loop()
{
  readGpsBytes();

  uint32_t now = millis();

  if (
      static_cast<uint32_t>(
          now - lastEnvironmentMs) >=
      Config::ENV_INTERVAL_MS)
  {

    lastEnvironmentMs = now;

    readEnvironment();
    refreshAlert();
  }

  if (
      static_cast<uint32_t>(
          now - lastReadMs) >=
      Config::READ_INTERVAL_MS)
  {

    lastReadMs = now;

    uint16_t adc =
        analogRead(Config::POT_PIN);

    bool ready =
        stateTracker.transitionReady(adc, now);

    if (ready)
    {
      bool canRetry =
          lastPersistAttemptMs == 0 ||
          static_cast<uint32_t>(
              now - lastPersistAttemptMs) >=
              Config::RETRY_INTERVAL_MS;

      if (canRetry)
      {
        lastPersistAttemptMs = now;

        if (persistTransition(adc, now))
        {
          stateTracker.confirmPersisted();

          lastPersistAttemptMs = 0;

          refreshAlert();
          printStatus();
        }
        else
        {
          Serial.println(
              "Falha ao salvar CSV. "
              "Nova tentativa em 1 segundo.");
        }
      }
    }
    else
    {
      lastPersistAttemptMs = 0;
    }
  }

  if (
      static_cast<uint32_t>(
          now - lastSerialMs) >=
      Config::SERIAL_INTERVAL_MS)
  {

    lastSerialMs = now;

    printStatus();
  }
}
