# Entso-E to Modbus bridge

Allows to publish electricity prices to local network using Modbus so the values can be used in automation controller. Tested with Fidelix FX-3000C / M-Link (RTU) and [ModbusTool](<https://github.com/ClassicDIY/ModbusTool>) (TCP) using ESP8266 (Wemos Mini D1), configuration follows [Alexander Emelianov's](<https://github.com/emelianov/modbus-esp8266>) modbus-esp8266 setup.

Modbus Info:

SlaveID: 100

Holding register:
| Offset  | Description   |
|---|---|
0    |  Read status (0 = Read success, other statuses are HTTP response statuses (for example 401 means Authorization failed, most likely invalid Entso-E token))
1...n | Price in configured output format (with TAX / margin if configured). slot 1 is current hour, 2 next hour etc. Will automatically rotate prices every hour.

Work in progress.

Data view

![image](https://github.com/w01w13/entso-e-modbus/assets/6333419/8ac4432c-209f-417d-b568-547575109ad7)

Configuration view

![image](https://github.com/w01w13/entso-e-modbus/assets/6333419/4ebba55d-0489-4e4c-ae6d-9dee310ae025)

Reading values with Modbus Master

![image](https://github.com/w01w13/entso-e-modbus/assets/6333419/e4a023a0-4b59-4932-8126-85e059eafb7b)

NOTE: Seems like the modbus master is handling the 32 single-precision values as 64-bit double precision thus resulting to erroneous values eventhough the hexvalue is correct. Need to double check whether this is a issue in implementation or in Modbus Master
