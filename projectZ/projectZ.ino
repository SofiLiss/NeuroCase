#include <LiquidCrystal.h>
#include <Arduino.h>
#include <GyverDBFile.h>
#include <LittleFS.h>
#include <SettingsESP.h>
#include <WiFiConnector.h>

GyverDBFile db(&LittleFS, "/data.db");
SettingsESP sett("Medical monitor", &db);

//ключи (ID) элементов в базе данных
DB_KEYS(
    kk,
    wifi_ssid,
    wifi_pass,
    apply);

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

//ЭКГ
const int LO_PLUS_PIN = 33;
const int LO_MINUS_PIN = 32;
const int ECG_PIN = 34;
int samplingDelay = 4; // ~250 Hz (1000/4 = 250 samples per second)
float alpha = 0.1; // Коэффициент фильтра (0-1)
int filteredValue = 0;
int rawValue;
int baseline;

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
#define PinButton 18
unsigned long timing;
int PinBuzzer = 23;

// ========== build ==========
void build(sets::Builder& b) 
{
  {
    sets::Group g(b, "WiFi");

    b.Input(kk::wifi_ssid, "SSID");
    b.Pass(kk::wifi_pass, "Password", "***");

    if (b.Button(kk::apply, "Connect")) 
    {
      db.update();
      WiFiConnector.connect(db[kk::wifi_ssid], db[kk::wifi_pass]);
    }
  }
  {
    sets::Group g(b, "info");

    b.LabelFloat("lbl_t"_h, "Температура", temp);
    b.LabelFloat("lbl_p"_h, "Пульс", BPM);
  }
  {
    sets::Group g(b, "plots");

    b.PlotRunning(H(temp), "Температура[°C]");
    b.PlotRunning(H(ecg), "Сырое значение, фильтр, baseline");
    b.PlotStack(H(puls), "Пульс[BPM]");
  }
}

// ========== update ==========
void update(sets::Updater& upd) {
  upd.update("lbl_t"_h, temp);
  upd.update("lbl_p"_h, BPM);
  upd.updatePlot(H(temp), temp);
  upd.updatePlot(H(puls), BPM);
  float ECG[] = {rawValue, filteredValue, baseline};
  upd.updatePlot(H(ecg), ECG);
}

// ========== setup ==========
void setup() {
  //открываем порт
  Serial.begin(115200);
  Serial.println();

  //название сети
  WiFiConnector.setName("Medical monitor");
  //timeout
  WiFiConnector.setTimeout(10);

  // базу данных запускаем до подключения к точке
#ifdef ESP32
    LittleFS.begin(true);
#else
    LittleFS.begin();
#endif
  db.begin();
  db.init(kk::wifi_ssid, "");
  db.init(kk::wifi_pass, "");

  // подключение и реакция на подключение или ошибку
  WiFiConnector.onConnect([]() {
      Serial.print("Connected! ");
      Serial.println(WiFi.localIP());

      //вывод IP на экран
      lcd.clear();
      lcd.print(WiFi.localIP());
      lcd.setCursor(0, 1);
      lcd.print("Press button");
      while(digitalRead(PinButton) == HIGH);
  });
  WiFiConnector.onError([]() {
      Serial.print("Error! start AP ");
      Serial.println(WiFi.softAPIP());

      //вывод уведомления на экран
      lcd.clear();
      lcd.print("Connect to");
      lcd.setCursor(0, 1);
      lcd.print("Med. monitor");
      while(digitalRead(PinButton) == HIGH);
  });

  WiFiConnector.connect(db[kk::wifi_ssid], db[kk::wifi_pass]);

  // запускаем сервер после connect, иначе DNS не подхватится
  sett.begin();
  sett.onBuild(build);
  sett.onUpdate(update);

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
  
  //Настройка пинов
  pinMode(PinBuzzer, OUTPUT);
  pinMode(PinPulse, INPUT);
  pinMode(PinButton, INPUT_PULLUP);
  pinMode(ECG_PIN, INPUT);
  pinMode(LO_PLUS_PIN, INPUT);
  pinMode(LO_MINUS_PIN, INPUT);

  //приветствие
  lcd.write(byte(2));
  lcd.print("-vo's children");
  lcd.setCursor(2,1);
  lcd.print("Med. monitor");
  tone(PinBuzzer, 1500); // включаем звук частотой 1500 Гц
  delay(200);
  tone(PinBuzzer, 1000); // включаем звук частотой 1000 Гц
  delay(200);
  tone(PinBuzzer, 500); // включаем звук частотой 500 Гц
  delay(200);
  noTone(PinBuzzer); // выключаем звук
  delay(3000);
}

// ========== loop ==========
void loop() {
  //измерение температуры
  temp = Temp();

  //Измерение пульса
  bpm = Pulse();
  if (bpm > 0) {
        //Serial.print("BPM - ");
        //Serial.println(bpm);
        BPM = bpm;
        tone(PinBuzzer, 2000,100);
        Heart();
  }

  //ECG
  ECG();

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

  WiFiConnector.tick();
  sett.tick();
}

// ========== Temp ==========
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

// ========== Pulse ==========
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

// ========== Heart ==========
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

// ========== ECG ==========
void ECG(){
  rawValue = analogRead(ECG_PIN);
  
  // Простой low-pass фильтр
  filteredValue = alpha * rawValue + (1 - alpha) * filteredValue;
  
  // Автоматическое определение baseline
  static long baselineSum = 0;
  static int baselineCount = 0;
  baselineSum += rawValue;
  baselineCount++;
  
  if (baselineCount >= 100) {
    baseline = baselineSum / baselineCount;
    baselineSum = 0;
    baselineCount = 0;
    
    // Вывод данных
    Serial.print(rawValue);
    Serial.print(",");
    Serial.print(filteredValue);
    Serial.print(",");
    Serial.println(baseline);
  }
}