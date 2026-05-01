#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>


// настройка сети
#define BUTTON_PIN D1
#define CONFIG_TIMEOUT_MS 60000
#define WIFI_TIMEOUT_MS 10000
#define WIFI_RECONNECT_MS 60000
#define SERVER_TIMEOUT_MS 1000
#define MAGIC 0xEFBEADDE // b'\xDE\xAD\xBE\xEF'

struct Config {
	unsigned int magic;
	char ssid[33];
	char password[65];
	char host[65];
	unsigned short port;
};

Config config;

WiFiClient client;
WiFiServer configServer(7931);

const String my_ssid = "ESP_CONIG";
const String my_password = "34670000";


u64 lastWifiConnectAttempt = 0;
bool connectToWifi() {
	if (WiFi.isConnected()) return true;
	if (lastWifiConnectAttempt == 0) {
		WiFi.disconnect();
		WiFi.begin(config.ssid, config.password);
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


u64 lastServerConnectAttempt = 0;
bool connectToServer() {
	if (client.connected()) return true;
	if (lastServerConnectAttempt == 0) {
		client.stop();
		client.connect(config.host, config.port);
		lastServerConnectAttempt = millis();
	}

	if (millis() - lastServerConnectAttempt > SERVER_TIMEOUT_MS) {
		Serial.println("Сервер недоступен");
		lastServerConnectAttempt = 0;
	}
	return false;
}


template<typename T>
bool recvValue(WiFiClient rc, T *str) {
	if (rc.readBytes((char*)str, sizeof(T)) != sizeof(T))
		return false;
	return true;
}


bool recvString(WiFiClient rc, char *str, int max_size = -1) {
	u8 str_size;
	if (!recvValue(rc, &str_size))
		return false;
	if (max_size >= 0 && str_size > max_size) 
		return false;
	if (rc.readBytes(str, str_size) != str_size)
		return false;
	str[str_size] = 0;
	return true;
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
		WiFiClient rc = configServer.accept();
		if (rc) {
			rc.setTimeout(2000);
			Config _config;

			if (!recvValue(rc, &_config.magic)) {
				rc.stop(); continue; }
			if (_config.magic != MAGIC) {
				Serial.println("MAGIC пакета не совпал");
				rc.stop(); continue; }

			if (!recvString(rc, _config.ssid, 32)) {
				rc.stop(); continue; }
				
			if (!recvString(rc, _config.password, 64)) {
				rc.stop(); continue; }

			if (!recvString(rc, _config.host, 64)) {
				rc.stop(); continue; }
			
			if (!recvValue(rc, &_config.port)) {
				rc.stop(); continue; }

			config.magic = _config.magic;
			config.port = _config.port;
			strcpy(config.ssid, _config.ssid);
			strcpy(config.password, _config.password);
			strcpy(config.host, _config.host);

			rc.stop();
			configured = true;

			EEPROM.put(0, config);
			EEPROM.commit();

			Serial.println("Новые параметры получены:");
			Serial.print("SSID: ");
			Serial.println(config.ssid);
			Serial.print("PASSWORD: ");
			Serial.println(config.password);
			Serial.print("HOST: ");
			Serial.println(config.host);
			Serial.print("PORT: ");
			Serial.println(config.port);
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
			doubleClickDetected = true; }
		lastPressTime = millis(); }
	lastButtonState = currentButtonState;
	return doubleClickDetected;
}


void setup() {
	// ESP использует Little-Endian формат
	Serial.begin(115200);
	Serial.println();
	// инициализация памяти
	EEPROM.begin(256);
	EEPROM.get(0, config);
	if (config.magic == MAGIC) {
		Serial.println("Загружены параметры:");
		Serial.print("SSID: ");
		Serial.println(config.ssid);
		Serial.print("PASSWORD: ");
		Serial.println(config.password);
		Serial.print("HOST: ");
		Serial.println(config.host);
		Serial.print("PORT: ");
		Serial.println(config.port);
	} else {
		Serial.println("Параметры не заданы");
	}
	pinMode(BUTTON_PIN, INPUT);
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(my_ssid, my_password);
}


void loop() {
	if (checkDoubleClick() || config.magic != MAGIC) enterConfigMode();
	if (!connectToWifi()) return;
	if (!connectToServer()) return;
}