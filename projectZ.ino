#include <LiquidCrystal.h>

//пользовательские символы для LCD
byte heart_part1[8] = {
  B01110,
  B11111,
  B11111,
  B11111,
  B01111,
  B00111,
  B00011,
  B00001
};

byte heart_part2[8] = {
  B01110,
  B11111,
  B11111,
  B11111,
  B11110,
  B11100,
  B11000,
  B10000
};

byte pi[8] = {
  B11111,
  B01010,
  B01010,
  B01010,
  B01010,
  B01011,
  B10000,
  B00000
};

//пульс
#define samp_siz 6 // количество расчетов - можно увеличить до 20
#define rise_threshold 5
int PinPulse = 35;
float bpm;
float BPM;
float reads[samp_siz], sum;
long int last_beat;
float first, second, third;
bool sensorInitialized = false;

//термистер
float temp;
int ThermistorPin = 34;
int Vo;
float R1 = 10000; // значение R1 на модуле
float logR2, R2, T;
float c1 = 0.001129148, c2 = 0.000234125, c3 = 0.0000000876741; //коэффициенты Штейнхарта-Харта для термистора

// Определяем пины для подключения LCD дисплея
const int rs = 13;    // Register Select
const int en = 12;    // Enable
const int d4 = 14;    // Data bit 4
const int d5 = 27;    // Data bit 5
const int d6 = 26;    // Data bit 6
const int d7 = 25;    // Data bit 7

// Создаем объект LCD
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

//допы
unsigned long timing;
int PinBuzzer = 23;

void setup() {
  //открываем порт
  Serial.begin(9600);

  // Настройка АЦП ESP32
  analogReadResolution(12);      // 12-битное разрешение (0-4095)

  // Инициализируем LCD дисплей (16 символов, 2 строки)
  lcd.begin(16, 2);
  lcd.createChar(0, heart_part1);
  lcd.createChar(1, heart_part2);
  lcd.createChar(2, pi);
  lcd.clear();

  // Инициализируем датчика пульса
  initPulseSensor();
  
  //Настройка вывода платы в режим "Выход"
  pinMode (PinBuzzer, OUTPUT);
  pinMode(PinPulse, INPUT);

  //приветствие
  lcd.write(byte(2));
  lcd.print("-vo's children");
  lcd.setCursor(3,1);
  lcd.print("NeiroTech");
  tone(PinBuzzer, 1500); // включаем звук частотой 1500 Гц
  delay(200);
  tone(PinBuzzer, 1000); // включаем звук частотой 1000 Гц
  delay(200);
  tone(PinBuzzer, 500); // включаем звук частотой 500 Гц
  delay(200);
  noTone(PinBuzzer); // выключаем звук
  delay(3000);
}

void loop() {
  //измерение температуры
  temp = Temp();

  //Измерение пульса
  bpm = Pulse();
  if (bpm > 0) {
        Serial.print("BPM - ");
        Serial.println(bpm);
        BPM = bpm;
        tone(PinBuzzer, 2000,100);
        Heart();
  }

  delay(20);

  //вывод на дисплей
  if (millis() - timing > 500){ // Вместо 10000 подставьте нужное вам значение паузы 
    timing = millis();
    lcd.clear();

    lcd.print("Temp - ");
    lcd.print(temp);
    lcd.print("C");

    lcd.setCursor(0, 1);
    lcd.print("BPM - ");
    lcd.print(BPM);
 }
}

float Temp(){
  Vo = analogRead(ThermistorPin);
  R2 = R1 * (4095.0 / (float)Vo - 1.0); //вычислите сопротивление на термисторе
  logR2 = log(R2);
  T = (1.0 / (c1 + c2*logR2 + c3*logR2*logR2*logR2)); // температура в Кельвине
  T = T - 273.15; //преобразование Кельвина в Цельсия
  return T;
}

void initPulseSensor() {
    for (int i = 0; i < samp_siz; i++) {
        reads[i] = 0;
    }
    sum = 0;
    last_beat = millis();
    first = 0;
    second = 0;
    third = 0;
    sensorInitialized = true;
}

float Pulse(){
  if (!sensorInitialized) {
        initPulseSensor();
  }

  static long int ptr = 0;
  static float last = 0, before = 0;
  static bool rising = false;
  static int rise_count = 0;

  float reader = 0;
  int n = 0;
  long int now, start;
  float bpm = -1; // -1 означает, что удара не обнаружено
  
  // В течение 20 мс вычисляем среднее значение датчика
  start = millis();
  do {
      reader += analogRead(PinPulse);
      n++;
      now = millis();
  } while (now < start + 20);
  
  reader /= n; // Получаем среднее значение
  
  // Обновляем скользящее среднее
  sum -= reads[ptr];
  sum += reader;
  reads[ptr] = reader;
  last = sum / samp_siz;

  // Проверяем, появляется ли увеличение среднего значения
  if (last > before) {
      rise_count++;

      // Если значение увеличивается, что подразумевает сердцебиение
      if (!rising && rise_count > rise_threshold) {
          rising = true;
          first = millis() - last_beat;
          last_beat = millis();

          // Рассчитываем средневзвешенную частоту сердечных сокращений
          bpm = 60000.0 / (0.4 * first + 0.3 * second + 0.3 * third);
          
          // Обновляем историю ударов
          third = second;
          second = first;
      }
  } else {
      // Сбрасываем состояние при снижении сигнала
      rising = false;
      rise_count = 0;
  }
  
  before = last;
  ptr++;
  ptr %= samp_siz;

  return bpm;
}

void Heart(){
  lcd.clear();

  lcd.print("Temp - ");
  lcd.print(temp);
  lcd.print("C");

  lcd.setCursor(0, 1);
  lcd.print("BPM - ");
  lcd.print(BPM);

  lcd.write(byte(0));
  lcd.write(byte(1));

  delay(100); //я знаю что это костыль!!
}