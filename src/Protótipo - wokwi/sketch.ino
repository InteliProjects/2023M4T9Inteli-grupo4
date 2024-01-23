#include "HX711.h"
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
bool buttonPressed;
bool tareButtonPressed = false;
const int tareButton = 35;
const int button = 34;

//Estados do led
enum State{
  LED_BUZZER_OFF,
  BLINKING,
  LED_ON_BUZZER_OFF
};

//Iniciando no estado
State state = LED_BUZZER_OFF;

//Criando a classe WeightMeasurementSystem
class WeightMeasurementSystem {

public:
  //Variáveis
  float gravity = 9.8;
  float weightBalance; 
  float newtonBalance; 
  
  HX711 scale; 

  //Constantes
  const int led = 25;
  const int buzzer = 26;
  int alertLimit;

  //Variáveis de tempo
  unsigned long previousTime = 0;
  const long halfSecond = 500;

  //Construtor
  WeightMeasurementSystem() {
    scale.begin(2, 4);

    // Inicialização da biblioteca LEDC
    // ledcSetup(0, 5000, 8); // Canal 0, frequência de 5000 Hz, resolução de 8 bits
    // ledcAttachPin(led, 0); // Anexar o LED ao canal 0
    // ledcSetup(1, 5000, 8); // Canal 1, frequência de 5000 Hz, resolução de 8 bits
    // ledcAttachPin(buzzer, 1); // Anexar o buzzer ao canal 1

    pinMode(tareButton, INPUT);
    pinMode(led, OUTPUT);
    pinMode(buzzer, OUTPUT);
    pinMode(button, INPUT);
    scale.set_scale(420);
    
  }

  //Método de conversão de unidades
  void convertWeightToNewton(float weightBalance) {
    newtonBalance = weightBalance * gravity;
  }

  //Método de estado de alerta
  void checkWeightLimit() {
    switch(state){

      //Para o led e o buzzer estarem desligados:
      case LED_BUZZER_OFF:
        //A força tem que estar abaixo do limite definido
        if(newtonBalance < alertLimit){
          digitalWrite(led, LOW);
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
          digitalWrite(led, !digitalRead(led));
          tone(buzzer, digitalRead(led) ? 500:0);
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
          digitalWrite(led, HIGH);
          noTone(buzzer);
        }
        //Se não estiver nesse estado, estará desligado (estado LED_BUZZER_OFF)
        else{
          state = LED_BUZZER_OFF;
        }
        break;
    }

  }
  //Registra o peso
  float getWeight() {
    return scale.get_units();
  }
};

//criando uma instância de WeightMeasurementSystem
WeightMeasurementSystem wms; 

//Setup
void setup() {
  Serial.begin(9600);

  //Inicializa o lcd
  lcd.init();
  lcd.backlight();
  
  buttonPressed = 0;

  //Parecido com input, irá coletar o valor digitado e transformar no limite de peso suportado (alertLimit)
  Serial.println("Qual o limite de força em Newtons suportado?"); 
  while (Serial.available() == 0) {}
  wms.alertLimit = Serial.parseInt();
  Serial.print("Limite de alerta configurado para: ");
  Serial.println(wms.alertLimit);

};

//Loop
void loop() {
  float weightBalance = wms.getWeight();
  if(wms.newtonBalance >= wms.alertLimit && digitalRead(button)) {
    buttonPressed = true;
  }
  
  if (digitalRead(tareButton)) {
    wms.scale.tare();
    tareButtonPressed = true;
  }
  
  wms.convertWeightToNewton(weightBalance);
  // Serial.println("Leitura:" + String(weightBalance) + "kg " + String(wms.newtonBalance) + "N");
  
  lcd.setCursor(0, 0);
  lcd.print("Leitura:");
  lcd.setCursor(0,1);
  lcd.print(String(weightBalance) + "kg " + String(wms.newtonBalance) + "N");
  wms.checkWeightLimit();
  delay(10);
};