#include <Arduino.h>
#include <GyverDBFile.h>
#include <LittleFS.h>
GyverDBFile db(&LittleFS, "/data.db");

#include <SettingsGyver.h>
SettingsGyver sett("Medical Monitor", &db);

enum kk : size_t {
    // WiFi 
    wifi_ssid,
    wifi_pass,
    wifi_status,
    
    //мониторинг
    update_interval,
    
    // Данные
    temp_current,
    hr_current,
    ecg_current,
    emg_current,
    
    // Кнопки
    connect_btn,
    refresh_btn,
    save_btn,
    reboot_btn,
    ap_btn,
    

    mode_info
};

// Данные
float temperature = 36.6;
int heart_rate = 72;
int ecg_value = 512;
int emg_value = 300;

bool is_wifi_connected = false;
bool ap_mode = true;

// Буферы для граф
const int ECG_BUFFER_SIZE = 100;
float ecg_buffer[ECG_BUFFER_SIZE];
int ecg_index = 0;

const int EMG_BUFFER_SIZE = 100;
float emg_buffer[EMG_BUFFER_SIZE];
int emg_index = 0;

const int HR_BUFFER_SIZE = 50;
float hr_buffer[HR_BUFFER_SIZE];
int hr_index = 0;

const int TEMP_BUFFER_SIZE = 30;
float temp_buffer[TEMP_BUFFER_SIZE];
int temp_index = 0;

sets::Logger logger(150);

// ДАТЧИК ТЕМПЕРАТУРЫ KY-013
float readTemperatureKY013() {
    // заглушка женя
    return 36.0 + random(0, 30) / 10.0;
}

// ДАТЧИК ЭКГ AD8232
int readECGAD8232() {
    // заглушка женя
    return 400 + random(0, 400);
}

// ДАТЧИК ЭМГ (предположительно через ADC)
int readEMGSensor() {
    //заглушка женя
    return 200 + random(0, 400);
}

// ДАТЧИК ПУЛЬСА KY-039
int readPulseKY039() {
    //заглушка женя
    return 50 + random(0, 80);
}

void build(sets::Builder& b) {
    if (b.build.isAction()) {
        Serial.print("Action: 0x");
        Serial.print(b.build.id, HEX);
        Serial.print(" = ");
        Serial.println(b.build.value);
    }
    //WiFi
    if (b.beginGroup("WiFi Configuration")) {
        b.Input(kk::wifi_ssid, "WiFi SSID");
        b.Pass(kk::wifi_pass, "WiFi Password");
        
        // Статус подключения
        String wifi_status_text = is_wifi_connected ? 
            "Connected: " + WiFi.localIP().toString() : 
            "Disconnected";
        b.Label(kk::wifi_status, "WiFi Status", wifi_status_text);
        
        if (b.beginButtons()) {
            if (b.Button(kk::connect_btn, "Connect to WiFi")) {
                connectToWiFi();
            }
            
            // Кнопка для включения точки доступа
            if (!ap_mode && b.Button(kk::ap_btn, "Enable AP Mode")) {
                enableAPMode();
            }
            b.endButtons();
        }
        b.endGroup();
    }

    // Группа с настройками обновления
    if (b.beginGroup("Monitor Settings")) {
        b.Input(kk::update_interval, "Update Interval (ms)");
        b.endGroup();
    }

    // Группа с реальными данными
    if (b.beginGroup("Sensor Data")) {
        b.Label(kk::temp_current, "Temperature", String(temperature, 1) + " °C");
        b.Label(kk::hr_current, "Heart Rate", String(heart_rate) + " BPM");
        b.Label(kk::ecg_current, "ECG Raw", String(ecg_value));
        b.Label(kk::emg_current, "EMG Raw", String(emg_value));
        b.endGroup();
    }

    // График ЭКГ в рв
    b.PlotRunning(H(ecg_live), "ECG Live Signal");

    // График ЭМГ в рв
    b.PlotRunning(H(emg_live), "EMG Live Signal");

    // График сердца
    b.PlotRunning(H(hr_live), "Heart Rate Live");

    // График температуры
    b.PlotRunning(H(temp_live), "Temperature Live");

    // Все графики в одном
    b.PlotStack(H(vitals_stack), "Temperature;Heart Rate;ECG;EMG");

    // Информация о режиме
    if (b.beginGroup("System Info")) {
        String mode_info_text = ap_mode ? 
            "AP Mode: " + WiFi.softAPIP().toString() : 
            "STA Mode: " + WiFi.localIP().toString();
        b.Label(kk::mode_info, "Network Mode", mode_info_text);
        b.endGroup();
    }

    // Логгер
    b.Log(logger);

    // Кнопки управления
    if (b.beginButtons()) {
        if (b.Button(kk::refresh_btn, "Refresh Data")) {
            updateSensorData();
            Serial.println("Data refreshed");
        }

        if (b.Button(kk::save_btn, "Save Settings")) {
            db.update();
            Serial.println("Settings saved");
        }

        if (b.Button(kk::reboot_btn, "Reboot Device")) {
            Serial.println("Rebooting...");
            ESP.restart();
        }

        b.endButtons();
    }
}

//Обновление данных на странице
void update(sets::Updater& upd) {
    // текст
    upd.update(kk::temp_current, String(temperature, 1) + " °C");
    upd.update(kk::hr_current, String(heart_rate) + " BPM");
    upd.update(kk::ecg_current, String(ecg_value));
    upd.update(kk::emg_current, String(emg_value));
    
    //график ЭКГ
    float ecg_data[] = {ecg_buffer[(ecg_index - 1 + ECG_BUFFER_SIZE) % ECG_BUFFER_SIZE]};
    upd.updatePlot(H(ecg_live), ecg_data);
    
    //график ЭМГ
    float emg_data[] = {emg_buffer[(emg_index - 1 + EMG_BUFFER_SIZE) % EMG_BUFFER_SIZE]};
    upd.updatePlot(H(emg_live), emg_data);
    
    //график пульса
    float hr_data[] = {(float)heart_rate};
    upd.updatePlot(H(hr_live), hr_data);
    
    //график температуры
    float temp_data[] = {temperature};
    upd.updatePlot(H(temp_live), temp_data);
    
    //комбинированный график
    float vitals_data[] = {temperature, (float)heart_rate, 
                          ecg_buffer[(ecg_index - 1 + ECG_BUFFER_SIZE) % ECG_BUFFER_SIZE],
                          emg_buffer[(emg_index - 1 + EMG_BUFFER_SIZE) % EMG_BUFFER_SIZE]};
    upd.updatePlot(H(vitals_stack), vitals_data);
    
    //WiFi
    String wifi_status_text = is_wifi_connected ? 
        "Connected: " + WiFi.localIP().toString() : 
        "Disconnected";
    upd.update(kk::wifi_status, wifi_status_text);
    
    //Информация о режиме
    String mode_info_text = ap_mode ? 
        "AP Mode: " + WiFi.softAPIP().toString() : 
        "STA Mode: " + WiFi.localIP().toString();
    upd.update(kk::mode_info, mode_info_text);
}

void connectToWiFi() {
    String ssid = db[kk::wifi_ssid];
    String password = db[kk::wifi_pass];
    
    if (ssid.length() == 0) {
        Serial.println("No WiFi SSID configured");
        return;
    }
    
    Serial.println("Connecting to WiFi: " + ssid);
    
    //STA
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        is_wifi_connected = true;
        ap_mode = false;
        
        Serial.println("\nWiFi connected!");
        Serial.println("IP address: " + WiFi.localIP().toString());
        Serial.println("AP mode disabled");
        
        //сейв
        db.update();
        
    } else {
        is_wifi_connected = false;
        Serial.println("\nFailed to connect to WiFi!");
        enableAPMode();
    }
}

void enableAPMode() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("MedicalMonitor", "");
    ap_mode = true;
    
    Serial.println("AP mode enabled");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    delay(1000);

    //бд
#ifdef ESP32
    LittleFS.begin(true);
#else
    LittleFS.begin();
#endif

    db.begin();

    //Начальные значения
    db.init(kk::wifi_ssid, "");
    db.init(kk::wifi_pass, "");
    db.init(kk::wifi_status, "Disconnected");
    db.init(kk::update_interval, "100");
    db.init(kk::temp_current, "0");
    db.init(kk::hr_current, "0");
    db.init(kk::ecg_current, "0");
    db.init(kk::emg_current, "0");
    db.init(kk::mode_info, "Starting...");

    //буферы граф
    for (int i = 0; i < ECG_BUFFER_SIZE; i++) ecg_buffer[i] = 0;
    for (int i = 0; i < EMG_BUFFER_SIZE; i++) emg_buffer[i] = 0;
    for (int i = 0; i < HR_BUFFER_SIZE; i++) hr_buffer[i] = 72.0f;
    for (int i = 0; i < TEMP_BUFFER_SIZE; i++) temp_buffer[i] = 36.6f;

    //настройка пинов женя
    /*
    pinMode;  // KY-013 Temperature
    pinMode;  // AD8232 ECG
    pinMode;  // EMG Sensor
    pinMode;  // KY-039 Pulse
    */

    // WIFI
    enableAPMode();
    
    Serial.println("Medical Monitor Started");
    Serial.println("Access Point: MedicalMonitor");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    //Сохраненный WiFi подключение
    String savedSSID = db[kk::wifi_ssid];
    if (savedSSID.length() > 0) {
        Serial.println("Attempting to connect to saved WiFi: " + savedSSID);
        connectToWiFi();
    } else {
        Serial.println("No WiFi configured. Please set up WiFi via web interface");
    }

  
    sett.begin();
    sett.onBuild(build);
    sett.onUpdate(update);

    Serial.println("Web interface ready");
    if (ap_mode) {
        Serial.println("Access via AP: " + WiFi.softAPIP().toString());
    }
    if (is_wifi_connected) {
        Serial.println("Access via STA: " + WiFi.localIP().toString());
    }
}

void updateSensorData() {
    
    //Температура с KY-013
    temperature = readTemperatureKY013();
    
    //Пульса с KY-039  
    heart_rate = readPulseKY039();
    
    //ЭКГ с AD8232
    ecg_value = readECGAD8232();
    
    //ЭМГ 
    emg_value = readEMGSensor();
    
    // Обновление графиков
    ecg_buffer[ecg_index] = ecg_value / 1000.0; // Нормализация для графика
    ecg_index = (ecg_index + 1) % ECG_BUFFER_SIZE;
    
    emg_buffer[emg_index] = emg_value / 1000.0; // Нормализация для графика
    emg_index = (emg_index + 1) % EMG_BUFFER_SIZE;
    
    hr_buffer[hr_index] = heart_rate;
    hr_index = (hr_index + 1) % HR_BUFFER_SIZE;
    
    temp_buffer[temp_index] = temperature;
    temp_index = (temp_index + 1) % TEMP_BUFFER_SIZE;
    
    Serial.println("=== Sensor Data Updated ===");
    Serial.println("Temperature: " + String(temperature, 1) + " °C");
    Serial.println("Heart Rate: " + String(heart_rate) + " BPM");
    Serial.println("ECG: " + String(ecg_value));
    Serial.println("EMG: " + String(emg_value));
    Serial.println();
}

void loop() {
    sett.tick();

    static unsigned long lastUpdate = 0;
    unsigned long interval = db[kk::update_interval].toInt();
    if (millis() - lastUpdate > interval) {
        lastUpdate = millis();
        updateSensorData();
    }

    // Мониторинг подключения WiFi
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 10000) {
        lastWifiCheck = millis();
        
        bool currentStatus = (WiFi.status() == WL_CONNECTED);
        if (currentStatus != is_wifi_connected) {
            is_wifi_connected = currentStatus;
            
            if (is_wifi_connected && ap_mode) {
                ap_mode = false;
                Serial.println("WiFi connected - disabling AP mode");
            } else if (!is_wifi_connected && !ap_mode) {
                enableAPMode();
                Serial.println("WiFi lost - enabling AP mode");
            }
            
            Serial.println("WiFi status: " + String(is_wifi_connected ? "Connected" : "Disconnected"));
        }
        
        if (!is_wifi_connected && db[kk::wifi_ssid].length() > 0 && !ap_mode) {
            Serial.println("Attempting WiFi reconnection...");
            connectToWiFi();
        }
    }

    // Индикация
    static uint32_t ledTimer = 0;
    uint32_t blinkInterval;
    if (ap_mode) {
        blinkInterval = 500;
    } else if (is_wifi_connected) {
        blinkInterval = 2000;
    } else {
        blinkInterval = 1000;
    }
    
    if (millis() - ledTimer > blinkInterval) {
        ledTimer = millis();
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
    
    delay(50);
}