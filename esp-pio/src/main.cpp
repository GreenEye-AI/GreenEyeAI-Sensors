#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include "Adafruit_SHT31.h"
#include <ArduinoJson.h>


// D1 - SCL
// D2 - SDA
#define BUTTON_PIN D4
#define WATER_RELE D5
#define LIGHT_RELE D6
#define FAN_RELE D7

// настройка сети
#define CONFIG_TIMEOUT_MS 60000
#define WIFI_TIMEOUT_MS 10000
#define WIFI_RECONNECT_MS 60000
#define SERVER_TIMEOUT_MS 1000
#define MAGIC 0xEFBEADDE // b'\xDE\xAD\xBE\xEF'

bool water_rele_state = false;
bool light_rele_state = false;
bool fan_rele_state = false;

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

// настройка датчика SHT3x
bool enableSensor = true;
bool enableHeater = false;
Adafruit_SHT31 sht31 = Adafruit_SHT31();


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
		Serial.print(millis());
		Serial.println(" Сервер недоступен");
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


enum CommandStep { IDLE, WAIT_COMMAND, SEND_ACK };
CommandStep currentStep = IDLE;
unsigned long lastActionTime = 0;
unsigned long lastCmdCheck = 0;
int pendingCommandId = -1;
int queueSize = 0;

void handleServerCommands() {
	if (!client.connected()) {
		currentStep = IDLE;
		return;
	}

	switch (currentStep) {
		case IDLE: {
			if (millis() - lastCmdCheck < 3'000 && queueSize <= 0) return;
			
			lastCmdCheck = millis();
			client.print(
				"GET /api/esp/command HTTP/1.1\r\n"  
				"Connection: keep-alive\r\n\r\n"
			);
			
			currentStep = WAIT_COMMAND;
			lastActionTime = millis();
			Serial.print(millis());
			Serial.println(" Запрос отправлен");
			break;
		}

		case WAIT_COMMAND:
			if (client.available()) {
				int contentLength = 0;
				bool endOfHeaders = false;

				// 1. Парсинг HTTP заголовков
				while (client.available() && !endOfHeaders) {
					String line = client.readStringUntil('\n');
					line.trim(); // Удаляем \r
					if (line.length() == 0) {
						endOfHeaders = true; // Нашли \r\n\r\n
					} else if (line.startsWith("Content-Length:")) {
						contentLength = line.substring(15).toInt();
					}
				}

				// 2. Чтение тела JSON на основе Content-Length
				if (endOfHeaders && contentLength > 0) {
					JsonDocument doc;
					DeserializationError error = deserializeJson(doc, client);

					if (error == DeserializationError::Ok) {
						// 3. Проверка типов данных
						bool typesValid = doc["device"].is<String>() && 
						                  doc["action"].is<String>() && 
						                  doc["id"].is<int>() && 
						                  doc["queue_size"].is<int>();

						if (typesValid) {
							String device = doc["device"]; 
							String action = doc["action"]; 
							pendingCommandId = doc["id"];       
							queueSize = doc["queue_size"];
							Serial.print("Очередь: ");
							Serial.println(queueSize);

							if (device != "None") {
								bool state = (action == "on");
								
								if (device == "light") {
									light_rele_state = state;
									digitalWrite(LIGHT_RELE, !light_rele_state);
								} else if (device == "fan") {
									fan_rele_state = state;
									digitalWrite(FAN_RELE, !fan_rele_state);
								} else if (device == "water") {
									water_rele_state = state;
									digitalWrite(WATER_RELE, !water_rele_state);
								}
								currentStep = SEND_ACK;
							} else {
								currentStep = IDLE; 
							}
						} else {
							Serial.println("Ошибка: неверные типы данных в JSON");
							currentStep = IDLE;
						}
					} else {
						Serial.print("Ошибка десериализации: ");
						Serial.println(error.c_str());
						currentStep = IDLE;
					}
				}
			} else if (millis() - lastActionTime > 2000) {
				currentStep = IDLE; 
			}
			break;

		case SEND_ACK: {
			JsonDocument ackDoc;
			ackDoc["id"] = pendingCommandId; 

			String requestBody;
			serializeJson(ackDoc, requestBody);

			String headers = "POST /api/esp/command HTTP/1.1\r\n";
			headers += "Content-Type: application/json\r\n";
			headers += "Content-Length: " + String(requestBody.length()) + "\r\n";
			headers += "Connection: keep-alive\r\n\r\n";

			client.print(headers + requestBody);
			while (client.available()) client.read(); 

			currentStep = IDLE; 
			if (queueSize > 0) lastCmdCheck = 0; 
			break;
		}
	}
}


u64 lastPressTime = 0;
bool lastButtonState = false;
bool checkDoubleClick() {
	bool doubleClickDetected = false;
	bool currentButtonState = !digitalRead(BUTTON_PIN);
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
	pinMode(WATER_RELE, OUTPUT_OPEN_DRAIN);
	pinMode(LIGHT_RELE, OUTPUT_OPEN_DRAIN);
	pinMode(FAN_RELE,   OUTPUT_OPEN_DRAIN);
	digitalWrite(WATER_RELE, HIGH);
	digitalWrite(LIGHT_RELE, HIGH);
	digitalWrite(FAN_RELE,   HIGH);
	pinMode(BUTTON_PIN, INPUT_PULLUP);
	client.setTimeout(10);
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(my_ssid, my_password);
	// инициализация датчика
	Serial.println("Тест SHT3x");
	if (sht31.begin(0x44))
		Serial.println("Датчик подключен по 0x44");
	else if (sht31.begin(0x45))
		Serial.println("Датчик подключен по 0x45");
	else {
		Serial.println("Не удалось найти SHT3x по 0x44 | 0x45");
		enableSensor = false;
	}

	Serial.print("Состояние обогреватея: ");
	if (sht31.isHeaterEnabled()) {
		enableHeater = true;
		Serial.println("Включен");
	} else
		Serial.println("Выключен");
}


u32 lastSensorRead = 0;
u32 lastPing = 0;
void loop() {
	if (checkDoubleClick() || config.magic != MAGIC) enterConfigMode();
	if (!connectToWifi()) return;
	if (!connectToServer()) return;

	if (millis() - lastPing > 4'000) {
		lastPing = millis();
		client.print(
			"GET /ping HTTP/1.1\r\n"
			"Connection: keep-alive\r\n\r\n"
		);
		while (client.available()) client.read(); 
	}

	handleServerCommands();

	// опрос сенсора раз в 15 минут
	if (millis() - lastSensorRead > 900'000 && enableSensor) {
		lastSensorRead = millis();
		float temperature = sht31.readTemperature();
		float humidity = sht31.readHumidity();
		
		if (isnan(temperature) || isnan(humidity)) {
			Serial.print(millis());
			Serial.println(" Не удалось считать датчики");
			return;
		}

		Serial.println(millis());
		Serial.printf("Температура: %.2f °C\n", temperature);
		Serial.printf("Влажность: %.2f %%\n\n", humidity);
		
		JsonDocument ackDoc;
		ackDoc["temperature"] = temperature; 
		ackDoc["humidity"] = humidity; 

		String requestBody;
		serializeJson(ackDoc, requestBody);

		String headers = "POST /api/esp/sensors HTTP/1.1\r\n";
		headers += "Content-Type: application/json\r\n";
		headers += "Content-Length: " + String(requestBody.length()) + "\r\n";
		headers += "Connection: keep-alive\r\n\r\n";

		client.print(headers + requestBody);
		while (client.available()) client.read(); 
	}
}
