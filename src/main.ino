#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGPS++.h>

#include "TarsStateTracker.h"

#ifndef TARS_SIMULATED_GPS
#endif

namespace Config
{
  constexpr uint8_t POT_PIN = 34;
  constexpr uint8_t SD_CS = 5;
  constexpr uint8_t SD_SCK = 18;
  constexpr uint8_t SD_MISO = 19;
  constexpr uint8_t SD_MOSI = 23;
  constexpr uint8_t GPS_RX = 16;
  constexpr uint8_t GPS_TX = 17;
  constexpr uint32_t READ_INTERVAL_MS = 40;
  constexpr uint32_t RETRY_INTERVAL_MS = 1000;
  constexpr float SIMULATED_LAT = -23.550520f;
  constexpr float SIMULATED_LON = -46.633308f;
  constexpr uint32_t MAX_EVENT_ID = 9999999UL;
}

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
TarsStateTracker stateTracker;
uint32_t nextEventId = 1;
uint32_t lastReadMs = 0;
uint32_t lastPersistAttemptMs = 0;

struct LocationSnapshot
{
  bool valid;
  double latitude;
  double longitude;
  String utc;
  const char *source;
};

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
  return {true, Config::SIMULATED_LAT, Config::SIMULATED_LON,
          "", "SIMULADO_WOKWI"};
#else
  LocationSnapshot location = {false, 0.0, 0.0, "", "NEO6M_UART"};
  if (gps.location.isValid() && gps.location.age() < 5000)
  {
    location.valid = true;
    location.latitude = gps.location.lat();
    location.longitude = gps.location.lng();
  }
  if (gps.date.isValid() && gps.time.isValid() &&
      gps.date.age() < 5000 && gps.time.age() < 5000)
  {
    char buffer[25];
    snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    location.utc = buffer;
  }
  return location;
#endif
}

void eventPath(uint32_t eventId, char (&path)[16])
{

  snprintf(path, sizeof(path), "/E%07lu.CSV", static_cast<unsigned long>(eventId));
}

bool prepareSd()
{
  SPI.begin(Config::SD_SCK, Config::SD_MISO, Config::SD_MOSI, Config::SD_CS);
  if (!SD.begin(Config::SD_CS, SPI, 4000000U))
    return false;

  for (uint32_t id = 1; id <= Config::MAX_EVENT_ID; ++id)
  {
    char path[16];
    eventPath(id, path);
    if (!SD.exists(path))
    {
      nextEventId = id;
      return true;
    }
  }
  return false;
}

String buildCsvRow(uint32_t id, uint32_t elapsedMs, uint16_t adc,
                   int modelOutput, const LocationSnapshot &location)
{

  String row;
  row.reserve(180);
  row += String(id);
  row += ',';
  row += String(elapsedMs);
  row += ',';
  row += location.utc;
  row += ',';
  row += location.source;
  row += ',';
  row += (location.valid ? "1" : "0");
  row += ',';
  if (location.valid)
    row += String(location.latitude, 6);
  row += ',';
  if (location.valid)
    row += String(location.longitude, 6);
  row += ',';
  row += String(modelOutput);
  row += ',';
  row += (modelOutput == 1 ? "ALTA" : "NAO_ALTA");
  row += ',';
  row += String(adc);
  row += '\n';
  return row;
}

bool writeEventFile(uint32_t id, const String &row)
{
  char path[16];
  eventPath(id, path);
  if (SD.exists(path))
  {
    Serial.println("ERRO: ID ja existente; o evento nao sera sobrescrito.");
    return false;
  }

  File file = SD.open(path, FILE_WRITE);
  if (!file)
    return false;

  static const char *HEADER =
      "evento,timestamp_ms,timestamp_utc,origem_gps,gps_valido,latitude,longitude,previsao_alta,classe,adc\n";
  const size_t headerLength = strlen(HEADER);
  bool ok = file.print(HEADER) == headerLength;
  ok = file.print(row) == row.length() && ok;
  file.flush();
  file.close();

  if (!ok)
  {

    SD.remove(path);
    return false;
  }
  Serial.print("CSV salvo no microSD: ");
  Serial.println(path);
  return true;
}

void appendHistory(const String &row)
{
  const char *path = "/EVENTOS.CSV";
  bool newFile = !SD.exists(path);
  File history = SD.open(path, FILE_APPEND);
  if (!history)
  {
    Serial.println("AVISO: evento salvo individualmente, mas historico nao foi aberto.");
    return;
  }
  if (newFile)
    history.print("evento,timestamp_ms,timestamp_utc,origem_gps,gps_valido,latitude,longitude,previsao_alta,classe,adc\n");

  size_t written = history.print(row);
  history.flush();
  history.close();
  if (written != row.length())
    Serial.println("AVISO: falha ao anexar ao EVENTOS.CSV; arquivo individual preservado.");
}

bool persistTransition(uint16_t adc, uint32_t nowMs)
{
  if (nextEventId > Config::MAX_EVENT_ID)
  {
    Serial.println("ERRO: limite de IDs de eventos atingido.");
    return false;
  }
  const int newState = stateTracker.pendingState();
  const LocationSnapshot location = readLocation();
  const String row = buildCsvRow(nextEventId, nowMs, adc, newState, location);
  if (!writeEventFile(nextEventId, row))
    return false;

  appendHistory(row);
  Serial.print("EVENTO TARS: modelo_simulado=");
  Serial.print(newState);
  Serial.print(newState == 1 ? " (alta)" : " (nao alta)");
  Serial.print(" | GPS=");
  Serial.print(location.source);
  Serial.print(" | valid=");
  Serial.println(location.valid ? "1" : "0");
  Serial.print("CSV_ROW:");
  Serial.print(row);
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
  gpsSerial.begin(9600, SERIAL_8N1, Config::GPS_RX, Config::GPS_TX);
#endif
  Serial.println("TARS | GPS + saida simulada do modelo (0/1)");
#if TARS_SIMULATED_GPS
  Serial.println("GPS SIMULADO: coordenadas de referencia; nao indicam posicao real.");
#else
  Serial.println("GPS FISICO: NEO-6M UART2 (GPIO16/17). Fix pode demorar.");
#endif

  if (!prepareSd())
  {
    Serial.println("ERRO FATAL: cartao microSD indisponivel ou cheio. Nenhum evento sera descartado silenciosamente.");
    while (true)
      delay(1000);
  }
  const uint16_t initialAdc = analogRead(Config::POT_PIN);
  stateTracker.begin(initialAdc, millis());
  lastReadMs = millis();
  Serial.print("Estado inicial (nao gera CSV): ");
  Serial.println(stateTracker.state());
  Serial.println("Mova o potenciometro para o lado oposto e mantenha por 200ms.");
  Serial.println("Geracao: 1 CSV por mudanca + EVENTOS.CSV acumulado.");
}

void loop()
{
  readGpsBytes();
  const uint32_t nowMs = millis();
  if (static_cast<uint32_t>(nowMs - lastReadMs) < Config::READ_INTERVAL_MS)
    return;
  lastReadMs = nowMs;

  const uint16_t adc = analogRead(Config::POT_PIN);
  if (!stateTracker.transitionReady(adc, nowMs))
    return;
  if (lastPersistAttemptMs != 0 &&
      static_cast<uint32_t>(nowMs - lastPersistAttemptMs) < Config::RETRY_INTERVAL_MS)
    return;
  lastPersistAttemptMs = nowMs;

  if (persistTransition(adc, nowMs))
  {
    stateTracker.confirmPersisted();
    lastPersistAttemptMs = 0;
  }
  else
    Serial.println("ERRO: nao foi possivel gerar CSV. Estado nao confirmado; nova tentativa em 1s.");
}
