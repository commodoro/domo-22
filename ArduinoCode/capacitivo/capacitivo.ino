#include "WiFi.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi
const char* WIFI_NETWORK = "labce11";
const char* WIFI_PASSWORD = "laboratoriobajocubierta11";
const uint32_t WIFI_TIMEOUT_MS = 20000;
const uint32_t WIFI_RECOVER_TIME_MS = 30000;
void keepWiFiAlive(void * param);

// Web
const uint16_t ID = 0x01;
const char* HTTP_API_ACCESS = "http://192.168.1.247:8080/chair";

// Salidas
const uint8_t LED = 2;

// Sensor capacitivo
int touchValue;
uint8_t TOUCH = 33; // Pin
void leer(void* param);
int threshold = 50;
bool activo;
void autocal();

void setup() {
  Serial.begin(115200);
  pinMode (LED, OUTPUT);

  // Autocal
  autocal();
  Serial.printf("Threshold=%i\n", threshold);

  // Rutina de lectura del sensor
  xTaskCreate(
    leer,
    "Lectura sensor capacitivo",
    10000,           // Stack size (bytes)
    NULL,            // Parameter to pass
    1,               // Task priority
    NULL             // Task handle
  );

  // Rutina supervisor WiFi.
  xTaskCreatePinnedToCore(
    keepWiFiAlive,
    "keepWiFiAlive",  // Task name
    10000,             // Stack size (bytes)
    NULL,             // Parameter
    1,                // Task priority
    NULL,             // Task handle
    ARDUINO_RUNNING_CORE
  );
}

void loop()
{
  if (activo) {
    digitalWrite(LED, HIGH);
  }
  else {
    digitalWrite(LED, LOW);
  }
}

void leer(void* param) {
  // Realizamos la media de 20 valores para evitar medidas atípicas.
  int vector[20]; 
  for (auto &item : vector) item = 100;
  for (int n = 0;; ++n)
  {
    int mean = 0;
    n = (n == 20) ? 0 : n;

    // Lectura
    touchValue = touchRead(TOUCH);
    Serial.printf("[Sensor] Lectura: %i", touchValue);
    vector[n] = touchValue;

    // Calculo la media
    for (int i = 0; i < 20; ++i)
    {
      mean += vector[i];
    }
    mean /= 20;
    Serial.printf(" - Media: %i\n", mean);

    // Compruebo el umbral y envío el recurso
    if (mean < threshold && !activo)
    {
      activo = true;
      xTaskCreate( // CS
        peticion,
        "HTTP REQUEST 1",
        20000,           // Stack size (bytes)
        NULL,            // Parameter to pass
        1,               // Task priority
        NULL             // Task handle
      );
      vTaskDelay(5000 / portTICK_PERIOD_MS); // 5s enfriamiento
    } else if (mean > threshold && activo){
      activo = false;
      xTaskCreate( // CS
        peticion,
        "HTTP REQUEST 2",
        10000,           // Stack size (bytes)
        NULL,            // Parameter to pass
        1,               // Task priority
        NULL             // Task handle
      );
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void autocal()
{
  int suma = 0;
  for (int i = 0; i < 20; ++i)
  {
    suma += touchRead(TOUCH);
    delay(100);
  }
  threshold = suma / 20 - 3;
}

void peticion(void* param)
{
  // Creamos el cliente
  HTTPClient http;
  http.begin(HTTP_API_ACCESS);
  http.addHeader("Content-Type", "application/json");
  
  // JSON
  StaticJsonDocument<100> jsonDoc;
  jsonDoc["id"] = ID;
  jsonDoc["estado"] = activo;

  // Serializamos
  String load = "";
  serializeJson(jsonDoc, load);

  // Request
  int httpCode = http.POST(load);
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("[HTTP] Request Exitosa");
    } else {
      Serial.printf("[HTTP] Request fallida, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
  vTaskDelete(NULL); // Fin de la tarea.
}

void keepWiFiAlive(void * param) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      continue;
    }

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
