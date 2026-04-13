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
u32 dht_delay;
u32 last_measure_dht = 0;
// ##########################################
// настройка фоторезистора
// #define PHOTO_PIN A0
// ##########################################
// настройка сети
#define BUTTON_PIN D1
#define CONFIG_TIMEOUT_MS 60000
#define WIFI_TIMEOUT_MS 10000
#define WIFI_RECONNECT_MS 60000
#define SERVER_TIMEOUT_MS 1000
// #define SERVER_RECONNECT_MS 30000

String ssid     = "VNE-N41";
String password = "34670000";
String host     = "10.21.36.131";
u16 port        = 5000;

WiFiClient client;
WiFiServer configServer(7931);

const String my_ssid = "ESP_CONIG";
const String my_password = "34670000";
const u8 config_magic[4] = {0xDE, 0xAD, 0xBE, 0xEF};

u64 lastWifiConnectAttempt = 0;
u64 lastServerConnectAttempt = 0;
// ##########################################


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

bool connectToServer() {
	if (client.connected()) return true;
	if (lastServerConnectAttempt == 0) {
		client.stop();
		client.connect(host, port);
		lastServerConnectAttempt = millis();
	}

	if (millis() - lastServerConnectAttempt > SERVER_TIMEOUT_MS) {
		Serial.println("Сервер недоступен");
		lastServerConnectAttempt = 0;
	}
	return false;
}


void sendSensorData(float temperature, float humidity) {
	String body = "{\"device_id\":\"esp_01\","
		"\"readings\":{\"ph\": 0.0";
	body += ",\"temperature\":" + String(temperature);
	body += ",\"humidity\":" + String(humidity);
	body += ",\"light\":0,\"fan\":0}}";

	int contentLength = body.length();
	String req = "POST /api/sensors HTTP/1.1\r\n";
	req += "Host: " + host + "\r\n";
	req += "Content-Type: application/json\r\n";
	req += "Connection: close\r\n";
	req += "Content-Length: " + String(contentLength) + "\r\n";
	req += "\r\n";
	req += body;

	client.print(req);
	while (client.available()) client.read();
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
	sensor_t sensor;
	dht.temperature().getSensor(&sensor);
	dht.humidity().getSensor(&sensor);
	dht_delay = sensor.min_delay / 1000;
	// ##############################################

	// pinMode(PHOTO_PIN, INPUT);
	pinMode(BUTTON_PIN, INPUT);
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(my_ssid, my_password);
}

void loop() {
	if (checkDoubleClick()) enterConfigMode();
	if (!connectToWifi()) return;
	if (!connectToServer()) return;

	if (millis() - last_measure_dht > dht_delay) {
		last_measure_dht = millis();
		sensors_event_t event;
		float temperature, humidity;
		dht.temperature().getEvent(&event);
		if (isnan(event.temperature)) {
			Serial.println("DHT11 недоступен");
			return;
		}
		temperature = event.temperature;
		dht.humidity().getEvent(&event);
		if (isnan(event.relative_humidity)) {
			Serial.println("DHT11 недоступен");
			return;
		}
		humidity = event.relative_humidity;

		sendSensorData(temperature, humidity);
		Serial.print("Отправка заняла: ");
		Serial.print(millis() - lastServerConnectAttempt);
		Serial.println(" мс.");
		lastServerConnectAttempt = 0;
		client.stop();
	}
}