# Entso-E to Modbus bridge

Allows to publish electricity prices to local network using Modbus so the values can be used in automation controller. Tested with Fidelix FX-3000C / M-Link (RTU) and [ModbusTool](<https://github.com/ClassicDIY/ModbusTool>) (TCP) using ESP8266 (Wemos Mini D1), configuration follows [Alexander Emelianov's](<https://github.com/emelianov/modbus-esp8266>) modbus-esp8266 setup.

Modbus Info:

SlaveID: 100

Holding register:
| Offset  | Description   |
|---|---|
0    |  Read status (0 = Read success, other statuses are HTTP response statuses (for example 401 means Authorization failed, most likely invalid Entso-E token))
1...n | Price in Eur/MHw, slot 1 is current hour, 2 next hour etc. Will automatically rotate prices every hour.

Work in progress.
