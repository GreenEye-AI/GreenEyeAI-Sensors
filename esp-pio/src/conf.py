#!/usr/bin/env python3
"""
Скрипт для отправки конфигурационного пакета ESP8266.
Использование: python conf.py --ssid "SSID" --password "PASS" --host "SERVER" --port PORT
"""

import socket
import struct
import argparse

def send_config_packet(server_ip, server_port, new_ssid, new_password, new_host, new_port):
	"""
	Формирует и отправляет бинарный пакет конфигурации на ESP8266.
	"""
	# Магическая последовательность (4 байта)
	magic = b'\xDE\xAD\xBE\xEF'

	# Кодируем строки в байты (предполагается UTF-8, но можно и ASCII)
	ssid_bytes = new_ssid.encode('utf-8')
	pass_bytes = new_password.encode('utf-8')
	host_bytes = new_host.encode('utf-8')

	# Проверка ограничений длин (как в коде ESP)
	if len(ssid_bytes) > 32:
		raise ValueError("SSID слишком длинный (максимум 32 байта)")
	if len(pass_bytes) > 64:
		raise ValueError("Пароль слишком длинный (максимум 64 байта)")
	if len(host_bytes) > 64:
		raise ValueError("Host слишком длинный (максимум 64 байта)")

	# Сборка пакета:
	# [magic][len_ssid][ssid][len_pass][pass][len_host][host][port big-endian]
	packet = magic
	packet += struct.pack('B', len(ssid_bytes))   # 1 байт длины SSID
	packet += ssid_bytes
	packet += struct.pack('B', len(pass_bytes))   # 1 байт длины пароля
	packet += pass_bytes
	packet += struct.pack('B', len(host_bytes))   # 1 байт длины хоста
	packet += host_bytes
	packet += struct.pack('<H', new_port)  # 2 байта порта (little-endian)

	# Отправка
	sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	try:
		sock.connect((server_ip, server_port))
		sock.sendall(packet)
		print(f"Пакет успешно отправлен на {server_ip}:{server_port}")
		print(f"  SSID: {new_ssid}")
		print(f"  Password: {new_password}")
		print(f"  Host: {new_host}")
		print(f"  Port: {new_port}")
	except Exception as e:
		print(f"Ошибка при отправке: {e}")
	finally:
		sock.close()

if __name__ == "__main__":
	parser = argparse.ArgumentParser(description="Отправка конфигурационного пакета для ESP8266")
	parser.add_argument("--server-host", default="192.168.4.1", dest="server_host",
		help="IP-адрес ESP в режиме конфигурации (по умолчанию 192.168.4.1)")
	parser.add_argument("--server-port", type=int, default=7931, dest="server_port",
		help="Порт для конфигурации (по умолчанию 7931)")
	parser.add_argument("--ssid", required=True,
		help="Новый SSID Wi-Fi")
	parser.add_argument("--password", required=True,
		help="Новый пароль Wi-Fi")
	parser.add_argument("--host", required=True,
		help="Новый адрес сервера (IP или домен)")
	parser.add_argument("--port", type=int, required=True,
		help="Новый порт сервера")

	args = parser.parse_args()
	send_config_packet(args.server_host, args.server_port, args.ssid, args.password, args.host, args.port)