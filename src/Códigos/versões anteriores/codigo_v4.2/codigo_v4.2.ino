//Inclui bibliotecas para o conversor HX711 e do LCD
#include "HX711.h"
#include <LiquidCrystal_I2C.h>

//Inclui bibliotecas para o sensor de temperatura Adafruit BME280
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// Inclui bibliotecas para utilizar MQTT
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Cria instâncias
HX711 scale; 
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_BME280 bme;
WiFiClient espClient;
PubSubClient client(espClient);

// Declaração de variáveis globais e pinos
bool buttonPressed;
bool tareButtonPressed = false;
const int tareButton = 35;
const int button = 34;

// Enumeração para estados do LED e buzzer
enum State{
  LED_BUZZER_OFF,
  BLINKING,
  LED_ON_BUZZER_OFF
};

// Iniciando no estado
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
    // Inicia a escala HX711
    scale.begin(2, 4);

    // Inicialização da biblioteca LEDC
    ledcSetup(0, 5000, 8); // Canal 0, frequência de 5000 Hz, resolução de 8 bits
    ledcAttachPin(redPin, 0); // Anexar o LED ao canal 0
    ledcSetup(1, 5000, 8); // Canal 1, frequência de 5000 Hz, resolução de 8 bits
    ledcAttachPin(buzzer, 1); // Anexar o buzzer ao canal 1

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

  // Método para verificar estado do limite de peso
  void checkWeightLimit() {
    switch(state){

      //Para o led e o buzzer estarem desligados:
      case LED_BUZZER_OFF:
        //A força tem que estar abaixo do limite definido
        if(newtonBalance < alertLimit){
          digitalWrite(redPin, HIGH);
          digitalWrite(greenPin, LOW);
          digitalWrite(bluePin, HIGH);
          noTone(buzzer);

          //Se o botão tiver sido pressionado anteriormente, ele é liberado agora 
          if(buttonPressed == true){
            buttonPressed = 0;
            Serial.println("Botão liberado");
          }
        } 
        
        //Caso não estiverem no estado desligados, estarão no estado piscando
        else{
          state = BLINKING;
        }
        break;
      
      //Para o led e o buzzer estarem piscando:
      case BLINKING:

        //A força tem que estar abaixo do limite definido. Piscarão a cada meio segundo.
        if(newtonBalance >= alertLimit && millis()-previousTime > halfSecond){
          digitalWrite(greenPin, HIGH);
          digitalWrite(bluePin, HIGH);
          digitalWrite(redPin, !digitalRead(redPin));
          tone(buzzer, digitalRead(redPin) ? 500:0);
          previousTime = millis();
          
          //Se o botão for pressionado, muda o estado para LED_ON_BUZZER_OFF
          if( buttonPressed == true){
            state = LED_ON_BUZZER_OFF;
            Serial.println("Botão Pressionado");
          } 
        } 
        //Se não estiver no estado BLINKING, estará desligado (estado LED_BUZZER_OFF)
        else{
          state = LED_BUZZER_OFF;
        }
        break;
      
      //Para o led ligado e o buzzer desligado:
      case LED_ON_BUZZER_OFF:

        //A força tem que estar igual ou acima do limite definido e o botão tem que ter sido pressionado
        if(newtonBalance >= alertLimit &&  buttonPressed == true){
          digitalWrite(redPin, LOW);
          digitalWrite(greenPin, HIGH);
          digitalWrite(bluePin, HIGH);
          noTone(buzzer);
        }
        //Se não estiver nesse estado, estará desligado (estado LED_BUZZER_OFF)
        else{
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
  void setTare(){
    if(tareButtonPressed == true){
      Serial.println("Função da tara ativada");

      digitalWrite(greenPin, LOW);
      digitalWrite(redPin, LOW);
      digitalWrite(bluePin, HIGH);
      noTone(buzzer);

      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Coloque o peso");
      lcd.setCursor(0,1);
      lcd.print("para tara.");

      delay(4000);
      scale.tare();

      tareButtonPressed = false;
      lcd.clear();
    }
  };
};

// Instancia um objeto da classe WeightMeasurementSystem
WeightMeasurementSystem wms;

//Define constantes para MQTT e Wi-Fi
#define WIFI_SSID "Wi-Fi"
#define WIFI_PASSWORD "senha"
#define TOKEN "BBUS-C8isdbxdjwz3KOn2qv4MsXsclhiiYy"
#define DEVICE_LABEL "esp32"

//Função para detectar erro de funcionamento do GY-BME280
void detectionSensorTemp(){
  if (!bme.begin(0x76)) {
    Serial.println("Não foi possível encontrar o sensor BME280. Verifique as conexões!");
    while (1);
  }
}

// Função para conectar ao Wi-Fi e ao servidor MQTT
void connectionWifiAndMqtt(){
  // Conectar-se à rede WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando ao WiFi...");
  }
  Serial.println("Conectado ao WiFi");
  client.connect("ESP32", TOKEN, "");
  Serial.println("Conectado ao servidor MQTT");
  // Configurar o cliente MQTT
  client.setServer("industrial.api.ubidots.com", 1883);
}

// Função para reconectar ao servidor MQTT
void reconnectMqtt() {
  while (!client.connected()) {
    Serial.println("Tentando se reconectar ao MQTT...");
    if (client.connect("ESP32", TOKEN, "")) {
      Serial.println("Conectado ao servidor MQTT");
    } else {
      Serial.print("Falha na reconexão. Código de erro = ");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// Função para receber mensagens MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String payloadStr;
  for (int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }

  Serial.println(payloadStr);

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, payloadStr);

  if (doc["context"].containsKey("estado")) {
    String estado = doc["context"]["estado"];
    if (estado == "ciente") {
     buttonPressed = true;
    }
  }

  if (doc["context"].containsKey("Limit")) {
    // Atualiza limitAlert com o valor do limite
    wms.alertLimit = doc["context"]["Limit"].as<float>();
  }
}

// Função para subscrever em tópicos
void subscribers(){
  client.subscribe(("/v1.6/devices/" + String(DEVICE_LABEL) + "/awareLimit").c_str());
  client.subscribe(("/v1.6/devices/" + String(DEVICE_LABEL) + "/inputLimit").c_str());
}

// Publish da temperatura no servidor MQTT
void publishTemperature(){
  float getTemperature = bme.readTemperature();
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"temperature\": %.2f}", getTemperature);
  //Publica os dados
  client.publish(topic, payload);
}

// Publish do peso no servidor MQTT
void publishWeight(){
  float getWeight = wms.getWeight();
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"weight\": %.2f}", getWeight);
  client.publish(topic, payload);
}

// Publish o reset do botão de alerta de limite
void publishAwareLimit(){
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"awarelimit\": %f}", 0.0);
  client.publish(topic, payload);
}

// Publish do valor de limite
void publishValueLimit(){
  const float valueLimit = wms.alertLimit;
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"valueLimit\": %f}", valueLimit);
  client.publish(topic, payload);
}

//Setup
void setup() {
  Serial.begin(115200);

  detectionSensorTemp();
  connectionWifiAndMqtt();
  client.setCallback(callback);
  subscribers();

  // Escala da balança
  scale.set_scale(210514.50567261);   
  delay(2000);
  scale.tare();

  //Inicializa o lcd
  lcd.init();
  lcd.backlight();
  
  buttonPressed = false;
};

//Loop
void loop() {
  //Checa se o cliente está conectado
  if (!client.connected()) {
    reconnectMqtt();
    subscribers();
  }

  client.loop();

  // Envia dos dados
  publishValueLimit();
  publishTemperature();
  publishAwareLimit();
  publishWeight();

  //Variável para o peso da balança
  float weightBalance = wms.getWeight();

  // Executa a troca de estado quando necessário
  if(wms.newtonBalance >= wms.alertLimit && digitalRead(button)) {
    buttonPressed = true;
  }
  
  // Executa a tara quando necessário
  if (digitalRead(tareButton)) {
    tareButtonPressed = true;
    Serial.println("Tara pressionada");
  }
  
  //Conversão do peso em newton
  wms.convertWeightToNewton(weightBalance);

  //Variável para a temperatura
  float temperature = bme.readTemperature();
  
  //Print do serial
  // Serial.println(String(temperature) + "°C " + String(weightBalance) + "kg " + String(wms.newtonBalance) + "N");  

  //Configurações do lcd
  lcd.setCursor(0, 0);
  lcd.print(String(temperature) + " graus");
  lcd.setCursor(0, 1);
  lcd.print(String(weightBalance) + "kg " + String(wms.newtonBalance) + "N");

  //Configurações de acordo com o peso e o limite
  wms.checkWeightLimit();


  //Função tara
  wms.setTare();
  client.loop();
  delay(10);
};