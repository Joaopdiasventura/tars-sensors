
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGPS++.h>

#include "TarsStateTracker.h"

#ifndef TARS_SIMULATED_GPS
#define TARS_SIMULATED_GPS 1
#endif

namespace Config
{
  constexpr char SENSOR_CODIGO[] = "TARS-ESP32-001";

  constexpr uint8_t POT_PIN = 34;

  constexpr uint8_t SD_CS = 5;
  constexpr uint8_t SD_SCK = 18;
  constexpr uint8_t SD_MISO = 19;
  constexpr uint8_t SD_MOSI = 23;

  constexpr uint8_t GPS_RX = 16;
  constexpr uint8_t GPS_TX = 17;

  constexpr uint32_t READ_INTERVAL_MS = 40;
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

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
TarsStateTracker stateTracker;

uint32_t nextEventId = 1;
uint32_t lastReadMs = 0;
uint32_t lastPersistAttemptMs = 0;
uint32_t sessionNumber = 0;

String sessionCode;

struct LocationSnapshot
{
  bool valid;
  double latitude;
  double longitude;
  String utc;
  const char *source;
};

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
    File file = SD.open(Config::SESSION_PATH, FILE_READ);

    if (!file)
      return false;

    previousSession = file.parseInt();
    file.close();
  }

  if (previousSession == UINT32_MAX)
    return false;

  sessionNumber = previousSession + 1;

  File file = SD.open(Config::SESSION_PATH, FILE_WRITE);

  if (!file)
    return false;

  const String value = String(sessionNumber);

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

  if (!SD.begin(Config::SD_CS, SPI, 4000000U))
    return false;

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
    row += String(location.latitude, 6);

  row += ',';

  if (location.valid)
    row += String(location.longitude, 6);

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

  const size_t headerLength =
      strlen(Config::CSV_HEADER);

  const bool headerOk =
      file.print(Config::CSV_HEADER) ==
      headerLength;

  const bool rowOk =
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
  const bool newFile =
      !SD.exists(Config::EVENTS_PATH);

  File history = SD.open(
      Config::EVENTS_PATH,
      FILE_APPEND);

  if (!history)
  {
    Serial.println(
        "AVISO: historico indisponivel.");

    return false;
  }

  bool success = true;

  if (newFile)
    success =
        history.print(Config::CSV_HEADER) ==
        strlen(Config::CSV_HEADER);

  if (success)
    success =
        history.print(row) == row.length();

  history.flush();
  history.close();

  if (!success)
    Serial.println(
        "AVISO: falha ao atualizar EVENTOS.CSV.");

  return success;
}

bool persistTransition(
    uint16_t adc,
    uint32_t nowMs)
{
  if (nextEventId > Config::MAX_EVENT_ID)
  {
    Serial.println(
        "ERRO: limite de eventos atingido.");

    return false;
  }

  const int modelOutput =
      stateTracker.pendingState();

  if (modelOutput != 0 && modelOutput != 1)
  {
    Serial.println(
        "ERRO: resultado invalido do modelo.");

    return false;
  }

  const LocationSnapshot location =
      readLocation();

  const String row = buildCsvRow(
      nextEventId,
      nowMs,
      adc,
      modelOutput,
      location);

  if (!writeEventFile(nextEventId, row))
    return false;

  const bool historySaved = appendHistory(row);

  Serial.println();
  Serial.println("========== TARS ==========");

  Serial.print("Sensor: ");
  Serial.println(Config::SENSOR_CODIGO);

  Serial.print("Sessao: ");
  Serial.println(sessionCode);

  Serial.print("Evento: ");
  Serial.println(nextEventId);

  Serial.print("Alimento: ");
  Serial.println(
      "Definido pelo monitoramento no MySQL");

  Serial.print("Modelo: ");
  Serial.println("SIMULADO_POTENCIOMETRO");

  Serial.print("Previsao: ");
  Serial.println(
      modelOutput == 1
          ? "ALTA"
          : "NAO_ALTA");

  Serial.print("ADC: ");
  Serial.println(adc);

  Serial.print("GPS: ");
  Serial.println(location.source);

  Serial.print("GPS valido: ");
  Serial.println(location.valid ? 1 : 0);

  if (location.valid)
  {
    Serial.print("Latitude: ");
    Serial.println(location.latitude, 6);

    Serial.print("Longitude: ");
    Serial.println(location.longitude, 6);
  }

  Serial.print("UTC: ");
  Serial.println(
      location.utc.length() > 0
          ? location.utc
          : "INDISPONIVEL");

  Serial.print("Historico atualizado: ");
  Serial.println(historySaved ? "SIM" : "NAO");

  Serial.print("CSV_ROW:");
  Serial.print(row);

  Serial.println("==========================");
  Serial.println();

  ++nextEventId;

  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(150);

  analogReadResolution(12);
  pinMode(Config::POT_PIN, INPUT);

#if !TARS_SIMULATED_GPS

  gpsSerial.begin(
      9600,
      SERIAL_8N1,
      Config::GPS_RX,
      Config::GPS_TX);

#endif

  Serial.println();
  Serial.println("TARS | Sensor IoT de previsao");
  Serial.println("-----------------------------");

  Serial.print("Codigo do sensor: ");
  Serial.println(Config::SENSOR_CODIGO);

#if TARS_SIMULATED_GPS

  Serial.println(
      "GPS: SIMULADO_WOKWI");

  Serial.println(
      "Coordenadas simuladas; nao representam um fix real.");

#else

  Serial.println(
      "GPS: NEO6M_UART");

#endif

  if (!prepareSd())
  {
    Serial.println(
        "ERRO FATAL: falha ao preparar microSD ou sessao.");

    while (true)
      delay(1000);
  }

  Serial.print("Sessao de coleta: ");
  Serial.println(sessionCode);

  Serial.print("Proximo evento: ");
  Serial.println(nextEventId);

  const uint16_t initialAdc =
      analogRead(Config::POT_PIN);

  stateTracker.begin(
      initialAdc,
      millis());

  lastReadMs = millis();

  Serial.print("Estado inicial: ");
  Serial.println(stateTracker.state());

  Serial.println(
      "Estado inicial nao gera evento.");

  Serial.println(
      "Altere o potenciometro e mantenha por 200ms.");

  Serial.println(
      "Cada mudanca gera um CSV individual.");

  Serial.println(
      "EVENTOS.CSV acumula as medicoes.");

  Serial.println(
      "CSV_ROW pode ser consumido pelo backend.");

  Serial.println();
}

void loop()
{
  readGpsBytes();

  const uint32_t nowMs = millis();

  if (
      static_cast<uint32_t>(
          nowMs - lastReadMs) <
      Config::READ_INTERVAL_MS)
    return;

  lastReadMs = nowMs;

  const uint16_t adc =
      analogRead(Config::POT_PIN);

  if (!stateTracker.transitionReady(adc, nowMs))
    return;

  if (
      lastPersistAttemptMs != 0 &&
      static_cast<uint32_t>(
          nowMs - lastPersistAttemptMs) <
          Config::RETRY_INTERVAL_MS)

    return;

  lastPersistAttemptMs = nowMs;

  if (persistTransition(adc, nowMs))
  {
    stateTracker.confirmPersisted();
    lastPersistAttemptMs = 0;
  }
  else
  {
    Serial.println(
        "ERRO: evento nao persistido. "
        "Nova tentativa em 1 segundo.");
  }
}
