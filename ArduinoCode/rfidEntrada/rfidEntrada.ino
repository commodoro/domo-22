#include <SPI.h>
#include <MFRC522.h> //https://github.com/miguelbalboa/rfid
#include "WiFi.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

/* Notas:
    - Habría que añadir Watchdog al RFID
      y reiniciar si se vuelve loco.
*/

// Flags de compilación
// #define EN_LOCK // Descomentar para añadir lock

// Wifi
const char* WIFI_NETWORK = "labce11";
const char* WIFI_PASSWORD = "laboratoriobajocubierta11";
const uint32_t WIFI_TIMEOUT_MS = 20000;
const uint32_t WIFI_RECOVER_TIME_MS = 30000;
void keepWiFiAlive(void * param);

// HTTP
const char* HTTP_API_ACCESS = "http://192.168.11.170:9090/acceso";
const char* CLIENT_NAME = "Sala trabajo en grupo 1";
const char* ROOM = "grupo_1";
#ifdef EN_LOCK
const bool LOCK = true;
#endif

// RFID
const uint8_t SS_PIN = 5;
const uint8_t RST_PIN = 15;
MFRC522 rfid = MFRC522(SS_PIN, RST_PIN);
byte nuidPICC[4] = {0, 0, 0, 0};
MFRC522::MIFARE_Key key;
void readRFID(void *param);
void printHex(byte *buffer, byte bufferSize);

// LEDs
const uint8_t LED_R = 14;
const uint8_t LED_Y = 32;
const uint8_t LED_G = 25;
const uint8_t LED_ONBOARD = 2;

// RELE
const uint8_t RELE = 33;
const bool RLOGIC = true; // true -> logica invertida

// Máquina de estados
namespace sm
{
bool rfid_read = false;
bool open = false;
bool denegate = false;
}

void setup() {
  // Serial Debug
  Serial.begin(115200);

  // RFID
  SPI.begin();
  rfid.PCD_Init();
  rfid.PCD_DumpVersionToSerial();

  // LEDs
  pinMode(LED_R, OUTPUT);
  pinMode(LED_Y, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_ONBOARD, OUTPUT);


  // Rele
  pinMode(RELE, OUTPUT);
  digitalWrite(RELE, !RLOGIC);

  // Tareas
  xTaskCreate(
    readRFID,
    "Lectura RFID",
    10000,           // Stack size (bytes)
    NULL,            // Parameter to pass
    1,               // Task priority
    NULL             // Task handle
  );
  xTaskCreatePinnedToCore(
    keepWiFiAlive,
    "keepWiFiAlive",  // Task name
    5000,             // Stack size (bytes)
    NULL,             // Parameter
    1,                // Task priority
    NULL,             // Task handle
    ARDUINO_RUNNING_CORE
  );
}

void loop() {
  // Máquina de estados
  if (sm::rfid_read)
    digitalWrite(LED_Y, HIGH);
  else
    digitalWrite(LED_Y, LOW);

  if (sm::open)
  {
    digitalWrite(RELE, !RLOGIC);
    digitalWrite(LED_G, HIGH);
  } else {
    digitalWrite(RELE, RLOGIC);
    digitalWrite(LED_G, LOW);
  }

  if (sm::denegate)
    digitalWrite(LED_R, HIGH);
  else
    digitalWrite(LED_R, LOW);
}

void keepWiFiAlive(void * param) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(LED_ONBOARD, HIGH);
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      continue;
    }

    digitalWrite(LED_ONBOARD, LOW);
    Serial.println("[WIFI] Conectando.");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_NETWORK, WIFI_PASSWORD);

    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttemptTime < WIFI_TIMEOUT_MS) {}

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Fallo.");
      vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);
      continue;
    }
    Serial.print("[WIFI] Conectado. IP: ");
    Serial.println(WiFi.localIP());
  }
}

void readRFID(void *param) {
  for (;;) {
    vTaskDelay(100 / portTICK_PERIOD_MS);

    if (sm::rfid_read || sm::open || sm::denegate)
      continue;

    for (byte i = 0; i < 6; i++) {
      key.keyByte[i] = 0xFF;
    }
    // Look for new 1 cards
    if ( ! rfid.PICC_IsNewCardPresent())
      continue;

    // Verify if the NUID has been readed
    if (  !rfid.PICC_ReadCardSerial())
      continue;

    // Store NUID into nuidPICC array
    for (byte i = 0; i < 4; i++) {
      nuidPICC[i] = rfid.uid.uidByte[i];
    }

    sm::rfid_read = true;
    // Lanzo una tarea para la petición http.
    xTaskCreate( // Reinicia la máquina de estados
      consultarAcceso,
      "HTTP Request",
      50000,           // Stack size (bytes)
      NULL,            // Parameter to pass
      1,               // Task priority
      NULL             // Task handle
    );

    Serial.print("[RFID] Detectado (hex): ");
    printHex(rfid.uid.uidByte, rfid.uid.size);
    Serial.println();

    // Halt PICC
    rfid.PICC_HaltA();

    // Stop encryption on PCD
    rfid.PCD_StopCrypto1();
  }
}

void printHex(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}

void consultarAcceso(void * param)
{
  HTTPClient http;
  http.begin(HTTP_API_ACCESS);
  http.addHeader("Content-Type", "application/json");

  // Extraemos código
  String code_access = "";
  for (byte i = 0; i < 4; i++) {
    code_access += nuidPICC[i] > 0x0f ? String(nuidPICC[i], HEX) : String('0') + String(nuidPICC[i], HEX);
    if (i < 3)
      code_access += String('-');
  }

  // JSON
  StaticJsonDocument<100> jsonDoc;
  jsonDoc["code_access"] = code_access;
  jsonDoc["client"] = CLIENT_NAME;
  jsonDoc["room"] = ROOM;
#ifdef EN_LOCK
  jsonDoc["lock"] = LOCK;
#endif

  // Serializar
  String load = "";
  serializeJson(jsonDoc, load);

  // Request
  int httpCode = http.POST(load);
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("[HTTP] Request Exitosa");
      String load = http.getString();
      deserializeJson(jsonDoc, load);
      bool acceso = jsonDoc["acceso"];
      if (acceso)
        sm::open = true;
      else
        sm::denegate = true;
      sm::rfid_read = false;
    }
  } else {
    Serial.printf("[HTTP] Request fallida, error: %s\n", http.errorToString(httpCode).c_str());
    sm::denegate = true;
    // Lanzar tarea de reiniciar máquina de estados
  }

  // Reinicia la máquina de estados
  xTaskCreate(
    resetSM,
    "Reinicia la maquina de estados",
    5000,           // Stack size (bytes)
    NULL,            // Parameter to pass
    1,               // Task priority
    NULL             // Task handle
  );
  http.end();
  vTaskDelete(NULL); // Fin de la tarea
}

void resetSM(void *param)
{
  // Tras 5s apaga las luces
  vTaskDelay(5000 / portTICK_PERIOD_MS);
  sm::open = false;
  sm::denegate = false;
  sm::rfid_read = false;
  vTaskDelete(NULL);
}
