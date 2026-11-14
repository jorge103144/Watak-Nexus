#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// Pines exactos al diagrama
#define TRIG 27
#define ECHO 25
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

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servo;
WiFiClient wifi;
PubSubClient mqtt(wifi);

bool armado = false, alarma = false;
int angulo = 90, dist = 0, umbral = 50; // Alerta si distancia < 50 cm
unsigned long tAlarma = 0, tEnvio = 0;

void showLCD(String l1, String l2) {
  lcd.clear();
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);
}

int leerDist() {
  digitalWrite(TRIG, LOW); delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long dur = pulseIn(ECHO, HIGH, 30000);
  return (dur == 0) ? 400 : dur * 0.034 / 2;
}

void pubEstado() {
  if (!mqtt.connected()) return;
  String j = "{\"armado\":" + String(armado ? "true" : "false") +
             ",\"alarma\":" + String(alarma ? "true" : "false") +
             ",\"distancia\":" + String(dist) +
             ",\"umbral\":" + String(umbral) +
             ",\"cerradura\":\"" + String(angulo == 0 ? "CERRADA" : "ABIERTA") + "\"}";
  mqtt.publish(T_EST, j.c_str());
}

void setEstado(bool act, int ang, String l1, String l2) {
  armado = act; alarma = false; angulo = ang;
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

  if (cmd == "armar") setEstado(true, 0, "SISTEMA ARMADO", "Rango < " + String(umbral) + "cm");
  else if (cmd == "desarmar") setEstado(false, 90, "SISTEMA APAGADO", "Puerta Abierta");
  else if (cmd == "cerrar_puerta") setEstado(armado, 0, "PUERTA CERRADA", armado ? "Armado" : "Apagado");
  else if (cmd == "abrir_puerta") setEstado(armado, 90, "PUERTA ABIERTA", armado ? "Armado" : "Apagado");
  else if (cmd.startsWith("umbral:")) {
    umbral = cmd.substring(7).toInt();
    showLCD("NUEVO UMBRAL:", String(umbral) + " cm");
    pubEstado();
  }
  else if (cmd == "estado") pubEstado();
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    String id = "WN_US_" + String(random(0xffff), HEX);
    if (mqtt.connect(id.c_str())) {
      mqtt.subscribe(T_CMD);
      pubEstado();
    } else delay(1000);
  }
}

void setup() {
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(BUZ, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_V, OUTPUT);
  pinMode(BTN_ON, INPUT_PULLUP);
  pinMode(BTN_OFF, INPUT_PULLUP);

  servo.attach(SERVO_PIN);
  lcd.init(); lcd.backlight();
  showLCD("WATAK - NEXUS", "Sensor Ultrason.");

  WiFi.begin(SSID, PASS, 6);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(callback);
  setEstado(false, 90, "SISTEMA APAGADO", "WiFi OK");
}

void loop() {
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  if (digitalRead(BTN_ON) == LOW) { delay(200); setEstado(true, 0, "SISTEMA ARMADO", "Rango < " + String(umbral) + "cm"); }
  if (digitalRead(BTN_OFF) == LOW) { delay(200); setEstado(false, 90, "SISTEMA APAGADO", "Puerta Abierta"); }

  dist = leerDist();
  if (armado && dist < umbral && !alarma) {
    alarma = true;
    tAlarma = millis();
    angulo = 0; servo.write(0);
    tone(BUZ, 2000);
    digitalWrite(LED_R, HIGH);
    showLCD("!!! INTRUSO !!!", "Dist: " + String(dist) + " cm");
    pubEstado();
  }

  if (alarma && millis() - tAlarma > 5000) {
    alarma = false;
    noTone(BUZ);
    digitalWrite(LED_R, LOW);
    showLCD("SISTEMA ARMADO", "Rango < " + String(umbral) + "cm");
    pubEstado();
  }

  if (millis() - tEnvio > 1000) { tEnvio = millis(); pubEstado(); }
}