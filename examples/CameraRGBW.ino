/*

   Лицензионное соглашение:
   Если вы полностью уничтожите компьютер/контроллер/устройство/мотоцикл 
   или устроите восстание машин, то вся вина на вас.

   Вы автоматически принимаете его, открыв данный файл и/или загрузив в контроллер.
*/

#define Version "1.0.0.1"
#define otl 1
#define otlwifi 1
#include <Adafruit_NeoPixel.h> //https://github.com/adafruit/Adafruit_NeoPixel
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include "FS.h"
#include <ESP8266FtpServer.h>
FtpServer ftpSrv;

#define   LED_PIN              13 //пин на светодиоды rgb
#define   LED_WHITE_PIN        14 //пин на светодиоды rgbw 4000K и 3500K
#define   ColoursNum           5 //Количество  потенциометров

#define   PIXEL_IN_STICK              4//Количество светодиодов на полочке 
#define   PIXEL_IN_W_WHITE_STICK      (2 * PIXEL_IN_STICK)//rgbw 3500k //полоски// 2*4=8
#define   PIXEL_IN_C_WHITE_STICK      (2 * PIXEL_IN_STICK)//rgbw 4500k//полоски // 2*4=8
#define   PIXEL_IN_COLOUR_STICK_1     PIXEL_IN_STICK//полоска rgb//4 светодиода
#define   PIXEL_IN_COLOUR_STICK_2     PIXEL_IN_STICK//полоска rgb//4 светодиода
#define   PIXEL_NUM                   (PIXEL_IN_COLOUR_STICK_1 + PIXEL_IN_COLOUR_STICK_2)// две полоски по 8 rgb
#define   PIXEL_WHITE_NUM             (PIXEL_IN_W_WHITE_STICK + PIXEL_IN_C_WHITE_STICK)//две полоски по 8 rgbw 4500k 3500k//

#ifndef APSSID
#define APSSID "WiFi-Settings"              //логин для режима точки доступа
#define APPSK  "settings"                   //пароль для режима точки доступа
#endif

const char *softAP_ssid = APSSID;
const char *softAP_password = APPSK;
const char *myHostname = "rgbw.set";             //домен
char ssid[32] = "";
char password[32] = "";

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

boolean connect;
unsigned long lastConnectTry = 0;
unsigned int status = WL_IDLE_STATUS;

Adafruit_NeoPixel LEDS_WHITE = Adafruit_NeoPixel(PIXEL_WHITE_NUM, LED_WHITE_PIN, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel LEDS = Adafruit_NeoPixel(PIXEL_NUM, LED_PIN, NEO_GRB + NEO_KHZ800);

unsigned int COLOURS[ColoursNum];
bool Active = false;

unsigned int s;
String nomer = "0";
String znach = "0";
String temp_restore;
String temp;

void setup()
{
  LEDS.begin();
  LEDS.show();
  LEDS_WHITE.begin();
  LEDS_WHITE.show();

  delay(1000);
  Serial.begin(115200);
  if (otlwifi) Serial.println();
  if (otlwifi) Serial.println("[WiFi] Настройка точки доступа...");
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(softAP_ssid, softAP_password);
  delay(500);
  if (otlwifi) Serial.print("[WiFi] IP адрес точки доступа: ");
  if (otlwifi) Serial.println(WiFi.softAPIP());
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);     //перенаправление со всех http запросов на нас (в режиме точки доступа)

  SPIFFS.begin();
  if (otlwifi) Serial.println("[SPIFFS] Файловая система установлена.");
  if (otlwifi) Serial.println("[SPIFFS] Файлы в памяти:");
  Dir dir = SPIFFS.openDir("/");
  if (otlwifi) {
    while (dir.next()) {
      if (otlwifi) Serial.print("[SPIFFS] Файл: ");
      if (otlwifi) Serial.print(dir.fileName());
      File f = dir.openFile("r");
      if (otlwifi) Serial.print(" | размер: ");
      if (otlwifi) Serial.print(f.size());
      if (otlwifi) Serial.println(" байт.");
      unsigned long all = f.size();
    }
  }
  read_saving_data();
  webServer.on("/", index_html);
  webServer.on("/index.htm", index_html);
  webServer.on("/index.html", index_html);
  webServer.on("/img.png", img_png);
  webServer.on("/wifi", handleWifi);
  webServer.on("/wifisave", handleWifiSave);
  webServer.on("/generate_204", index_html);
  webServer.on("/fwlink", index_html);
  webServer.onNotFound(handleNotFound);
  webServer.on("/1", []() {
    String SDI = "";
    SDI += ",11:"; SDI += COLOURS[0];
    SDI += ",12:"; SDI += COLOURS[1];
    SDI += ",13:"; SDI += COLOURS[2];
    SDI += ",14:"; SDI += COLOURS[3];
    SDI += ",15:"; SDI += COLOURS[4];
    webServer.send(200, "text/plain", SDI); //Serial.println(SDI);
  });
  webServer.on("/setRGB", []() {
    COLOURS[0] = webServer.arg("re").toInt();
    COLOURS[1] = webServer.arg("gr").toInt();
    COLOURS[2] = webServer.arg("bl").toInt();
    if (otlwifi) Serial.println(String(COLOURS[0]) + " " + String(COLOURS[1]) + " " + String(COLOURS[2]));
    delay(50);
    webServer.send(200, "text/plain", "");
    while (!save()) {
      delay(50);
    }
    save();
  });

  webServer.on("/setRange", []() {
    nomer = webServer.arg("type");
    znach = webServer.arg("value");
    if (nomer == "01") {
      if (Active) {
        Active = false;
        for (byte i = 0; i < PIXEL_NUM; i++)
          LEDS.setPixelColor(i, 0);
        for (byte i = 0; i < PIXEL_IN_W_WHITE_STICK; i++)
          LEDS_WHITE.setPixelColor(i, 0);
        for (byte i = PIXEL_IN_W_WHITE_STICK; i < (PIXEL_IN_W_WHITE_STICK + PIXEL_IN_C_WHITE_STICK); i++)
          LEDS_WHITE.setPixelColor(i, 0);

        LEDS_WHITE.show();
        LEDS.show();
      }
      else Active = true;
    }
    else if (nomer == "11") COLOURS[0] = znach.toInt();
    else if (nomer == "12") COLOURS[1] = znach.toInt();
    else if (nomer == "13") COLOURS[2] = znach.toInt();
    else if (nomer == "14") COLOURS[3] = znach.toInt();
    else if (nomer == "15") COLOURS[4] = znach.toInt();
    else {
      if (otlwifi) Serial.println("Ошибка пришедших данных");
    }
    save();
    delay(50);
    webServer.send(200, "text/plain", znach);
  });
  webServer.begin();
  if (otlwifi) Serial.println("[HTTP] сервер запущен.");
  ftpSrv.begin("login", "password");
  if (otlwifi) Serial.println("[FTP] сервер запущен.");
  if (otlwifi) Serial.print("[INFO] Версия прошивки: ");
  if (otlwifi) Serial.print(Version);
  if (otlwifi) Serial.println(".");
  while (!save()) {
    delay(50);
  }
  loadCredentials();
  connect = strlen(ssid) > 0;
}
//----------------------------------------
void loop()
{
  if (connect) {
    if (otlwifi) Serial.println("[WiFi] Повторное подключение...");
    connect = false;
    connectWifi();
    lastConnectTry = millis();
  }
  {
    s = WiFi.status();
    if (s == 0 && millis() > (lastConnectTry + 60000)) {
      connect = true;
    }
    if (status != s) {
      if (otlwifi) Serial.print("[WiFi] Статус: ");
      if (otlwifi) Serial.println(s);
      status = s;
      if (s == WL_CONNECTED) {
        if (otlwifi) Serial.println("[WiFi] Подключён к: ");
        if (otlwifi) Serial.println(ssid);
        if (otlwifi) Serial.print("[WiFi] IP адресс: ");
        if (otlwifi) Serial.println(WiFi.localIP());

        if (!MDNS.begin(myHostname)) {
          if (otlwifi) Serial.println("[mDNS] [ERROR] Ошибка настройки!");
        } else {
          if (otlwifi) Serial.println("[mDNS] Система доменных имён запущена.");
          MDNS.addService("http", "tcp", 80);
        }
      } else if (s == WL_NO_SSID_AVAIL) {
        WiFi.disconnect();
      }
    }
    if (s == WL_CONNECTED) {
      MDNS.update();
    }
  }
  ftpSrv.handleFTP();
  dnsServer.processNextRequest();
  webServer.handleClient();
  if (Active) {
    for (byte i = 0; i < PIXEL_NUM; i++)
      LEDS.setPixelColor(i, COLOURS[0], COLOURS[1], COLOURS[2]);
    for (byte i = 0; i < PIXEL_IN_W_WHITE_STICK; i++)
      LEDS_WHITE.setPixelColor(i, COLOURS[0], COLOURS[1], COLOURS[2], COLOURS[3]);
    for (byte i = PIXEL_IN_W_WHITE_STICK; i < (PIXEL_IN_W_WHITE_STICK + PIXEL_IN_C_WHITE_STICK); i++)
      LEDS_WHITE.setPixelColor(i, COLOURS[0], COLOURS[1], COLOURS[2], COLOURS[4]);

    LEDS_WHITE.show();
    LEDS.show();
  }
}

void connectWifi() {
  if (otlwifi) Serial.println("[WiFi] Подключение в качестве клиента...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  int connRes = WiFi.waitForConnectResult();
  if (otlwifi) Serial.print("[WiFi] результат: ");
  if (otlwifi) Serial.println(connRes);
}

void loadCredentials() {
  EEPROM.begin(512);
  EEPROM.get(0, ssid);
  EEPROM.get(0 + sizeof(ssid), password);
  char ok[2 + 1];
  EEPROM.get(0 + sizeof(ssid) + sizeof(password), ok);
  EEPROM.end();
  if (String(ok) != String("OK")) {
    ssid[0] = 0;
    password[0] = 0;
  }
  if (otlwifi) Serial.println("[EEPROM] Восстановленные данные роутера пользователя: ");
  if (otlwifi) Serial.print("[EEPROM] ");
  if (otlwifi) Serial.println(strlen(ssid) > 0 ? "SSID: " + String(ssid) : "<no ssid>");
  if (otlwifi) Serial.print("[EEPROM] ");
  if (otlwifi) Serial.println(strlen(password) > 0 ? "PASS: ********" : "<no password>");
}

void saveCredentials() {
  EEPROM.begin(512);
  EEPROM.put(0, ssid);
  EEPROM.put(0 + sizeof(ssid), password);
  char ok[2 + 1] = "OK";
  EEPROM.put(0 + sizeof(ssid) + sizeof(password), ok);
  EEPROM.commit();
  EEPROM.end();
}

boolean isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

void handleWifi() {
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");

  String Page;
  Page += F(
            "<html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><meta http-equiv='X-UA-Compatible' content='ie=edge'><meta charset='utf - 8'>"
            "<title>Настройка Wi-Fi</title>"
            "<style>* {padding: 0;margin: 0;font-family: Open Sans;}.form {display: flex;flex-direction: column;justify-content: space-around;align-items: center;padding-top: 4vh;padding-bottom: 2vh;text-align: center;text-align-last: center;}body {background-color: #222222;}.title {padding-bottom: 1vh;display: flex;flex-direction: column;justify-content: center;align-items: center;color: #fff;font-size: 20pt;}.controll {padding-bottom: 4%;font-size: 15pt;text-align: center;justify-content: center;align-items: center;position: relative}p {margin: 0;color: white;font-size: 17px;padding-top: 1vh;padding-bottom: 0vh;}a {color: #ffffff;}.buttons {display: flex;flex-direction: column;justify-content: space-around;align-items: center;padding-top: 4vh;padding-bottom: 2vh;text-align: center;text-align-last: center;}.button {border: 1px solid grey;background-color: #222222;color: white;font-size: 17px;height: 40px;margin-bottom: 1vh;text-align: center;padding-left: 1vw;padding-right: 1vw;width: 80vw;max-width: 300px;}.fildd {padding-top: 1vh;padding-bottom: 2vh;color: white;display: flex;flex-direction: column;align-items: center;text-align: center;text-align-last: center;padding-left: 1vw;padding-right: 1vw;width: 80vw;max-width: 400px;}.form .fildd .fild {width: 80vw;max-width: 300px;display: flex;position: relative;border: 1px solid grey;height: 40px;align-items: center;justify-content: center;margin-bottom: 2vh;font-size: 18px;}.fildd .fild input:focus {outline: none;}.form .fild label {position: absolute;top: -10px;left: 10px;color: white;background-color: #222222;padding: 0 5px;}.form .fild input {height: 100%;padding-left: 1vw;padding-right: 1vw;width: 80vw;max-width: 400px;font-size: 18px;background-color: #222222;color: white;border: none;}.form .fild input:focus {outline: none;}.copy {color: white;bottom: 20px;}.form-1,.form-2,.form-3 {display: none;}.form .fild select {width: 100%;font-size: 15px;background-color: #222222;color: white;border: none;}.form .fild select:focus {outline: none;}.main {max-width: 450px;min-width: 250px;width: 100%;margin: auto;}</style>"
            "</head><body><div class='main'><div class='title'><h3>RGBW Control</h3></div><div class='buttons'>"
          );

  if (s == WL_CONNECTED) {
    Page += String(F("<p>Режим: клиент.</p><p>SSID: ")) + String(ssid) + String(F("</p><p>IP: "));
    Page += toStringIp(WiFi.localIP());
    Page += F("</p>");
  } else {
    Page += String(F("<p>Режим: точка доступа.</p><p>SSID: ")) + String(softAP_ssid) + String(F("</p><p>IP: "));
    Page += toStringIp(WiFi.softAPIP());
    Page += F("</p>");
  }

  Page += F(
            "<button class='button' onClick='switcher(1, true)'>Подключиться к сети</button><form action='/'><button class='button' type='submit'>Назад</button></form><div class='copy'>Разработчик ПО: <a href=https://vk.com/theaxial>TheAxial</a></div><div class='copy'>Основатель идеи: <a href=https://www.instagram.com/ignatikovam />ignatikovam</a></div></div><div class='form-1 form'><form method='POST' class='fildd' action='wifisave'><div class='title'>Подключение к сети</div>"
            "<div class='fild'><label>Логин</label><select name='n'><option>Выберите Wi-Fi сеть</option>"
          );
  if (otlwifi) Serial.println("[WiFi] Сканирование запущено.");
  int n = WiFi.scanNetworks();
  if (otlwifi) Serial.println("[WiFi] Сканирование завершено.");
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      Page += String(F("<option>")) + WiFi.SSID(i) + F("</option>");
    }
  } else {
    Page += F("<option>Wi-Fi сети не найдены</option>");
  }

  Page += F(
            "</select></div><div class='fild'><label for=''>Пароль</label><input type='password' name='p'></div><input class='button' type='submit' value='Подлючиться/Отлючиться' /><input readonly class='button' onClick='switcher(1, false)' value='Назад' /></form>"
            "<div class='copy'>Разработчик ПО: <a href=https://vk.com/theaxial>TheAxial</a></div><div class='copy'>Основатель идеи: <a href=https://www.instagram.com/ignatikovam />ignatikovam</a></div></div></div><script>function switcher(num, act) {if (act) {document.querySelector('.form-' + num).style.display = 'flex';document.querySelector('.buttons').style.display = 'none';} else {document.querySelector('.form-' + num).style.display = 'none';document.querySelector('.buttons').style.display = 'flex';}}</script></body></html>"
          );
  webServer.send(200, "text/html", Page);
  webServer.client().stop();
}

void handleWifiSave() {
  webServer.arg("n").toCharArray(ssid, sizeof(ssid) - 1);
  webServer.arg("p").toCharArray(password, sizeof(password) - 1);
  webServer.sendHeader("Location", "wifi", true);
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.send(302, "text/plain", "");
  webServer.client().stop();
  saveCredentials();
  connect = strlen(ssid) > 0;
  if (otlwifi) Serial.println("[WiFi] сохранён");
}

void handleNotFound() {
  if (captivePortal()) {
    return;
  }
  String message = F("File Not Found\n\n");
  message += F("URI: ");
  message += webServer.uri();
  message += F("\nMethod: ");
  message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += webServer.args();
  message += F("\n");

  for (uint8_t i = 0; i < webServer.args(); i++) {
    message += String(F(" ")) + webServer.argName(i) + F(": ") + webServer.arg(i) + F("\n");
  }
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.send(404, "text/plain", message);
}

boolean captivePortal() {
  if (!isIp(webServer.hostHeader()) && webServer.hostHeader() != (String(myHostname) + ".local")) {
    if (otlwifi) Serial.println("[WiFi] Запрос перенаправляется на портал");
    webServer.sendHeader("Location", String("http://") + toStringIp(webServer.client().localIP()), true);
    webServer.send(302, "text/plain", "");
    webServer.client().stop();
    return true;
  }
  return false;
}

void index_html() {
  if (otl)Serial.print("[SPIFFS] Запрос к index.html.");
  File temporary_file = SPIFFS.open("/index.html", "r");
  if (temporary_file) {
    webServer.streamFile(temporary_file, "text/html");
    if (otl)Serial.println(" (Успешно.)");
  }
  else {
    if (otl)Serial.println(" (Невозможно открыть или создать файл.)");
  }
  temporary_file.close();
}

void img_png() {
  if (otl)Serial.print("[SPIFFS] Запрос к img.png.");
  File temporary_file = SPIFFS.open("/img.png", "r");
  if (temporary_file) {
    webServer.streamFile(temporary_file, "image/png");
    if (otl)Serial.println(" (Успешно.)");
  }
  else {
    if (otl)Serial.println(" (Невозможно открыть или создать файл.)");
  }
  temporary_file.close();
}

bool save() {
  if (otl)Serial.print("[SPIFFS] Вызвана функция сохранения настроек.");
  String saving = String(COLOURS[0]);                         //составляем строку
  saving += " "; saving += String(COLOURS[1]);              //составляем строку
  saving += " "; saving += String(COLOURS[2]);             //составляем строку
  saving += " "; saving += String(COLOURS[3]);             //составляем строку
  saving += " "; saving += String(COLOURS[4]);                 //составляем строку
  File temporary_file = SPIFFS.open("/config.txt", "w");//открываем файл
  if (temporary_file) {                                 //проверка
    if (temporary_file.print(saving)) {                 //записываем в файл
      if (otl)Serial.println(" (Файл очищен успешно.)");//сообщаем статус
    }
  }
  else {
    if (otl)Serial.println(" (Невозможно открыть или создать файл.)");
    return false;
  }
  temporary_file.close();
  return true;
}

void read_saving_data() {
  if (otl)Serial.print("[SPIFFS] Вызвана функция восстановления настроек.");
  File temporary_file = SPIFFS.open("/config.txt", "r");
  if (temporary_file) {
    if (otl)Serial.println(" (Файл открыт для чтения успешно.)");
    while (temporary_file.available()) {
      temp_restore += temporary_file.readString();
      delay(2);
      //Serial.write(temporary_file.read());
    }
    COLOURS[0] = getValue(temp_restore, ' ', 0).toInt();
    COLOURS[1] = getValue(temp_restore, ' ', 1).toInt();
    COLOURS[2] = getValue(temp_restore, ' ', 2).toInt();
    COLOURS[3] = getValue(temp_restore, ' ', 3).toInt();
    COLOURS[4] = getValue(temp_restore, ' ', 4).toInt();
  }
  else {
    if (otl)Serial.println(" (Невозможно открыть или создать файл.)");
  }
  temporary_file.close();
  temp_restore = "";
}

String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
