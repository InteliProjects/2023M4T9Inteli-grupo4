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

// Inclui bibliotecas para utilizar o cartão SD
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

//  Inclui biblioteca de estatísticas
#include <Statistic.h>

//Inclui biblioteca para registro da hora
#include "time.h"

// Cria instâncias
HX711 scale; 
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_BME280 bme;
WiFiClient espClient;
PubSubClient client(espClient);
File logFile;
Statistic weightStats;
Statistic forceStats;
Statistic tempStats;

// Estados
bool buttonPressed;
bool tareButtonPressed = false;
const int tareButton = 35;
const int button = 34;
unsigned long lastTimeClosed = 0;
unsigned long lastTimeDataSend = 0;
unsigned long lastAttemptConnection = 0;

//Variáveis para registro da hora
const char* ntpServer = "a.st1.ntp.br";
const long  gmtOffset_sec = -3 * 3600;
const int   daylightOffset_sec = 0;


// Enumeração para estados do LED e buzzer
enum State{
  LED_BUZZER_OFF,
  BLINKING,
  LED_ON_BUZZER_OFF
};

//Define constantes para MQTT e Wi-Fi
#define WIFI_SSID "Wifi"
#define WIFI_PASSWORD "Senha"
#define TOKEN "Token"
#define DEVICE_LABEL "Dispositivo"

// Iniciando no estado
State state = LED_BUZZER_OFF;

// Classe para o sistema de medição de peso
class WeightMeasurementSystem {

  public:
  float gravity = 9.8;
  float weightBalance; 
  float newtonBalance; 
  bool alertLimitStatus;
  bool alertLoadCellStatus;

  const int redPin = 12;
  const int greenPin = 14;
  const int bluePin = 27;
  const int buzzer = 26;
  float alertLimit;
  float loadCellLimit;

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
  float convertWeightToNewton(float weight) {
    newtonBalance = weight * gravity;
    return newtonBalance;
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
          alertLimitStatus = 0;
          alertLoadCellStatus = 0;

          //Se o botão tiver sido pressionado anteriormente, ele é liberado agora 
          if(buttonPressed == true){
            buttonPressed = 0;
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
        if(newtonBalance >= alertLimit && weightBalance < loadCellLimit && millis()-previousTime > halfSecond){
          digitalWrite(greenPin, !digitalRead(redPin));
          digitalWrite(bluePin, HIGH);
          digitalWrite(redPin, !digitalRead(redPin));
          tone(buzzer, !digitalRead(redPin) ? 500:0);
          previousTime = millis();
          alertLimitStatus = 1;
          alertLoadCellStatus = 0;
          
          //Se o botão for pressionado, muda o estado para LED_ON_BUZZER_OFF
          if( buttonPressed == true){
            state = LED_ON_BUZZER_OFF;
          } 
        }

        else if (newtonBalance >= alertLimit && weightBalance >= loadCellLimit && millis()-previousTime > halfSecond){
          digitalWrite(greenPin, HIGH);
          digitalWrite(bluePin, HIGH);
          digitalWrite(redPin, !digitalRead(redPin));
          tone(buzzer, !digitalRead(redPin) ? 500:0);
          previousTime = millis();
          alertLimitStatus = 1;
          alertLoadCellStatus = 1;
          
          //Se o botão for pressionado, muda o estado para LED_ON_BUZZER_OFF
          if( buttonPressed == true){
            state = LED_ON_BUZZER_OFF;
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
        if(newtonBalance >= alertLimit && weightBalance < loadCellLimit && buttonPressed == true){
          digitalWrite(redPin, LOW);
          digitalWrite(greenPin, LOW);
          digitalWrite(bluePin, HIGH);
          noTone(buzzer);
          alertLimitStatus = 1;
          alertLoadCellStatus = 0;
        }
        else if(newtonBalance >= alertLimit && weightBalance >= loadCellLimit && buttonPressed == true){
          digitalWrite(redPin, LOW);
          digitalWrite(greenPin, HIGH);
          digitalWrite(bluePin, HIGH);
          noTone(buzzer);
          alertLimitStatus = 1;
          alertLoadCellStatus = 1;
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
    weightBalance = scale.get_units();
    return weightBalance;
  }

  // Método para configurar tara
  void setTare(){
    if(tareButtonPressed == true){
      digitalWrite(greenPin, HIGH);
      digitalWrite(redPin, HIGH);
      digitalWrite(bluePin, LOW);
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

//Função para detectar erro de funcionamento do GY-BME280
void detectionSensorTemp(){
  while (!bme.begin(0x76)) {
    Serial.println("Não foi possível encontrar o sensor BME280. Verifique as conexões!");
    digitalWrite(wms.redPin, LOW);
    digitalWrite(wms.bluePin, LOW);
    digitalWrite(wms.greenPin, HIGH);

    //display
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Erro no sensor");
    lcd.setCursor(0,1);
    lcd.print("de temperatura");

  }
}

char* getCurrentTime() {
  static char dateTimeStr[26];
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Falha ao obter a hora");
    return NULL;
  }
  strftime(dateTimeStr, sizeof(dateTimeStr), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return dateTimeStr;
}

// Função para conectar ao Wi-Fi e ao servidor MQTT
void connectionWifiAndMqtt(){
  //Led da coloração branca
  digitalWrite(wms.redPin, LOW);
  digitalWrite(wms.greenPin, LOW);
  digitalWrite(wms.bluePin, LOW);

  // Conectar-se à rede WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lcd.clear();
  
  //Ebquanto não estiver conectado  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando ao WiFi...");
    lcd.setCursor(0, 0);
    lcd.print("Conectando ao");
    lcd.setCursor(0,1);
    lcd.print("Wi-Fi...");
    // Configurar o cliente MQTT
    client.setServer("industrial.api.ubidots.com", 1883);
  }

  //Quando conectar o wifi
  Serial.println("Conectado ao WiFi");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Wi-Fi conectado");

  //Quando conectar o servidor
  client.connect("ESP32", TOKEN, "");
  lcd.setCursor(0, 0);
  lcd.print("Wi-Fi e Servidor");
  lcd.setCursor(0, 1);
  lcd.print("conectados.");

  Serial.println("Conectado ao servidor MQTT");

  //Configurando a hora com o protocolo N
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

// Função para reconectar ao servidor MQTT
void reconnectMqtt() {
  int attempts = 1;

  //Led da coloração branca
  digitalWrite(wms.redPin, LOW);
  digitalWrite(wms.greenPin, LOW);
  digitalWrite(wms.bluePin, LOW);
  lcd.clear();

  // Enquanto não estiver conectado ao servidor, iá repetir o ciclo 5 vezes
  while (!client.connected() && attempts <= 5) {
    Serial.println("Tentando se reconectar ao MQTT...");
    lcd.setCursor(0,0);
    lcd.print("Reconectando...");
    lcd.setCursor(0,1);
    lcd.print("Tentativa:" + String(attempts));
    delay(4000);

    // Ao conectar  
    if (client.connect("ESP32", TOKEN, "")) {
      Serial.println("Conectado ao servidor MQTT");
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Reconectado ao");
      lcd.setCursor(0,1);
      lcd.print("servidor");
      subscribers();
      delay(4000);
      lcd.clear();

      //Configurando a hora com o protocolo NTP
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    } 
    
    // Caso não conectar
    else {
      Serial.print("Falha na reconexão. Código de erro = ");
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Falha na");
      lcd.setCursor(0,1);
      lcd.print("reconexao.");
      delay(4000);

      lcd.clear();
      Serial.println(client.state());
    }
    attempts++;
  }
}

// Função para receber mensagens MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String payloadStr;
  for (int i = 0; i < length; i++) {
    payloadStr += (char)payload[i];
  }

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, payloadStr);

  if (doc["context"].containsKey("estado")) {
    String estado = doc["context"]["estado"];
    if (estado == "ciente") {
     buttonPressed = true;
    }
  }

  if (doc["context"].containsKey("AlertLimit")) {
    // Atualiza limitAlert com o valor do limite de alerta
    wms.alertLimit = doc["context"]["AlertLimit"].as<float>();
  }

  if (doc["context"].containsKey("LoadCellLimit")) {
    // Atualiza loadCellLimit com o valor do limite da célula de carga
    wms.loadCellLimit = doc["context"]["LoadCellLimit"].as<float>();
  }
}

// Função para subscrever em tópicos
void subscribers(){
  client.subscribe(("/v1.6/devices/" + String(DEVICE_LABEL) + "/awareLimit").c_str());
  client.subscribe(("/v1.6/devices/" + String(DEVICE_LABEL) + "/inputLimit").c_str());
  client.subscribe(("/v1.6/devices/" + String(DEVICE_LABEL) + "/inputloadcelllimit").c_str());
}

// Publish da temperatura no servidor MQTT
void publishTemperature(float data){
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"temperature\": %.2f}", data);
  //Publica os dados
  client.publish(topic, payload);
}

// Publish do peso no servidor MQTT
void publishWeight(float data){
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"weight\": %.2f}", data);
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
void publishValueAlertLimit(float data){
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"valuealertlimit\": %f}", data);
  client.publish(topic, payload);
}

void publishLoadCellLimit(float data){
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"loadcelllimit\": %f}", data);
  client.publish(topic, payload);
}

void publishForce(){
  float getForce = wms.convertWeightToNewton(wms.weightBalance);
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"force\": %.2f}", getForce);
  client.publish(topic, payload);
}

void publishUsedSpaceLimit(float force, float limit){
  //Porcentagem do limite utilizado
  float usedSpaceLimit = ((force *100)/limit);
  if (limit == 0 || usedSpaceLimit <= 0){
    usedSpaceLimit = 0;
  }

  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"usedspacelimit\": %f}", usedSpaceLimit);
  client.publish(topic, payload);
}

void publishUsedSpaceLoadCell(float weight, float limit){
  //Porcentagem da célula de carga utilizada
  float usedSpaceLoadCell = ((weight *100)/limit);
  if (limit == 0 || usedSpaceLoadCell <= 0){
    usedSpaceLoadCell = 0;
  }

  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"usedSpaceLoadCell\": %f}", usedSpaceLoadCell);
  client.publish(topic, payload);
}

void publishalertLimitStatus(bool data){
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"alertLimitStatus\": %d}", data);
  client.publish(topic, payload);
}

void publishAlertLoadCellStatus(bool data){
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"alertLoadCellStatus\": %d}", data);
  client.publish(topic, payload);
}

void publishConnectionStatus(){
  char topic[100];
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  char payload[100];
  sprintf(payload, "{\"connectionstatus\": %f}", 1.0);
  client.publish(topic, payload);
}

void showLCD(float temperature, float weight, float force){
  lcd.setCursor(0, 0);
  lcd.print(String(temperature) + " graus");
  lcd.setCursor(0, 1);
  lcd.print(String(weight) + "kg " + String(force) + "N");
}

void detectionSDCard() {
  //Não inicia o programa se o cartão não for iniciado corretamente
  while (!SD.begin(5)) {
    Serial.println("Falha na inicialização do cartão SD");
    digitalWrite(wms.redPin, LOW);
    digitalWrite(wms.bluePin, LOW);
    digitalWrite(wms.greenPin, HIGH);    

    //display
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro no modulo");
    lcd.setCursor(0, 1);
    lcd.print("do cartao");
  }
}

void createFile(){
  logFile = SD.open("/log.csv", FILE_WRITE);
  if (logFile) {
    logFile.println("Data e Hora, Peso minimo (kg), Peso maximo (kg), Peso medio (kg), Forca minima (N), Forca maxima (N), Forca media (N), Temperatura minima (C), Temperatura maxima (C), Temperatura media (C)");
    Serial.println("Arquivo criado");
    logFile.close();
  }else{
    Serial.println("Falha ao criar o arquivo");
  }
}

void sendDataToFile(){
  static unsigned long lastTime = 0;
  char* currentTime = getCurrentTime();

  // Envia os dados a cada 10 minutos
  if (millis() - lastTime >= 600000) {

    // Calcula a média, o mínimo e o máximo
    float avgWeight = weightStats.average();
    float minWeight = weightStats.minimum();
    float maxWeight = weightStats.maximum();

    float avgForce = forceStats.average();
    float minForce = forceStats.minimum();
    float maxForce = forceStats.maximum();

    float avgTemp = tempStats.average();
    float minTemp = tempStats.minimum();
    float maxTemp = tempStats.maximum();

    logFile = SD.open("/log.csv", FILE_APPEND);
    if (logFile) {
     // Escreve a data e hora no arquivo, se a data não for nula
      if (currentTime != NULL) {
        logFile.print(currentTime);
        logFile.print(",");
      }

      // Escreve os dados das variáveis no arquivo
      logFile.print(minWeight);
      logFile.print(",");
      logFile.print(maxWeight);
      logFile.print(",");
      logFile.print(avgWeight);
      logFile.print(",");
      logFile.print(minForce);
      logFile.print(",");
      logFile.print(maxForce);
      logFile.print(",");
      logFile.print(avgForce);
      logFile.print(",");
      logFile.print(minTemp);
      logFile.print(",");
      logFile.print(maxTemp);
      logFile.print(",");
      logFile.println(avgTemp);

      Serial.println("Dados escritos com sucesso.");
    } else {
      Serial.println("Erro ao escrever os dados.");
    }

    // Redefine as estatísticas para o próximo intervalo 
    weightStats.clear();
    forceStats.clear();
    tempStats.clear();

    lastTime = millis();
  }
}

//Setup
void setup() {
  Serial.begin(115200);
  //Inicia o LCD
  lcd.init();
  lcd.backlight();
  //Inicia o Módulo SD
  SD.begin(5);
  //Verifica se o sensor BME280 está conectado corretamente
  detectionSensorTemp();
  //Verifica se o SD está conectado corretamente
  detectionSDCard();
  //Realiza as conexões WiFi e MQTT
  connectionWifiAndMqtt();
  client.setCallback(callback);
  subscribers();

  // Escala da balança
  scale.set_scale(210514.50567261);   
  delay(2000);
  scale.tare();

  //Cria o arquivo Log no cartão SD
  createFile();

  //Limpa o LCD antes de iniciar o loop
  lcd.clear();

  buttonPressed = false;
};

//Loop
void loop() {
  //Verifica conexões
  detectionSensorTemp();

  //Inicia o ciclo de reconexão a cada 5 minutos sem internet
  if (!client.connected() && millis() - lastAttemptConnection > 300000) {
    Serial.println("Queda na conexão com o MQTT");
    reconnectMqtt();
    if (!client.connected()){
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Nova tentativa");
      lcd.setCursor(0,1);
      lcd.print("em 5 minutos");
      delay(4000);
      lcd.clear();
    }
    lastAttemptConnection = millis();
  }

  //Variável para a temperatura
  float temperature = bme.readTemperature();

  //Atualiza o peso da balança
  wms.getWeight();
  
  //Conversão do peso em newton
  wms.convertWeightToNewton(wms.weightBalance);

  publishConnectionStatus();
  publishValueAlertLimit(wms.alertLimit);
  publishLoadCellLimit(wms.loadCellLimit);
  publishWeight(wms.weightBalance);
  publishForce();
  publishTemperature(temperature);
  publishalertLimitStatus(wms.alertLimitStatus);
  publishAwareLimit();
  publishUsedSpaceLimit(wms.newtonBalance, wms.alertLimit);
  publishUsedSpaceLoadCell(wms.weightBalance, wms.loadCellLimit);
  publishAlertLoadCellStatus(wms.alertLoadCellStatus);
 
  // Executa a troca de estado quando necessário
  if(wms.newtonBalance >= wms.alertLimit && digitalRead(button)) {
    buttonPressed = true;
  }
  
  // Executa a tara quando necessário
  if (digitalRead(tareButton)) {
    tareButtonPressed = true;
    Serial.println("Tara pressionada");
  }

  //Mostra as informações no display
  showLCD(temperature, wms.weightBalance, wms.newtonBalance);

  //Configurações de acordo com o peso e o limite
  wms.checkWeightLimit();

  //Função tara
  wms.setTare();

  //Calcula estatísticas
  weightStats.add(wms.weightBalance);
  forceStats.add(wms.newtonBalance);
  tempStats.add(temperature);

  //Envia os dados para o cartão SD
  sendDataToFile();

  client.loop();
  delay(10);
};