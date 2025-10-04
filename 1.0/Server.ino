#include <WiFi.h>

// Настройки WiFi
const char* ssid = "your_SSID";        // Замените на имя вашей WiFi сети
const char* password = "your_PASSWORD"; // Замените на пароль вашей WiFi сети

// Пины для AD8232 в формате D
const int LO_PLUS_PIN = D17;    // LO+ (Lead Off Detection +)
const int LO_MINUS_PIN = D16;   // LO- (Lead Off Detection -)
const int ECG_OUTPUT_PIN = D36; // Аналоговый выход датчика (VP)

// Переменные для данных ЭКГ
int ecgValue = 0;
bool leadOffDetected = false;

void setup() {
  // Инициализация последовательного порта
  Serial.begin(115200);
  
  // Настройка пинов
  pinMode(LO_PLUS_PIN, INPUT);
  pinMode(LO_MINUS_PIN, INPUT);
  pinMode(ECG_OUTPUT_PIN, INPUT);
  
  // Подключение к WiFi
  connectToWiFi();
  
  Serial.println("Система ЭКГ готова к работе");
  Serial.println("Проверка подключения электродов...");
}

void loop() {
  // Проверка состояния электродов
  checkLeadOff();
  
  // Чтение данных ЭКГ
  readECG();
  
  // Вывод данных в монитор порта
  printData();
  
  delay(10); // Небольшая задержка для стабильности
}

void connectToWiFi() {
  Serial.println();
  Serial.print("Подключение к ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi подключен!");
  Serial.print("IP адрес: ");
  Serial.println(WiFi.localIP());
}

void checkLeadOff() {
  // Проверка детекции отрыва электродов
  if (digitalRead(LO_PLUS_PIN) || digitalRead(LO_MINUS_PIN)) {
    leadOffDetected = true;
    Serial.println("ВНИМАНИЕ: Обнаружен отрыв электродов!");
  } else {
    leadOffDetected = false;
  }
}

void readECG() {
  // Чтение аналогового значения с датчика
  ecgValue = analogRead(ECG_OUTPUT_PIN);
}

void printData() {
  // Вывод данных в монитор порта
  Serial.print("ЭКГ: ");
  Serial.print(ecgValue);
  Serial.print(" | Отрыв электродов: ");
  Serial.println(leadOffDetected ? "ДА" : "НЕТ");
}