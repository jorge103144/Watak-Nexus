#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// Pines (PIR, Buzzer, BtnOn, BtnOff, LedR, LedV, Servo)
#define PIR 27
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

bool armado = false, alarma = false, mov = false;
int angulo = 90;
unsigned long tAlarma = 0, tEnvio = 0;

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
             ",\"movimiento\":" + String(mov ? "true" : "false") +
             ",\"cerradura\":\"" + String(angulo == 0 ? "CERRADA" : "ABIERTA") + "\"}";
  mqtt.publish(T_EST, j.c_str());
}

// Función única reutilizable para Armar, Desarmar y Cerradura
void setEstado(bool act, int ang, String l1, String l2) {
  armado = act;
  alarma = false;
  angulo = ang;
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

  if (cmd == "armar") setEstado(true, 0, "SISTEMA ARMADO", "Vigilando...");
  else if (cmd == "desarmar") setEstado(false, 90, "SISTEMA APAGADO", "Puerta Abierta");
  else if (cmd == "cerrar_puerta") setEstado(armado, 0, "PUERTA CERRADA", armado ? "Armado" : "Apagado");
  else if (cmd == "abrir_puerta") setEstado(armado, 90, "PUERTA ABIERTA", armado ? "Armado" : "Apagado");
  else if (cmd == "estado") pubEstado();
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    String id = "WN_" + String(random(0xffff), HEX);
    if (mqtt.connect(id.c_str())) {
      mqtt.subscribe(T_CMD);
      pubEstado();
    } else delay(1000);
  }
}

void setup() {
  pinMode(PIR, INPUT);
  pinMode(BUZ, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_V, OUTPUT);
  pinMode(BTN_ON, INPUT_PULLUP);
  pinMode(BTN_OFF, INPUT_PULLUP);

  servo.attach(SERVO_PIN);
  lcd.init();
  lcd.backlight();
  showLCD("WATAK - NEXUS", "Conectando...");

  WiFi.begin(SSID, PASS, 6);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(callback);
  setEstado(false, 90, "SISTEMA APAGADO", "WiFi OK");
}

void loop() {
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  if (digitalRead(BTN_ON) == LOW) { delay(200); setEstado(true, 0, "SISTEMA ARMADO", "Vigilando..."); }
  if (digitalRead(BTN_OFF) == LOW) { delay(200); setEstado(false, 90, "SISTEMA APAGADO", "Puerta Abierta"); }

  bool pir = digitalRead(PIR);
  if (pir != mov) {
    mov = pir;
    if (armado && mov && !alarma) {
      alarma = true;
      tAlarma = millis();
      angulo = 0;
      servo.write(0);
      tone(BUZ, 2000);
      digitalWrite(LED_R, HIGH);
      showLCD("!!! ALERTA !!!", "INTRUSO DETECT.");
    }
    pubEstado();
  }

  if (alarma && millis() - tAlarma > 5000) {
    alarma = false;
    noTone(BUZ);
    digitalWrite(LED_R, LOW);
    showLCD("SISTEMA ARMADO", "Vigilando...");
    pubEstado();
  }

  if (millis() - tEnvio > 2000) { tEnvio = millis(); pubEstado(); }
}