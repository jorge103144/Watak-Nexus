#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <Keypad.h>

// Pines exactos al diagrama
#define BUZ 26
#define BTN_ON 14
#define BTN_OFF 12
#define LED_R 2
#define LED_V 4
#define SERVO_PIN 13

const char* SSID = "Wokwi-GUEST";
const char* PASS = "";
const char* BROKER = "broker.emqx.io";
const char* T_CMD = "watak_nexus_erick2026/comando";
const char* T_EST = "watak_nexus_erick2026/estado";

byte rowPins[4] = {32, 33, 25, 15};
byte colPins[4] = {16, 17, 18, 19};
char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servo;
WiFiClient wifi;
PubSubClient mqtt(wifi);

bool armado = false, alarma = false, acceso = false;
int angulo = 90, intentosFallidos = 0;
String pinIngresado = "", pinCorrecto = "A4B9";
unsigned long tAlarma = 0, tAcceso = 0, tEnvio = 0;

void showLCD(String l1, String l2) {
  lcd.clear();
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);
}

void pubEstado() {
  if (!mqtt.connected()) return;
  String j = "{\"armado\":" + String(armado ? "true" : "false") +
             ",\"alarma\":" + String(alarma ? "true" : "false") +
             ",\"acceso\":" + String(acceso ? "true" : "false") +
             ",\"fallos\":" + String(intentosFallidos) +
             ",\"cerradura\":\"" + String(angulo == 0 ? "CERRADA" : "ABIERTA") + "\"}";
  mqtt.publish(T_EST, j.c_str());
}

void setEstado(bool act, int ang, String l1, String l2) {
  armado = act; alarma = false; acceso = false;
  pinIngresado = ""; angulo = ang;
  servo.write(angulo);
  digitalWrite(LED_V, armado);
  digitalWrite(LED_R, !armado);
  noTone(BUZ);
  showLCD(l1, l2);
  pubEstado();
}

void callback(char* topic, byte* payload, unsigned int len) {
  String cmd = "";
  for (int i = 0; i < len; i++) cmd += (char)payload[i];
  cmd.trim();

  if (cmd == "armar") setEstado(true, 0, "SISTEMA ARMADO", "Ingrese PIN:");
  else if (cmd == "desarmar") setEstado(false, 0, "SISTEMA APAGADO", "Cerradura Bloq.");
  else if (cmd == "abrir_puerta") {
    acceso = true; tAcceso = millis(); angulo = 90; servo.write(90);
    showLCD("ACCESO REMOTO", "Bienvenido (10s)");
    pubEstado();
  }
  else if (cmd == "cerrar_puerta") setEstado(armado, 0, armado ? "SISTEMA ARMADO" : "SISTEMA APAGADO", armado ? "Ingrese PIN:" : "Cerradura Bloq.");
  else if (cmd.startsWith("pin:") && cmd.length() == 8) {
    pinCorrecto = cmd.substring(4);
    showLCD("NUEVO PIN OK:", "****");
    delay(1000);
    showLCD(armado ? "SISTEMA ARMADO" : "SISTEMA APAGADO", armado ? "Ingrese PIN:" : "Listo");
  }
  else if (cmd == "estado") pubEstado();
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    String id = "WN_KP_" + String(random(0xffff), HEX);
    if (mqtt.connect(id.c_str())) {
      mqtt.subscribe(T_CMD);
      pubEstado();
    } else delay(1000);
  }
}

void setup() {
  pinMode(BUZ, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_V, OUTPUT);
  pinMode(BTN_ON, INPUT_PULLUP);
  pinMode(BTN_OFF, INPUT_PULLUP);

  servo.attach(SERVO_PIN);
  lcd.init(); lcd.backlight();
  showLCD("WATAK - NEXUS", "Control Keypad");

  WiFi.begin(SSID, PASS, 6);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(callback);
  setEstado(false, 90, "SISTEMA APAGADO", "WiFi OK");
}

void loop() {
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  if (digitalRead(BTN_ON) == LOW) { delay(200); setEstado(true, 0, "SISTEMA ARMADO", "Ingrese PIN:"); }
  if (digitalRead(BTN_OFF) == LOW) { delay(200); setEstado(false, 0, "SISTEMA APAGADO", "Cerradura Bloq."); }

  // Lectura del Teclado Matricial cuando está armado
  if (armado && !alarma && !acceso) {
    char tecla = keypad.getKey();
    if (tecla) {
      if (tecla == '*') { pinIngresado = ""; showLCD("SISTEMA ARMADO", "Ingrese PIN:"); return; }
      pinIngresado += tecla;
      lcd.setCursor(0, 1);
      lcd.print("PIN: ");
      for (int i = 0; i < pinIngresado.length(); i++) lcd.print("*");

      if (pinIngresado.length() >= 4) {
        delay(250);
        if (pinIngresado == pinCorrecto) {
          acceso = true; tAcceso = millis();
          angulo = 90; servo.write(90);
          showLCD("Codigo Correcto", "Bienvenido");
        } else {
          alarma = true; tAlarma = millis();
          intentosFallidos++;
          angulo = 0; servo.write(0);
          tone(BUZ, 2000);
          digitalWrite(LED_R, HIGH);
          showLCD("INTRUSO DETECT.", "PIN INCORRECTO");
        }
        pinIngresado = "";
        pubEstado();
      }
    }
  }

  // Cierre automático tras 10s de acceso permitido
  if (acceso && millis() - tAcceso >= 10000) {
    acceso = false; angulo = 0; servo.write(0);
    showLCD("SISTEMA ARMADO", "Ingrese PIN:");
    pubEstado();
  }

  // Silencio de alarma tras 5s (mantiene bloqueo)
  if (alarma && millis() - tAlarma > 5000) {
    alarma = false; noTone(BUZ);
    digitalWrite(LED_R, LOW);
    showLCD("SISTEMA ARMADO", "Ingrese PIN:");
    pubEstado();
  }

  if (millis() - tEnvio > 2000) { tEnvio = millis(); pubEstado(); }
}