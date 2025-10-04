#include <LiquidCrystal.h>

//пульс
const int pulsePin = 35;

int sensorValue = 0;
int lastValue = 0;
bool rising = false;
unsigned long lastBeatTime = 0;
unsigned long lastCalculationTime = 0;

int beatCount = 0;
const int sampleWindow = 10000; // 10 секунд для расчета
int bpm = 0;

const int numReadings = 10;
int readings[numReadings];
int readIndex = 0;
int total = 0;
int average = 0;

int threshold = 2000;
int minThreshold = 1500;
int maxThreshold = 3000;

//термистер
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
  analogReadResolution(12);      // 10-битное разрешение (0-4095)

   // Инициализация массива для скользящего среднего
  for (int i = 0; i < numReadings; i++) {
    readings[i] = 0;
  }

  // Инициализируем LCD дисплей (16 символов, 2 строки)
  lcd.begin(16, 2);
  lcd.clear();
  
  //Настройка вывода платы в режим "Выход"
  pinMode (PinBuzzer, OUTPUT);

  // Выводим приветственное сообщение
  lcd.print("Hello");
  delay(2000);

  tone(PinBuzzer, 1500); // включаем звук частотой 1500 Гц
  delay(200);
  tone(PinBuzzer, 1000); // включаем звук частотой 1000 Гц
  delay(200);
  tone(PinBuzzer, 500); // включаем звук частотой 500 Гц
  delay(200);

  noTone(PinBuzzer); // выключаем звук

}

void loop() {
  //измерение температуры
  Vo = analogRead(ThermistorPin);
  R2 = R1 * (4095.0 / (float)Vo - 1.0); //вычислите сопротивление на термисторе
  logR2 = log(R2);
  T = (1.0 / (c1 + c2*logR2 + c3*logR2*logR2*logR2)); // температура в Кельвине
  T = T - 273.15; //преобразование Кельвина в Цельсия
 
  //отображение температуры в serial
  //Serial.print("Temperature: "); 
  //Serial.print(T);
  //Serial.println(" C"); 
  
  //измерение пульса
  // Чтение значения с датчика
  sensorValue = analogRead(pulsePin);
  
  // Применяем скользящее среднее для фильтрации шума
  total = total - readings[readIndex];
  readings[readIndex] = sensorValue;
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % numReadings;
  
  average = total / numReadings;
  
  // Автокалибровка порога
  threshold = (minThreshold + maxThreshold) / 2;
  
  // Обнаружение сердцебиения
  detectBeat(average);
  
  // Расчет BPM каждые 10 секунд
  if (millis() - lastCalculationTime >= sampleWindow) {
    calculateBPM();
    lastCalculationTime = millis();
  }

  // Вывод данных в Serial
  Serial.print("Signal: ");
  Serial.print(average);
  Serial.print(", Threshold: ");
  Serial.print(threshold);
  Serial.print(", BPM: ");
  Serial.println(bpm);

  delay(20);

  //вывод на дисплей
  if (millis() - timing > 500){ // Вместо 10000 подставьте нужное вам значение паузы 
    timing = millis(); 
    lcd.clear();

    lcd.print("Temp - ");
    lcd.print(T);
    lcd.print( "C");

    lcd.setCursor(0, 1);
    lcd.print("Sig:");
    lcd.print(average);
    lcd.print(",BPM:");
    lcd.print(bpm);
 }
}

void detectBeat(int value) {
  if (value > threshold && !rising && value > lastValue) {
    // Начало подъема сигнала
    rising = true;
  }
  
  if (rising && value < threshold) {
    // Пик пройден - обнаружено сердцебиение
    rising = false;
    
    unsigned long currentTime = millis();
    
    // Проверяем, что это не ложное срабатывание (минимум 300 мс между ударами)
    if (currentTime - lastBeatTime > 200) {
      beatCount++;
      lastBeatTime = currentTime;
      
      // Динамическая подстройка порогов
      if (value > maxThreshold) {
        maxThreshold = value - 100;
      }
      if (value < minThreshold) {
        minThreshold = value + 100;
      }
       
      Serial.println("♥ BEAT DETECTED ♥");
      tone(PinBuzzer, 2500,300); // включаем звук частотой 2500 Гц
    }
  }
  
  lastValue = value;
}

void calculateBPM() {
  if (beatCount > 1) {
    // Расчет BPM на основе количества ударов за период времени
    unsigned long elapsedTime = millis() - lastCalculationTime;
    bpm = (beatCount * 60000) / elapsedTime;
    
    // Ограничение реалистичных значений пульса
    if (bpm < 40) bpm = 0;
    if (bpm > 200) bpm = 0;
  } else {
    bpm = 0;
  }
  
  beatCount = 0;
}