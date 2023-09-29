#include "ModbusIP_ESP8266.h"
#include "ModbusRTU.h"
#include "entso-e.h"
#include <EEPROM.h>
#include <ESP8266TrueRandom.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <SoftwareSerial.h>
#include <StringTokenizer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>

#define BAUD_RATE 9600
#define SLAVE_ID 100
#define NUM_REGS 49
#define TOKEN_LENGTH 36
#define TAX_LENGTH 2
#define MARGIN_LENGTH 4
#define UNIT_LENGTH 1

ESP8266WebServer server;

// D1: Driver Input (TX pin), DI blue
#define TX_PIN 5 // D1
// D2: Driver Enable (LOW-Enable), DE green
#define TX_ENABLE 4 // D2
// D3 Receiver Enable (HIGH-Enable), RE yellow
#define RX_ENABLE 0
// D4 Receiver Output (RX pin), RO orange
#define RX_PIN 2
// Wifi
WiFiManager wm; // global wm instance

bool wm_nonblocking = false; // change to true to use non blocking
int status;
// Entso-E token
String token = "";
// Tax, defaults to company
String tax = "0.0";
// Margin, defaults to zero
String margin = "0.00";
// Should have prices in KWh / Mwh
// 1 is eur/MWh, 0 is cents/KWh
String selectedPrice = "0";
// mDNS
String mDnsName = "entso-modbus";
// Price data & control params
double* priceData;
int* priceLen;
unsigned long nextUpdate = 0;
unsigned long now = 0;
bool hasValues = false;
// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
// Modbus
ModbusRTU rtuSlave;
ModbusIP tcpSlave;
// SoftwareSerial S(int8_t rxPin, int8_t txPin = -1, bool invert = false);
SoftwareSerial MAX_485(RX_PIN, TX_PIN);
// Reset token
bool reset = false;
String reset_token;

union f_2uint {
    float f;
    uint16_t i[2];
};

void refresh()
{
    Serial.printf("START: Retrieving values, update is %lu\n", nextUpdate);
    free(priceData);
    free(priceLen);
    status = entso_e_refresh(token.c_str(), &priceData, &priceLen);
    Serial.printf("Got status %i\n", status);
    // Reset the status
    // TODO: Should we move this as coil? Hack, figure out proper way to update the HREG
    rtuSlave.removeHreg(0, 1);
    rtuSlave.addHreg(0, status, 1);
    // If intial read fails, dont put values in registry.
    // If other requests fail, we've the values
    if (status != 0 && !hasValues) {
        Serial.println("Invalid read");
    } else {
        hasValues = true;
        resetRegistry(); // Clear existing values. Hack, figure out proper way to update the HREG.
        Serial.println("Read successfull");
        updateRegistry();
    }
}
void configModeCallback(WiFiManager* myWiFiManager)
{
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

int saveToEeprom(String value, unsigned int len, unsigned int offset)
{
    if (value) {
        for (unsigned int i = 0; i < len; ++i) {
            EEPROM.write(i + offset, value[i]);
        }
    }
    return len;
}
String readFromEeprom(unsigned int len, unsigned int offset)
{
    String tmp;
    for (unsigned int i = offset; i < len + offset; ++i) {
        char c = char(EEPROM.read(i));
        if (c != '\0') {
            tmp += c;
        }
    }
    return tmp;
}
void saveParamCallback()
{
    token = getParam("entso");
    tax = getParam("tax");
    margin = getParam("margin");
    Serial.println("Entso-E token is " + token);
    Serial.println("Tax is " + tax);
    Serial.println("Margin is " + margin);
    int offset = 0;
    EEPROM.begin(512);
    offset += saveToEeprom(token, TOKEN_LENGTH, offset);
    offset += saveToEeprom(tax, TAX_LENGTH, offset);
    offset += saveToEeprom(margin, MARGIN_LENGTH, offset);
    offset += saveToEeprom(selectedPrice, UNIT_LENGTH, offset);
    EEPROM.commit();
    EEPROM.end();
    eeprom_read();
}
void eeprom_read()
{
    EEPROM.begin(512);
    int offset = 0;
    token = readFromEeprom(TOKEN_LENGTH, offset);
    offset += TOKEN_LENGTH;
    Serial.print("Token is ");
    Serial.println(token);
    tax = readFromEeprom(TAX_LENGTH, offset);
    offset += TAX_LENGTH;
    Serial.print("Tax is ");
    Serial.println(tax.toDouble());
    margin = readFromEeprom(MARGIN_LENGTH, offset);
    offset += MARGIN_LENGTH;
    Serial.print("Margin is ");
    Serial.println(margin.toDouble());
    selectedPrice = readFromEeprom(UNIT_LENGTH, offset);
    offset += UNIT_LENGTH;
    Serial.print("Unit is ");
    Serial.println(selectedPrice);
    EEPROM.end();
}
String getParam(String name)
{
    // read parameter from server, for customhmtl input
    String value;
    if (wm.server->hasArg(name)) {
        value = wm.server->arg(name);
    }
    return value;
}

void handleRoot()
{
    String s
        = "<!DOCTYPE html><html><head><title>Entso2Modbus</title><style>label {margin: 2px;} select { height: 1.8em; "
          "border: unset; width: 100%; font-size: 16px; line-height: 1.2em;} input::-webkit-outer-spin-button, "
          "input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; } .data-container { display: block; "
          "} input[type=number] { -moz-appearance: textfield; } .form-wrapper { margin: 10px; } .full-input { display: "
          "inline-block; padding: 3px; border: 1px solid black; border-radius: 6px; } input { outline: none; border: "
          "none; display: block; line-height: 1.2em; font-size: 14pt; } label { display: block; font-size: 12px; "
          "color: black; } html { display: grid; } body { display: grid; font-family: Arial, Helvetica, sans-serif; } "
          ".save-container { display: flex; justify-content: end; } .action-form-container { display: grid; "
          "grid-template-columns: auto auto; } .form-container { display: grid; margin: 4em; } .form { display: grid; "
          "grid-template-columns: auto; gap: 1em; } .navigation-item:hover { background-color: #ddd; } .tabcontent { "
          "display: none; padding: 6px 12px; border: 1px solid #ccc; border-top: none; } .navigation-item { "
          "background-color: inherit; float: left; border: none; outline: none; cursor: pointer; padding: 14px 16px; "
          "transition: 0.3s; font-size: 17px; } .action-button:hover { cursor: pointer; filter: brightness(50%); "
          "color: white; } .action-button { transition-duration: 0.4s; border-radius: 10px; border: none; color: "
          "white; padding: 20px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; "
          "margin: 4px 2px; } .submit { background-color: #ff0000; } .save { background-color: #3cb371 } .actions { "
          "display: flex; align-items: end; justify-content: end; } table { border-collapse: collapse; border-spacing: "
          "0; width: 100%; border: 1px solid #ddd; } th, td { text-align: left; padding: 16px; } tr:nth-child(even) { "
          "background-color: #f2f2f2; } </style></head><body onLoad=\"openTab(event, 'data-container')\"><script> function openTab(evt, tabName) { var i, "
          "tabcontent, tablinks; tabcontent = document.getElementsByClassName('tabcontent'); for (i = 0; i < "
          "tabcontent.length; i++) { tabcontent[i].style.display = 'none'; } tablinks = "
          "document.getElementsByClassName('tablinks'); for (i = 0; i < tablinks.length; i++) { tablinks[i].className "
          "= tablinks[i].className.replace(' active', ''); } document.getElementById(tabName).style.display = 'block'; "
          "evt.currentTarget.className += ' active'; } </script><div class='navigation'><button "
          "class='navigation-item active' onclick=\"openTab(event, 'data-container')\">Data</button><button "
          "class='navigation-item' onclick=\"openTab(event, 'action-container')\">Configuration</button></div><div "
          "class='tabcontent' id='action-container'><div class='action-form-container'><div "
          "class='form-container'><form action='save' method='POST'><div class='form'><div class='full-input'><label "
          "for='token'>Entso-E Token</label><input id='token' name='token' maxlength=36 type='password' value='";
    s += token;
    s += "' /></div><div "
         "class='full-input'><label for='tax'>VAT</label><input id='tax' name='tax' type='number' value='";
    s += tax;
    s += "' /></div><div "
         "class='full-input'><label for='margin'>Margin</label><input id='margin' name='margin' type='number' "
         "step='.01' value='";
    s += margin;
    s += "' /></div>";
    s += "<div class='full-input'><label for='unit'>Output unit</label><select name='unit' id='unit' "
         "name='unit'>";
    s += "<option value='0'";
    if (selectedPrice == "0") {
        s += "selected";
    }
    s += ">KWh/Cents</option><option value='1'";
    if (selectedPrice == "1") {
        s += "selected";
    }
    s += ">MWh/Eur</option></select></div>";
    s += "<br><div class='save-container'><input type='submit' class='save action-button' "
         "value='Save Settings' /></div></div></form></div><div class='form-container'><div class='actions'>";
    char link[140];
    sprintf(link, "<form action='%s'> <input type='submit' class='submit action-button' value='Reset Wifi' /></form>",
        reset_token.c_str());
    s += link;
    s += "</div></div></div></div><div class='tabcontent' id='data-container'><div "
         "class='data'><table><tr><th>Index</th><th>Value</th></tr>";
    char state[50];
    sprintf(state, "<tr><td>%d</td><td>%d</td></tr>", 0, status);
    s += state;
    for (int i = 0; i < *priceLen; i++) {
        char line[50];
        sprintf(line, "<tr><td>%d</td><td>%f</td></tr>", i + 1, getPrice((float)priceData[i]));
        s += line;
    }
    s += "</table></div></div></body></html>";
    server.send(200, "text/html", s);
}
void handleReset()
{
    reset = true;
    server.send(200, "text/plain", "Settings reset");
}

void handleGet() { server.send(200, "application/json", "Settings reset"); }
void handleSave()
{
    // Check if body received
    if (server.hasArg("plain") == false) {
        server.send(200, "text/plain", "Body not received");
    } else {
        StringTokenizer tokens(server.arg("plain"), "&");
        int offset = 0;
        EEPROM.begin(512);

        while (tokens.hasNext()) {
            String val = tokens.nextToken();
            String key = val.substring(0, val.indexOf("="));
            if (key == "token") {
                token = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(token, TOKEN_LENGTH, offset);
            } else if (key == "tax") {
                tax = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(tax, TAX_LENGTH, offset);
            } else if (key == "margin") {
                margin = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(margin, MARGIN_LENGTH, offset);
            } else if (key == "unit") {
                selectedPrice = val.substring(val.indexOf("=") + 1, val.length());
                offset += saveToEeprom(selectedPrice, UNIT_LENGTH, offset);
            } else {
                Serial.println("Unknown " + key);
            }
        }
        EEPROM.commit();
        EEPROM.end();
        eeprom_read();
        handleNotFound();
    }
    return;
}
void handleNotFound()
{
    server.sendHeader("Location", "/");
    server.send(307, "text/plain", "");
}

void resetSettings()
{
    wm.resetSettings();
    handleNotFound();
    ESP.restart();
}
void generate_reset_token()
{
    reset_token = ESP8266TrueRandom.random(10000, 20000);
    Serial.println("Generated reset token:");
    Serial.println(reset_token);
}

void resetRegistry()
{
    Serial.println("Resetting registry");
    for (int i = 0; i < *priceLen; i++) {
        rtuSlave.removeHreg(i + 1, 1);
        tcpSlave.removeHreg(i + 1, 1);
    }
}

float addMargin(float price)
{
    if (margin) {
        return price + (margin.toFloat() * 10);
    } else {
        return price;
    }
}
float addVat(float price)
{
    if (tax) {
        float tmp = price;
        float vat = tax.toFloat() / 100;
        if (vat > 0 && tmp > 0) {
            tmp += (tmp * vat);
        }
        return tmp;
    } else {
        return price;
    }
}

float convertPrice(float price)
{
    if (selectedPrice && selectedPrice == "0") {
        return price / 10;
    } else {
        return price;
    }
}

float getPrice(float price)
{
    float tmp = addVat(price);
    tmp = addMargin(tmp);
    return convertPrice(tmp);
}
void updateRegistry()
{
    Serial.printf("Prices in memory:\u0020[");
    for (int i = 0; i < *priceLen; i++) {
        int offset = (i * 2) + 1;
        float price = getPrice((float)priceData[i]);
        // https://github.com/emelianov/modbus-esp8266/issues/158
        f_2uint reg = f_2uint_int(price); // split the float into 2 unsigned integers
        if (i == (*priceLen - 1)) {
            Serial.printf("%f]\n", price);
        } else {
            Serial.printf("%f,\u0020", price);
        }
        // Code for reversefloat modbus
        // TODO: Print out the values in hex so we can match them in modbus end
        for (int j = 0; j < 2; j++) {
            tcpSlave.addHreg(offset + j, reg.i[j], 1);
            rtuSlave.addHreg(offset + j, reg.i[j], 1);
        }

        // tcpSlave.addHreg(offset, reg.i[0], 1);
        //  tcpSlave.addHreg(offset + 1, reg.i[1], 1);
        //  rtuSlave.addHreg(offset, reg.i[0], 1);
        //  rtuSlave.addHreg(offset + 1, reg.i[1], 1);
    }
}

f_2uint f_2uint_int(double float_number)
{ // split the float and return first unsigned integer
    union f_2uint f_number;
    f_number.f = float_number;

    return f_number;
}

void setup()
{
    generate_reset_token();
    WiFi.mode(WIFI_STA);
    Serial.begin(115200);
    timeClient.begin(); // Start NTP
    delay(3000);
    if (wm_nonblocking) {
        wm.setConfigPortalBlocking(false);
    }
    bool res = wm.autoConnect(mDnsName.c_str(), "password"); // password protected ap
    if (!res) {
        Serial.println("Failed to connect or hit timeout");
        // ESP.restart();
    } else {
        // if you get here you have connected to the WiFi
        Serial.println("Connected to WiFi");
        eeprom_read();
        server.onNotFound(handleNotFound);
        server.on("/", handleRoot);
        server.on("/" + reset_token, handleReset);
        server.on("/save", handleSave);
        server.begin(80);
        if (!MDNS.begin(mDnsName)) {
            Serial.println("Error setting up MDNS responder!");
        }
    }

    //  pinMode(TX_ENABLE, OUTPUT);
    pinMode(RX_ENABLE, OUTPUT);
    // digitalWrite(TX_ENABLE, LOW);
    digitalWrite(RX_ENABLE, LOW);
    MAX_485.begin(BAUD_RATE, SWSERIAL_8N1);
    rtuSlave.begin(&MAX_485); // Modbus RTU start
    rtuSlave.slave(SLAVE_ID);
    tcpSlave.begin(); // Modbus TCP start
}

void loop()
{
    MDNS.update();
    wm.process();
    server.handleClient();
    timeClient.update(); // Update NTP and current time
    now = timeClient.getEpochTime();
    // If we should update the data
    if (WiFi.status() == WL_CONNECTED && nextUpdate <= now) {
        nextUpdate = (now - (now % 60)) + 60; // Polls every minute, values will be updated hourly
        refresh();
    } else if (WiFi.status() != WL_CONNECTED) {
        Serial.println("No wifi connection");
    }
    rtuSlave.task();
    tcpSlave.task();
    yield();
    if (reset) {
        resetSettings();
    }
}