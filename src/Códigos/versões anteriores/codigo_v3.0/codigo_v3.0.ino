// Inclui bibliotecas para o conversor HX711 e LCD
#include "HX711.h"
#include <LiquidCrystal_I2C.h>

// Inclui bibliotecas para o sensor de temperatura Adafruit BME280
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// Inclui bibliotecas para utilizar MQTT
#include <WiFi.h>
#include <PubSubClient.h>

// Define os pinos e cria instâncias para o LCD e o sensor HX711
LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 scale;
Adafruit_BME280 bme;
WiFiClient espClient;
PubSubClient client(espClient);

// Declaração de variáveis globais e pinos
bool buttonPressed = false;
bool tareButtonPressed = false;
const int tareButton = 35;
const int button = 34;

// Enumeração para estados do LED e buzzer
enum State { LED_BUZZER_OFF, BLINKING, LED_ON_BUZZER_OFF };
State state = LED_BUZZER_OFF;

// Classe para o sistema de medição de peso
class WeightMeasurementSystem {
public:
  float gravity = 9.8;
  float weightBalance;
  float newtonBalance;
  const int redPin = 12;
  const int greenPin = 14;
  const int bluePin = 27;
  const int buzzer = 26;
  int alertLimit;
  unsigned long previousTime = 0;
  const long halfSecond = 500;

  // Construtor
  WeightMeasurementSystem() {
    scale.begin(2, 4); // Inicia a escala HX711
    pinMode(tareButton, INPUT);
    pinMode(redPin, OUTPUT);
    pinMode(greenPin, OUTPUT);
    pinMode(bluePin, OUTPUT);
    pinMode(buzzer, OUTPUT);
    pinMode(button, INPUT);
  }

  // Método para converter peso em Newton
  void convertWeightToNewton(float weightBalance) {
    newtonBalance = weightBalance * gravity;
  }

  // Método para verificar limite de peso
  void checkWeightLimit() {
    switch (state) {
      case LED_BUZZER_OFF:
        if (newtonBalance < alertLimit) {
          digitalWrite(redPin, HIGH);
          digitalWrite(greenPin, LOW);
          digitalWrite(bluePin, HIGH);
          noTone(buzzer);
          if (buttonPressed == true) {
            buttonPressed = false;
            Serial.println("Botão liberado");
          }
        } else {
          state = BLINKING;
        }
        break;
      case BLINKING:
        if (newtonBalance >= alertLimit && millis() - previousTime > halfSecond) {
          digitalWrite(greenPin, HIGH);
          digitalWrite(bluePin, HIGH);
          digitalWrite(redPin, !digitalRead(redPin));
          tone(buzzer, digitalRead(redPin) ? 500 : 0);
          previousTime = millis();
          if (buttonPressed == true) {
            state = LED_ON_BUZZER_OFF;
            Serial.println("Botão Pressionado");
          }
        } else {
          state = LED_BUZZER_OFF;
        }
        break;
      case LED_ON_BUZZER_OFF:
        if (newtonBalance >= alertLimit && buttonPressed == true) {
          digitalWrite(redPin, LOW);
          digitalWrite(greenPin, HIGH);
          digitalWrite(bluePin, HIGH);
          noTone(buzzer);
        } else {
          state = LED_BUZZER_OFF;
        }
        break;
    }
  }

  // Método para obter peso
  float getWeight() {
    return scale.get_units();
  }

  // Método para configurar tara
  void setTare() {
    if (tareButtonPressed == true) {
      Serial.println("Função da tara ativada");
      digitalWrite(greenPin, LOW);
      digitalWrite(redPin, LOW);
      digitalWrite(bluePin, HIGH);
      noTone(buzzer);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Coloque o peso");
      lcd.setCursor(0, 1);
      lcd.print("para tara.");
      delay(4000);
      scale.tare();
      tareButtonPressed = false;
      lcd.clear();
    }
  }
};

// Instancia um objeto da classe WeightMeasurementSystem
WeightMeasurementSystem wms;

// Define constantes para MQTT e Wi-Fi
#define WIFI_SSID "Inteli-welcome"
#define WIFI_PASSWORD ""
#define TOKEN "BBUS-agsiaYL4A0fkNXLYtHQe8Td3YLcgQu"
#define DEVICE_LABEL "esp32"

// Define o tópico MQTT para o limite de alerta
const char* awareLimit_topic = "/v1.6/devices/esp32/awareLimit/lv";

// Funções para operações de MQTT, sensor de temperatura, etc.
void detectionSensorTemp() {
  if (!bme.begin(0x76)) {
    Serial.println("Não foi possível encontrar o sensor BME280. Verifique as conexões!");
    while (1);
  }
}

// Conecta ao WiFi e ao servidor MQTT
void connectionWifiAndMqtt() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando ao WiFi...");
  }
  Serial.println("Conectado ao WiFi");
  client.connect("ESP32", TOKEN, "");
  Serial.println("Conectado ao servidor MQTT");
  client.setServer("industrial.api.ubidots.com", 1883);
  client.setCallback(callback);
}

// Reconecta ao servidor MQTT se necessário
void reconnectMqtt() {
  while (!client.connected()) {
    Serial.println("Tentando se reconectar ao MQTT...");
    if (client.connect("ESP32", TOKEN, "")) {
      Serial.println("Conectado ao servidor MQTT");
      client.subscribe(awareLimit_topic);
    } else {
      Serial.print("Falha na reconexão. Código de erro = ");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// Lê e publica a temperatura no servidor MQTT
void getAndPostTemperature() {
  float getTemperature = bme.readTemperature();
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"temperature\": %.2f}", getTemperature);
  client.publish(topic, payload);
}

// Lê e publica o peso no servidor MQTT
void weight() {
  float getWeight = wms.getWeight();
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"weight\": %.2f}", getWeight);
  client.publish(topic, payload);
}

// Callback para mensagens MQTT recebidas
void callback(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  strncpy(msg, (char*)payload, length);
  msg[length] = '\0';
  if (strcmp(topic, awareLimit_topic) == 0) {
    bool state = atoi(msg) == 1;
    Serial.print("Estado de alerta: ");
    Serial.println(state);
  }
}

// Configurações iniciais
void setup() {
  Serial.begin(115200);
  detectionSensorTemp();
  connectionWifiAndMqtt();
  scale.set_scale(210514.50567261);
  delay(2000);
  scale.tare();
  lcd.init();
  lcd.backlight();
  buttonPressed = false;
  Serial.println("Qual o limite de força em Newtons suportado?");
  while (Serial.available() == 0) {}
  wms.alertLimit = Serial.parseInt();
  Serial.print("Limite de alerta configurado para: ");
  Serial.println(wms.alertLimit);
}

// Loop principal
void loop() {
  if (!client.connected()) {
    reconnectMqtt();
  }
  client.loop();
  getAndPostTemperature();
  weight();
  float weightBalance = wms.getWeight();
  
  // Verifica se o limite de peso foi excedido
  if (wms.newtonBalance >= wms.alertLimit && digitalRead(button)) {
    buttonPressed = true;
  }

  // Executa a tara quando necessário
  if (digitalRead(tareButton)) {
    tareButtonPressed = true;
    Serial.println("Tara pressionada");
  }

  wms.convertWeightToNewton(weightBalance);
  float temperature = bme.readTemperature();
  Serial.println(String(temperature) + "°C " + String(weightBalance) + "kg " + String(wms.newtonBalance) + "N");
  lcd.setCursor(0, 0);
  lcd.print(String(temperature) + " graus");
  lcd.setCursor(0, 1);
  lcd.print(String(weightBalance) + "kg " + String(wms.newtonBalance) + "N");
  wms.checkWeightLimit();
  wms.setTare();
  delay(10);
}