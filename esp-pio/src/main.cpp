#include <Arduino.h>
#include <ESP8266WiFi.h>

// ##########################################
// настройка сенсора температуры/влажности
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTPIN D2
#define DHTTYPE DHT11
DHT_Unified dht(DHTPIN, DHTTYPE);
uint32_t dht_delay;
// ##########################################

#define START_SENSOR 0
#define SENSORS 3
#define BUTTON_PIN D0
#define CONFIG_TIMEOUT_MS 60000
#define WIFI_TIMEOUT_MS 10000
#define WIFI_RECONNECT_MS 60000
#define SERVER_TIMEOUT_MS 5000
#define SERVER_RECONNECT_MS 30000

String ssid     = "VNE-N41";
String password = "34670000";
String host     = "10.21.36.131";
u16 port        = 5000;

WiFiClient client;
WiFiServer configServer(7931);



const String my_ssid = "ESP_CONIG";
const String my_password = "34670000";
const u8 config_magic[4] = {0xDE, 0xAD, 0xBE, 0xEF};

struct Sensor {
	float value;
	u64 last_time;
	u64 delay;
	float base_min; float base_max;
	float shift_min; float shift_max;
};

Sensor s[] = {
	{ 0.0, 0, 5000, -1000.0, 1000.0,  -10.0,  10.0 }, // pH sensor
	{ 0.0, 0, 1000,     0.0, 1024.0, -200.0, 200.0 }, // light sensor
	{ 0.0, 0, 1000,   -30.0,   50.0,   -1.0,   1.0 }, // T sensor
};


float getRandomPrecent() {
	return rand() / (float) RAND_MAX;
}


float getRandom(float min, float max) {
	return min + (max - min) * getRandomPrecent();
}


u64 lastWifiConnectAttempt = 0;
bool connectToWifi() {
	if (WiFi.isConnected()) return true;
	if (lastWifiConnectAttempt == 0) {
		WiFi.disconnect();
		WiFi.begin(ssid, password);
		lastWifiConnectAttempt = millis();
	}

	if (millis() - lastWifiConnectAttempt < WIFI_TIMEOUT_MS) {
		auto status = WiFi.status();
		if (status == WL_NO_SSID_AVAIL) {
			Serial.println("\nЗаданная сеть не найдена.\n");
			lastWifiConnectAttempt = millis() - WIFI_TIMEOUT_MS - 100;
		} else if (status == WL_WRONG_PASSWORD) {
			Serial.println("\nЗаданный пароль не верный.\n");
			lastWifiConnectAttempt = millis() - WIFI_TIMEOUT_MS - 100;
		}
	}

	if (millis() - lastWifiConnectAttempt > WIFI_TIMEOUT_MS
		+ WIFI_RECONNECT_MS) lastWifiConnectAttempt = 0;
	return false;
}

// u64 lastServerConnectAttempt = 0;
// bool connectToServer() {
// 	if (client.connected()) return true;
// 	if (lastServerConnectAttempt == 0) {
// 		client.connect(host, port);
// 		lastServerConnectAttempt = millis();
// 	} 

// 	// if (millis() - lastServerConnectAttempt > SERVER_TIMEOUT_MS) {}
// 	if (millis() - lastServerConnectAttempt > SERVER_TIMEOUT_MS 
// 		+ SERVER_RECONNECT_MS) {
// 		lastServerConnectAttempt = 0;
// 	}

// 	return false;
// }

void sendSensorData(int sensor_id, float data) {
	String body = "{\"sensor_id\":\"" + String(sensor_id + START_SENSOR) +
								"\",\"data\":" + String(data) + "}";
	int contentLength = body.length();
	String req = "POST /data HTTP/1.1\r\n";
	req += "Host: " + host + "\r\n";
	req += "Content-Type: application/json\r\n";
	req += "Connection: keep-alive\r\n";
	req += "Content-Length: " + String(contentLength) + "\r\n";
	req += "\r\n";
	req += body;

	// u64 start = millis();
	while (!client.connected())
		client.connect(host, port);
	// Serial.print("Подключение к серверу заняло: ");
	// Serial.print(millis() - start);
	// Serial.println(" мс.");
	client.print(req);
	// while (client.available()) client.read();
	client.stop();
	// Serial.print("Отправлен ");
	// Serial.print(sensor_id);
	// Serial.print(" со значением: ");
	// Serial.println(data);
}


void enterConfigMode() {
	Serial.println("\n=== РЕЖИМ КОНФИГУРАЦИИ (1 минута) ===");
	Serial.print("Точка доступа: ");
	Serial.print(my_ssid);
	Serial.print(" / ");
	Serial.println(my_password);
	Serial.println("Подключитесь и отправьте конфигурационный пакет на порт " + String(7931));

	configServer.begin();
	u32 startTime = millis();
	bool configured = false;

	while (millis() - startTime < CONFIG_TIMEOUT_MS && !configured) {
		WiFiClient configClient = configServer.accept();
		if (configClient) {
			configClient.setTimeout(2000);

			u8 magic[4];
			if (configClient.readBytes(magic, 4) != 4) {
				configClient.stop(); continue; }
			if (memcmp(magic, config_magic, 4) != 0) {
				configClient.stop(); continue; }

			u8 ssid_len;
			if (configClient.readBytes(&ssid_len, 1) != 1) {
				configClient.stop(); continue; }
			if (ssid_len > 32) {
				configClient.stop(); continue; }
				
			u8 ssid_buf[ssid_len+1];
			if (configClient.readBytes(ssid_buf, ssid_len) != ssid_len) {
				configClient.stop(); continue; }
			ssid_buf[ssid_len] = 0;

			u8 pass_len;
			if (configClient.readBytes(&pass_len, 1) != 1) {
				configClient.stop(); continue; }
			if (pass_len > 64) {
				configClient.stop(); continue; }

			u8 pass_buf[pass_len+1];
			if (configClient.readBytes(pass_buf, pass_len) != pass_len) {
				configClient.stop(); continue; }
			pass_buf[pass_len] = 0;

			u8 host_len;
			if (configClient.readBytes(&host_len, 1) != 1) {
				configClient.stop(); continue; }
			if (host_len > 64) {
				configClient.stop(); continue; }

			u8 host_buf[host_len+1];
			if (configClient.readBytes(host_buf, host_len) != host_len) {
				configClient.stop(); continue; }
			host_buf[host_len] = 0;

			u8 port_bytes[2];
			if (configClient.readBytes(port_bytes, 2) != 2) {
				configClient.stop(); continue; }

			ssid = (char*)ssid_buf;
			password = (char*)pass_buf;
			host = (char*)host_buf;
			port = (port_bytes[0] << 8) | port_bytes[1];

			Serial.println("Новые параметры получены:");
			Serial.println("SSID: " + ssid);
			Serial.println("PASSWORD: " + password);
			Serial.println("HOST: " + host);
			Serial.println("PORT: " + String(port));

			configClient.stop();
			configured = true;
		}
		delay(10);
	}
	configServer.stop();
	if (!configured) {
		Serial.println("Таймаут конфигурации, продолжаем работу со старыми параметрами");
	} else {
		lastWifiConnectAttempt = 0;
		// lastServerConnectAttempt = 0;
	}
	Serial.println("=== ВЫХОД ИЗ РЕЖИМА КОНФИГУРАЦИИ ===\n");
}

u64 lastPressTime = 0;
bool lastButtonState = false;
bool checkDoubleClick() {
	bool doubleClickDetected = false;
	bool currentButtonState = digitalRead(BUTTON_PIN);
	if (currentButtonState && !lastButtonState) {
		auto x = millis() - lastPressTime;
		if (x > 100 && x < 500) {
			lastPressTime = 0;
			doubleClickDetected = true;
		}
		lastPressTime = millis();
	}
	lastButtonState = currentButtonState;
	return doubleClickDetected;
}

void setup() {
	srand(micros());
	Serial.begin(115200);
	// Serial.setDebugOutput(true);
	Serial.println();

	// ##############################################
	// инициализация сеносра DHT11
	dht.begin();
	Serial.println(F("DHTxx Unified Sensor Example"));
	// Print temperature sensor details.
	sensor_t sensor;
	dht.temperature().getSensor(&sensor);
	Serial.println(F("------------------------------------"));
	Serial.println(F("Temperature Sensor"));
	Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
	Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
	Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
	Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("°C"));
	Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("°C"));
	Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));
	Serial.println(F("------------------------------------"));
	// Print humidity sensor details.
	dht.humidity().getSensor(&sensor);
	Serial.println(F("Humidity Sensor"));
	Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
	Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
	Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
	Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("%"));
	Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("%"));
	Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("%"));
	Serial.println(F("------------------------------------"));
	// Set delay between sensor readings based on sensor details.
	dht_delay = sensor.min_delay / 1000;
	// ##############################################

	// pinMode(D0, INPUT);
	// WiFi.mode(WIFI_AP_STA);
	// WiFi.softAP(my_ssid, my_password);
	// for (u32 i = 0; i < SENSORS; i++) {
	// 	s[i].value = getRandom(s[i].base_min, s[i].base_max);
	// }
}

void loop() {
	// if (checkDoubleClick()) enterConfigMode();
	// if (!connectToWifi()) return;
	// // if (!connectToServer()) return;

	// for (u32 i = 0; i < SENSORS; i++) {
	// 	if (millis() - s[i].last_time < s[i].delay) continue;
	// 	s[i].value += getRandom(s[i].shift_min, s[i].shift_max);
	// 	if (s[i].value < s[i].base_min)
	// 		s[i].value = s[i].base_min;
	// 	if (s[i].value > s[i].base_max)
	// 		s[i].value = s[i].base_max;
	// 	sendSensorData(i, s[i].value);
	// 	s[i].last_time = millis();
	// }

	// Delay between measurements.
	auto start_time = millis();
	delay(dht_delay);
	// Get temperature event and print its value.
	sensors_event_t event;
	dht.temperature().getEvent(&event);
	if (isnan(event.temperature)) {
		Serial.println(F("Error reading temperature!"));
	}
	else {
		Serial.print(F("Temperature: "));
		Serial.print(event.temperature);
		Serial.println(F("°C"));
	}
	// Get humidity event and print its value.
	dht.humidity().getEvent(&event);
	if (isnan(event.relative_humidity)) {
		Serial.println(F("Error reading humidity!"));
	}
	else {
		Serial.print(F("Humidity: "));
		Serial.print(event.relative_humidity);
		Serial.println(F("%"));
	}
	Serial.print(millis() - start_time);
	Serial.println(" ms");
	Serial.println();
}