/**
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║   SISTEMA ALARMA ESP01 3V3  ·  Activación por Corte de Alimentación ║
 * ║   Basado en: SEGURITY HOME · Autor: LUGAR O CASA DE PERSONA                  ║
 * ║   Plataforma: ESP8266 (ESP-01)                                       ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  CONCEPTO PRINCIPAL: ACTIVACIÓN POR CORTE/RESTAURACIÓN DE 3.3V
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  En modo DESPLEGADO, cada vez que se restaura la alimentación 3.3V
 *  (es decir, cada arranque del ESP-01), el sistema inicia una cuenta
 *  regresiva de 6 segundos y luego dispara la secuencia completa de
 *  alarma: envía alertas al grupo y al administrador por Telegram,
 *  repite el ciclo periódicamente hasta que se corte la alimentación.
 *
 *  FLUJO DE ACTIVACIÓN:
 *
 *    [Corte 3.3V] → [Restaura 3.3V] → [Arranque ESP-01]
 *         → [Conecta WiFi] → [Cuenta 6s] → [ALARMA] → [Repite]
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  MODOS DE OPERACIÓN
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  NO DESPLEGADO (Configuración):
 *  ──────────────────────────────
 *   • Modo por defecto en primer arranque o tras pérdida de WiFi grave.
 *   • Crea Access Point + portal web completo de configuración.
 *   • El botón GPIO2 funciona como en el original (hold 6s → alarma).
 *   • Para armar el sistema: clic en "Desplegar" en el portal web.
 *
 *  DESPLEGADO (Armado):
 *  ────────────────────
 *   • Activado mediante el botón "Desplegar" del portal web.
 *   • En CADA encendido (restauración de 3.3V):
 *       1. Conecta WiFi (hasta 40 intentos antes de ir a AP).
 *       2. Espera 6 segundos (cuenta regresiva visible en Serial).
 *       3. Dispara alarma completa: 4 mensajes al grupo + 1 al admin.
 *       4. Repite alertas cada REPEAT_MS (5 minutos por defecto).
 *       5. El sistema sigue enviando hasta que se corte la alimentación.
 *   • El botón GPIO2 también dispara alarma manual (hold 6s).
 *   • El portal web permite "Desplegar" / "Replegar" el sistema.
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  HARDWARE ESP-01
 * ═══════════════════════════════════════════════════════════════════════
 *
 *   GPIO2  → BOTÓN DE PÁNICO MANUAL  (INPUT_PULLUP → GND al presionar)
 *            · Hold 6s → dispara alarma manualmente (ambos modos)
 *            · En modo Desplegado: también ocurre al arrancar (3.3V)
 *
 *   GPIO0  → LED externo opcional (activo HIGH)
 *            · En modo No Desplegado: encendido cuando hay WiFi
 *            · En modo Desplegado:    parpadeo durante cuenta regresiva
 *
 *   RST    → Solo para programación / reset de emergencia
 *
 *   3.3V   → DISPARADOR PRINCIPAL en modo Desplegado
 *            · Cortar 3.3V → ESP-01 se apaga
 *            · Restaurar 3.3V → ESP-01 arranca → cuenta 6s → alarma
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  CIRCUITO RECOMENDADO PARA CORTE DE 3.3V
 * ═══════════════════════════════════════════════════════════════════════
 *
 *   Opción A — Transistor/MOSFET controlado externamente:
 *
 *     VCC_3V3 ──[P-MOSFET]── VCC_ESP01
 *                   │
 *             [Control externo / sensor / reed switch / PIR]
 *
 *   Opción B — Relé de baja potencia:
 *
 *     VCC_3V3 ──[NC del relé]── VCC_ESP01
 *     Al activar el relé → corta alimentación → ESP-01 se apaga
 *     Al desactivar → restaura → ESP-01 arranca → alarma
 *
 *   Opción C (más simple) — Interruptor manual o reed switch:
 *
 *     VCC_3V3 ──[Interruptor / Reed]── VCC_ESP01
 *     Abrir = apaga. Cerrar = enciende → ALARMA en 6s.
 *
 *   NOTA: El ESP-01 requiere ~80mA peak al arrancar. Usar MOSFET o relé
 *   capaz de manejar esa corriente. Un transistor NPN pequeño NO alcanza.
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  COMPARATIVA DE CONSUMO (modo Desplegado activo):
 * ═══════════════════════════════════════════════════════════════════════
 *   Inactivo (3.3V cortado)  :  0 mA   ← sistema "dormido" sin batería
 *   Arranque + WiFi + alarma :  ~80 mA  (dura solo segundos/minutos)
 *   Reposo entre alertas     :  ~15 mA  (Modem Sleep entre repeticiones)
 *
 * ═══════════════════════════════════════════════════════════════════════
 *  LIBRERÍAS REQUERIDAS (Gestor de Librerías de Arduino)
 * ═══════════════════════════════════════════════════════════════════════
 *   · UniversalTelegramBot  by Brian Lough   v1.3.0+
 *   · ArduinoJson           by Benoit Blanchon v6.x
 *
 *  Librerías nativas ESP8266 (NO instalar):
 *   ESP8266WiFi · ESP8266WebServer · DNSServer · WiFiClientSecure
 *   EEPROM · time.h
 *
 *  Arduino IDE:
 *   Tarjeta   → "Generic ESP8266 Module"
 *   Flash     → 1M (FS: none, OTA: ~502KB)
 *   CPU Speed → 80 MHz (160 MHz si hay problemas TLS)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <time.h>

// ── Pines ESP-01 ──────────────────────────────────────────────────────────
#define PIN_BTN    2      // GPIO2 – Botón de pánico manual (INPUT_PULLUP)
#define PIN_LED    0      // GPIO0 – LED externo opcional (activo HIGH)
#define LED_ON     HIGH
#define LED_OFF    LOW

// ── EEPROM ────────────────────────────────────────────────────────────────
#define EEPROM_SZ  512
#define MAGIC_V    0xCA2D   // Versión 2 — diferente al original para evitar
                            // colisiones si se flashea encima del original

// ── NTP / Zona horaria ────────────────────────────────────────────────────
#define NTP_HOST   "pool.ntp.org"
#define UTC_OFFS   -18000   // UTC-5  Colombia / Ecuador / Perú
#define DST_OFFS   0

// ── Captive Portal ────────────────────────────────────────────────────────
#define DNS_PORT   53

// ── Tiempos ───────────────────────────────────────────────────────────────
#define HOLD_MS          6000UL    // Hold botón manual → alarma
#define AUTO_ALARM_MS    6000UL    // Espera tras power-on (modo Desplegado)
#define TG_POLL_MS       1500UL    // Polling Telegram
#define SES_TOUT         1800000UL // Sesión web: 30 min
#define SLEEP_TIMEOUT_MS 1800000UL // Modem Sleep: 30 min sin actividad
#define REPEAT_MS        60000UL   // Repetir alerta c/ 1 min en modo Desplegado (solo canales)
#define WIFI_MAX_TRIES   40        // Intentos WiFi antes de caer a AP

// ═══════════════════════════════════════════════════════════════════════════
//  ESTRUCTURA DE CONFIGURACIÓN (guardada en EEPROM)
// ═══════════════════════════════════════════════════════════════════════════
struct Config {
  uint16_t magic;
  char wSSID[32];
  char wPass[64];
  char botTok[64];
  char admNom[32];
  char admID[20];
  char grpID[20];
  char apSSID[32];
  char apPass[32];
  char webPin[16];
  bool desplegado;      // ← NUEVO: true = sistema armado
};

Config cfg;

// ═══════════════════════════════════════════════════════════════════════════
//  OBJETOS GLOBALES
// ═══════════════════════════════════════════════════════════════════════════
ESP8266WebServer          webSrv(80);
DNSServer                 dnsSrv;
BearSSL::WiFiClientSecure tls;
UniversalTelegramBot     *bot = nullptr;

bool apMode   = false;
bool ntpReady = false;
bool rptToday = false;
int  rptDay   = -1;
unsigned long tgT   = 0;
unsigned long bootT = 0;

// ── Botón manual GPIO2 ────────────────────────────────────────────────────
bool btnCur   = HIGH, btnPrev = HIGH;
unsigned long btnMs   = 0;
bool alarmSent = false;
int  lastSec   = -1;

// ── Sesión web ───────────────────────────────────────────────────────────
String        sesTok = "";
unsigned long sesMs  = 0;

// ── Modem Sleep ──────────────────────────────────────────────────────────
unsigned long lastActivityMs = 0;
bool          inSleep        = false;

// ── Modo Desplegado: auto-alarma por power-on ─────────────────────────────
// autoCountdown: true = esperando los 6s tras encendido en modo Desplegado
bool          autoCountdown  = false;
unsigned long autoStartMs    = 0;
int           autoLastSec    = -1;

// alarmActive: true = alarma disparada, enviando repeticiones periódicas
bool          alarmActive    = false;
unsigned long lastRepeatMs   = 0;

// Última hora de activación (para mostrar en portal)
char          lastAlarmTime[20] = "—";

// ═══════════════════════════════════════════════════════════════════════════
//  PROTOTIPOS
// ═══════════════════════════════════════════════════════════════════════════
void cfgSave();  void cfgLoad();  void cfgDefault();
void wifiConnect();  bool wifiTry(const char*, const char*, int);
void apStart();  void ledBlink(int);
void btnCheck(); void alarmFire(const char* motivo);
void tgProcess(int);
void rptSend();  void rptCheck();
void webSetup();
bool getTime8266(struct tm*);
String sesGen(); bool sesOk(String); bool httpAuth();
void hRoot();    void hLoginGET(); void hLoginPOST(); void hLogout();
void hStatus();  void hScanWifi();
void hSaveWifi();  void hSaveTok();   void hSaveAdmin();
void hSaveGrupo(); void hSaveAP();    void hSavePin();
void hTest();    void hReset();     void hReboot();
void hForceSleep(); void hDesplegar(); void hReplegar();
void hNotFound();
void enterSleep();
void wakeFromSleep();
void initAutoAlarm();   // Inicia cuenta regresiva de 6s en modo Desplegado
void checkAutoAlarm();  // Maneja la cuenta y disparo automático
void sendAlarmTelegram(const char* motivo); // Envía alertas al canal

// ═══════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LED_OFF);

  randomSeed(analogRead(A0) ^ (uint32_t)micros());

  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n════════════════════════════════════════════════════════"));
  Serial.println(F("   SISTEMA ALARMA ESP01 3V3  ·  LUGAR O CASA DE PERSONA"));
  Serial.println(F("   Activación por corte/restauración de alimentación"));
  Serial.println(F("════════════════════════════════════════════════════════"));

  EEPROM.begin(EEPROM_SZ);
  cfgLoad();
  if (cfg.magic != MAGIC_V) {
    Serial.println(F("Primera ejecución: cargando configuración por defecto..."));
    cfgDefault();
  }

  Serial.printf("Modo: %s\n", cfg.desplegado ? ">>> DESPLEGADO (ARMADO) <<<" : "No Desplegado (Configuración)");

  // ── Conectar WiFi ────────────────────────────────────────────────────
  wifiConnect();

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(PIN_LED, LED_ON);
    apMode = false;

    configTime(UTC_OFFS, DST_OFFS, NTP_HOST);
    uint32_t t0 = millis();
    while (time(nullptr) < 1000000UL && millis() - t0 < 6000) {
      delay(200); yield();
    }
    struct tm ti;
    if (getTime8266(&ti)) {
      ntpReady = true;
      char buf[32]; strftime(buf, 32, "%H:%M:%S %d/%m/%Y", &ti);
      Serial.println("NTP: " + String(buf));
    }

    if (strlen(cfg.botTok) > 10) {
      tls.setInsecure();
      tls.setBufferSizes(1024, 512);
      bot = new UniversalTelegramBot(cfg.botTok, tls);

      // Descartar mensajes anteriores
      int p = bot->getUpdates(bot->last_message_received + 1);
      while (p) { p = bot->getUpdates(bot->last_message_received + 1); }
      Serial.println(F("Bot Telegram listo."));

      // Mensaje de arranque al admin (solo en modo No Desplegado)
      // En modo Desplegado se envía después de notificar al canal (ver initAutoAlarm)
      if (strlen(cfg.admID) > 3 && !cfg.desplegado) {
        String m = "🔒 *Sistema Alarma ESP01 3V3 — Iniciado*\n\n"
                   "📶 WiFi: `" + String(cfg.wSSID) + "`\n"
                   "🌐 IP: `" + WiFi.localIP().toString() + "`\n"
                   "🖥 Portal: http://" + WiFi.localIP().toString() + "\n"
                   "✅ Modo: No Desplegado (Configuración)";
        bot->sendMessage(cfg.admID, m, "Markdown");
      }
    }
    Serial.println("Portal web → http://" + WiFi.localIP().toString());

  } else {
    // WiFi no disponible — volver a modo No Desplegado para no
    // quedarse en un estado indefinido sin conectividad
    Serial.println(F("WiFi no disponible → AP + modo No Desplegado"));
    if (cfg.desplegado) {
      Serial.println(F("AVISO: No se puede armar sin WiFi. Desarmando temporalmente."));
      cfg.desplegado = false;
      cfgSave();
    }
    apStart();
  }

  bootT = millis();
  webSetup();
  webSrv.begin();
  Serial.println(F("Servidor web iniciado."));

  btnPrev = digitalRead(PIN_BTN);
  lastActivityMs = millis();

  // ── Iniciar cuenta regresiva automática si está Desplegado ───────────
  if (cfg.desplegado && WiFi.status() == WL_CONNECTED) {
    initAutoAlarm();
  }

  if (btnPrev == LOW) {
    btnMs = millis(); alarmSent = false; lastSec = -1;
    Serial.println(F("Botón presionado al arrancar — mantener 6s para alarmar."));
  }

  Serial.printf("Reposo WiFi en: %.0f min sin actividad.\n", SLEEP_TIMEOUT_MS / 60000.0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════
void loop() {
  if (apMode) {
    dnsSrv.processNextRequest();

  } else if (!inSleep) {

    if (WiFi.status() != WL_CONNECTED) {
      digitalWrite(PIN_LED, LED_OFF);
      Serial.println(F("WiFi perdido. Reconectando..."));
      wifiConnect();
      if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(PIN_LED, LED_ON);
        if (!ntpReady) configTime(UTC_OFFS, DST_OFFS, NTP_HOST);
      } else {
        // Tras muchos intentos fallidos → ir a AP y desarmar
        Serial.println(F("Red WiFi perdida — Volviendo a modo configuración."));
        if (cfg.desplegado) {
          cfg.desplegado = false; cfgSave();
          autoCountdown = false; alarmActive = false;
        }
        apStart();
      }
    }

    rptCheck();

    // Polling Telegram (solo si no está en cuenta regresiva activa)
    if (bot && millis() - tgT > TG_POLL_MS) {
      yield();
      int n = bot->getUpdates(bot->last_message_received + 1);
      while (n) { tgProcess(n); n = bot->getUpdates(bot->last_message_received + 1); }
      tgT = millis();
    }

    // ── Cuenta regresiva automática (modo Desplegado) ─────────────────
    checkAutoAlarm();

    // ── Repetición periódica de alarma activa ─────────────────────────
    if (alarmActive && millis() - lastRepeatMs > REPEAT_MS) {
      Serial.println(F("Repitiendo alerta al canal..."));
      sendAlarmTelegram("Alerta repetida — sistema aún activo");
      lastRepeatMs = millis();
    }
  }

  webSrv.handleClient();
  btnCheck();
  delay(30);

  // Temporizador Modem Sleep (solo en modo No Desplegado)
  if (!inSleep && !cfg.desplegado && millis() - lastActivityMs > SLEEP_TIMEOUT_MS) {
    Serial.println(F("30 min sin actividad → Modem Sleep."));
    enterSleep();
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  AUTO-ALARMA: CUENTA REGRESIVA Y DISPARO POR POWER-ON
// ═══════════════════════════════════════════════════════════════════════════

/**
 * initAutoAlarm()
 * Inicia la cuenta regresiva de 6 segundos tras el encendido en modo
 * Desplegado. Se llama UNA sola vez desde setup().
 */
void initAutoAlarm() {
  autoCountdown = false;
  alarmActive   = true;
  lastRepeatMs  = millis();
  Serial.println(F(""));
  Serial.println(F("  ╔══════════════════════════════════════╗"));
  Serial.println(F("  ║  MODO DESPLEGADO — ALIMENTACIÓN ON  ║"));
  Serial.println(F("  ║  Iniciando protocolo de alarma...   ║"));
  Serial.println(F("  ╚══════════════════════════════════════╝"));
  Serial.println(F(""));
  alarmFire("Restauración de alimentación 3.3V");
  // Tras enviar al canal, notificar al admin que el sistema arrancó
  if (bot && strlen(cfg.admID) > 3) {
    String m = "🔴 *¡SISTEMA DESPLEGADO ARRANCÓ!*\n\n"
               "⚠️ Se detectó restauración de alimentación.\n"
               "✅ Alertas enviadas al canal.\n\n"
               "📶 WiFi: `" + String(cfg.wSSID) + "`\n"
               "🌐 IP: `" + WiFi.localIP().toString() + "`";
    bot->sendMessage(cfg.admID, m, "Markdown");
    Serial.println(F("Notificación de arranque enviada al admin."));
  }
}

/**
 * checkAutoAlarm()
 * Llamado desde el loop. Maneja la cuenta y dispara cuando llegue a 0.
 */
void checkAutoAlarm() {
  if (!autoCountdown) return;

  unsigned long elapsed = millis() - autoStartMs;
  int sec = elapsed / 1000;

  // Mostrar contador por Serial
  if (sec != autoLastSec && sec >= 0 && sec <= 6) {
    autoLastSec = sec;
    int remaining = 6 - sec;
    if (remaining > 0) {
      Serial.printf("  [AUTO-ALARMA] %d segundos para disparo...\n", remaining);
      ledBlink(1); // Un parpadeo por segundo
    }
  }

  if (elapsed >= AUTO_ALARM_MS) {
    autoCountdown = false;
    alarmActive   = true;
    lastRepeatMs  = millis();
    alarmFire("Restauración de alimentación 3.3V");
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  HELPER: tiempo local
// ═══════════════════════════════════════════════════════════════════════════
bool getTime8266(struct tm* ti) {
  time_t now = time(nullptr);
  if (now < 1000000UL) return false;
  localtime_r(&now, ti);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  EEPROM
// ═══════════════════════════════════════════════════════════════════════════
void cfgSave() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println(F("Configuración guardada en EEPROM."));
}

void cfgLoad() {
  EEPROM.get(0, cfg);
  if (cfg.magic == MAGIC_V) {
    Serial.println(F("Configuración cargada desde EEPROM."));
    int len = strnlen(cfg.webPin, 16);
    bool valid = (len >= 1 && len < 16);
    for (int i = 0; valid && i < len; i++)
      if ((byte)cfg.webPin[i] < 32 || (byte)cfg.webPin[i] > 126) valid = false;
    if (!valid) { strcpy(cfg.webPin, "1234"); cfgSave(); }
  }
}

void cfgDefault() {
  cfg.magic = MAGIC_V;
  strcpy(cfg.wSSID,  "MiRedWiFi");
  strcpy(cfg.wPass,  "");
  strcpy(cfg.botTok, "INGRESA_TOKEN_AQUI");
  strcpy(cfg.admNom, "Administrador");
  strcpy(cfg.admID,  "INGRESA_CHATID");
  strcpy(cfg.grpID,  "");
  strcpy(cfg.apSSID, "Alarma ESP01");
  strcpy(cfg.apPass, "00000000");
  strcpy(cfg.webPin, "1234");
  cfg.desplegado = false;
  cfgSave();
}

// ═══════════════════════════════════════════════════════════════════════════
//  WIFI / ACCESS POINT / LED
// ═══════════════════════════════════════════════════════════════════════════
bool wifiTry(const char* ssid, const char* pass, int tries) {
  for (int t = 1; t <= tries; t++) {
    Serial.printf("  Intento %d/%d → %s\n", t, tries, ssid);
    WiFi.disconnect(true); delay(300);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); yield(); Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("  Conectado. IP: " + WiFi.localIP().toString());
      return true;
    }
    Serial.println(F("  Intento fallido."));
  }
  return false;
}

/**
 * wifiConnect()
 * Primer intento sin disconnect para conectar en 1-3s.
 * Solo hace disconnect/reconnect si falla. Polling a 250ms.
 */
void wifiConnect() {
  int tries = cfg.desplegado ? WIFI_MAX_TRIES : 3;
  Serial.printf("Conectando a '%s'...\n", cfg.wSSID);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wSSID, cfg.wPass);

  for (int t = 0; t < tries && WiFi.status() != WL_CONNECTED; t++) {
    if (t > 0) {
      Serial.printf("  Reintento %d/%d\n", t + 1, tries);
      WiFi.disconnect(); delay(200);
      WiFi.begin(cfg.wSSID, cfg.wPass);
    }
    for (int i = 0; i < 32 && WiFi.status() != WL_CONNECTED; i++) {
      delay(250); yield(); Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("  Conectado. IP: " + WiFi.localIP().toString());
      return;
    }
    Serial.printf("  [%d/%d] Sin conexión.\n", t + 1, tries);
  }
  Serial.println(F("WiFi: no disponible."));
}

void apStart() {
  if (apMode) return;
  WiFi.disconnect(true); delay(300);
  WiFi.mode(WIFI_AP);
  const char* apN = (strlen(cfg.apSSID) > 0) ? cfg.apSSID : "Alarma ESP01";
  const char* apP = (strlen(cfg.apPass) >= 8) ? cfg.apPass : "00000000";
  WiFi.softAP(apN, apP);
  delay(500);
  dnsSrv.start(DNS_PORT, "*", WiFi.softAPIP());
  apMode = true;
  digitalWrite(PIN_LED, LED_OFF);
  Serial.println("Modo AP: " + String(apN) + "  IP: " + WiFi.softAPIP().toString());
}

void ledBlink(int n) {
  for (int i = 0; i < n; i++) {
    digitalWrite(PIN_LED, LED_ON);  delay(150);
    digitalWrite(PIN_LED, LED_OFF); delay(150);
  }
  if (!apMode && WiFi.status() == WL_CONNECTED) digitalWrite(PIN_LED, LED_ON);
}

// ═══════════════════════════════════════════════════════════════════════════
//  BOTÓN MANUAL GPIO2
// ═══════════════════════════════════════════════════════════════════════════
void btnCheck() {
  btnCur = digitalRead(PIN_BTN);

  if (btnCur == LOW && inSleep) {
    wakeFromSleep();
  }
  if (btnCur == LOW) lastActivityMs = millis();

  if (btnCur != btnPrev) {
    if (btnCur == LOW) {
      btnMs = millis(); alarmSent = false; lastSec = -1;
      Serial.println(F("BOTÓN GPIO2 PRESIONADO — mantener 6s para alarmar..."));
    } else {
      if (!alarmSent) Serial.println(F("Botón soltado — alarma manual cancelada."));
      btnMs = 0; alarmSent = false; lastSec = -1;
    }
    btnPrev = btnCur;
  }

  if (btnCur == LOW && !alarmSent && btnMs > 0) {
    unsigned long elapsed = millis() - btnMs;
    int sec = elapsed / 1000;
    if (sec != lastSec && sec >= 1) { lastSec = sec; Serial.printf("  Botón: %d/6s\n", sec); }
    if (elapsed >= HOLD_MS) {
      alarmFire("Botón manual GPIO2 (6s hold)");
      if (!cfg.desplegado) { btnMs = millis(); alarmSent = false; lastSec = -1; }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  ALARMA — DISPARO PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════
void alarmFire(const char* motivo) {
  alarmSent = true;
  Serial.println(F("══════════════════════════════════════════════"));
  Serial.println(F("  !!!  ALARMA ACTIVADA  !!!"));
  Serial.printf("  Motivo: %s\n", motivo);
  Serial.println(F("══════════════════════════════════════════════"));
  // Guardar hora de activación
  struct tm ti;
  if (getTime8266(&ti)) strftime(lastAlarmTime, 20, "%H:%M:%S", &ti);

  // Esperar WiFi si es necesario (hasta 8s)
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Esperando WiFi para enviar alarma..."));
    for (int i = 0; i < 16 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); yield();
    }
  }

  sendAlarmTelegram(motivo);
  ledBlink(5);
}

/**
 * sendAlarmTelegram()
 * Envía los mensajes de alarma únicamente al canal de emergencia.
 * Separado de alarmFire() para poder llamarlo periódicamente.
 */
void sendAlarmTelegram(const char* motivo) {
  if (!bot || WiFi.status() != WL_CONNECTED) {
    Serial.println(F("ERROR: Bot o WiFi no disponibles."));
    return;
  }

  if (strlen(cfg.grpID) <= 3) { Serial.println(F("AVISO: Sin canal configurado.")); return; }

  String msg  = "🚨 *¡ALARMA DE SEGURIDAD!* 🚨\n\n";
  msg += "📍 *Casa de LUGAR O CASA DE PERSONA*\n";
  msg += "⏰ Hora: `" + String(lastAlarmTime) + "`\n";
  msg += "⚡ Motivo: `" + String(motivo) + "`\n";
  msg += "🌐 IP: `" + WiFi.localIP().toString() + "`\n\n";
  msg += "‼️ *SE REQUIERE ATENCIÓN INMEDIATA* ‼️";

  Serial.println("Enviando al canal: " + String(cfg.grpID));
  for (int i = 0; i < 4; i++) {
    bool ok = bot->sendMessage(cfg.grpID, msg, "Markdown");
    Serial.printf("  Envío %d: %s\n", i+1, ok ? "OK" : "FALLO");
    if (i < 3) { delay(50); yield(); }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  TELEGRAM: MENSAJES ENTRANTES
// ═══════════════════════════════════════════════════════════════════════════
void tgProcess(int n) {
  for (int i = 0; i < n; i++) {
    String cid  = String(bot->messages[i].chat_id); cid.trim();
    String txt  = bot->messages[i].text;            txt.trim();
    String from = bot->messages[i].from_name;

    int at = txt.indexOf('@');
    if (at != -1) txt = txt.substring(0, at);
    txt.trim();

    Serial.println("TG | " + from + " [" + cid + "] → " + txt);

    String admCID = String(cfg.admID); admCID.trim();
    if (cid != admCID) {
      bot->sendMessage(cid, "🔒 Acceso denegado.\n\n🆔 Tu Chat ID: `" + cid + "`", "Markdown");
      continue;
    }

    lastActivityMs = millis();

    if (txt == "/start") {
      String s = "👋 Hola *" + String(cfg.admNom) + "*\n\n";
      s += "🛡 *Sistema Alarma ESP01 3V3*\n\n";
      s += "Modo actual: " + String(cfg.desplegado ? "🔴 *DESPLEGADO*" : "🟢 No Desplegado") + "\n\n";
      s += "Usa /help para los comandos.";
      bot->sendMessage(cid, s, "Markdown");

    } else if (txt == "/status" || txt == "/estado") {
      unsigned long elapsed = millis() - lastActivityMs;
      unsigned long sleepIn = (elapsed < SLEEP_TIMEOUT_MS) ? (SLEEP_TIMEOUT_MS - elapsed) / 1000 : 0;
      String s = "📊 *Estado del Sistema*\n\n";
      s += "🔴 Modo: " + String(cfg.desplegado ? "*DESPLEGADO* (armado)" : "No Desplegado") + "\n";
      s += "🔘 Botón GPIO2: " + String(btnCur == LOW ? "⚠️ PRESIONADO" : "✅ Libre") + "\n";
      s += "⚡ Alarma activa: " + String(alarmActive ? "🚨 SÍ" : "No") + "\n";
      s += "🕐 Última alarma: `" + String(lastAlarmTime) + "`\n";
      s += "📶 WiFi: " + String(WiFi.RSSI()) + " dBm\n";
      s += "🌐 IP: `" + WiFi.localIP().toString() + "`\n";
      s += "💤 Reposo en: " + String(cfg.desplegado ? "N/A (desplegado)" : (String(sleepIn/60) + "m " + String(sleepIn%60) + "s")) + "\n\n";
      s += "✅ Sistema operativo";
      bot->sendMessage(cid, s, "Markdown");

    } else if (txt == "/ip") {
      String s = "🌐 *IP del Sistema*\n\n`" + WiFi.localIP().toString() + "`\n\nhttp://" + WiFi.localIP().toString();
      bot->sendMessage(cid, s, "Markdown");

    } else if (txt == "/pin") {
      String s = "🔑 PIN portal web:\n\n`" + String(cfg.webPin) + "`\n\nhttp://" + WiFi.localIP().toString();
      bot->sendMessage(cid, s, "Markdown");

    } else if (txt == "/test") {
      bot->sendMessage(cid, "✅ Sistema funcionando correctamente.\nModo: " + String(cfg.desplegado ? "DESPLEGADO" : "No Desplegado"), "");

    } else if (txt == "/getchatid") {
      bot->sendMessage(cid, "🆔 Tu Chat ID:\n\n`" + cid + "`", "Markdown");

    } else if (txt == "/desplegar") {
      if (cfg.desplegado) {
        bot->sendMessage(cid, "⚠️ El sistema ya está desplegado.", "");
      } else {
        cfg.desplegado = true; cfgSave();
        bot->sendMessage(cid,
          "🔴 *Sistema DESPLEGADO*\n\n"
          "El próximo encendido disparará la alarma en 6 segundos.",
          "Markdown");
      }

    } else if (txt == "/replegar") {
      if (!cfg.desplegado) {
        bot->sendMessage(cid, "ℹ️ El sistema ya está en modo No Desplegado.", "");
      } else {
        cfg.desplegado = false; alarmActive = false; cfgSave();
        bot->sendMessage(cid,
          "🟢 *Sistema REPLEGADO*\n\nVuelto a modo de configuración.",
          "Markdown");
      }

    } else if (txt == "/dormir") {
      if (cfg.desplegado) {
        bot->sendMessage(cid, "⚠️ En modo Desplegado el reposo está desactivado.", "");
      } else {
        bot->sendMessage(cid, "💤 *Apagando WiFi (Modem Sleep)...*", "Markdown");
        bot->getUpdates(bot->last_message_received + 1);
        delay(1000);
        enterSleep();
      }

    } else if (txt == "/reiniciar") {
      bot->sendMessage(cid, "🔄 *Reiniciando...*", "Markdown");
      bot->getUpdates(bot->last_message_received + 1);
      delay(1500); ESP.restart();

    } else if (txt == "/help") {
      String s = "📋 *Comandos disponibles:*\n\n";
      s += "/status — Estado del sistema\n";
      s += "/ip — IP y enlace al portal\n";
      s += "/pin — PIN del portal web\n";
      s += "/test — Prueba de conectividad\n";
      s += "/desplegar — Armar el sistema\n";
      s += "/replegar — Desarmar el sistema\n";
      s += "/dormir — Modem Sleep (solo si No Desplegado)\n";
      s += "/reiniciar — Reiniciar el ESP-01\n";
      s += "/getchatid — Tu Chat ID\n";
      s += "/help — Esta ayuda";
      bot->sendMessage(cid, s, "Markdown");

    } else {
      bot->sendMessage(cid, "❓ Comando no reconocido. Usa /help.", "");
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  REPORTE DIARIO 18:00
// ═══════════════════════════════════════════════════════════════════════════
void rptSend() {
  if (!bot || WiFi.status() != WL_CONNECTED) return;
  if (strlen(cfg.admID) < 4) return;
  struct tm ti;
  if (!getTime8266(&ti)) return;
  char dt[40]; strftime(dt, 40, "%d/%m/%Y %H:%M", &ti);

  String m = "📅 *Reporte Diario — 18:00*\n\n";
  m += "🗓 `" + String(dt) + "`\n";
  m += "🔴 Modo: " + String(cfg.desplegado ? "*DESPLEGADO*" : "No Desplegado") + "\n";
  m += "⚡ Alarma activa: " + String(alarmActive ? "🚨 SÍ" : "No") + "\n";
  m += "🕐 Última alarma: `" + String(lastAlarmTime) + "`\n";
  m += "📶 WiFi: " + String(WiFi.RSSI()) + " dBm\n";
  m += "🌐 IP: `" + WiFi.localIP().toString() + "`\n\n";
  m += "🛡 *Sistema operativo y monitoreando.*";

  bot->sendMessage(cfg.admID, m, "Markdown");
  Serial.println(F("Reporte diario enviado."));
}

void rptCheck() {
  if (!ntpReady) { if (time(nullptr) > 1000000UL) ntpReady = true; else return; }
  struct tm ti;
  if (!getTime8266(&ti)) return;
  if (ti.tm_hour == 18 && ti.tm_min == 0 && rptDay != ti.tm_yday) {
    rptDay = ti.tm_yday; rptToday = true;
    rptSend();
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SESIÓN WEB
// ═══════════════════════════════════════════════════════════════════════════
String sesGen() { return String(millis()) + String(random(0x7FFFFFFF)); }

bool sesOk(String tok) {
  if (tok.isEmpty() || sesTok.isEmpty() || tok != sesTok) return false;
  if (millis() - sesMs > SES_TOUT) { sesTok = ""; return false; }
  sesMs = millis();
  return true;
}

bool httpAuth() {
  String cookie = webSrv.header("Cookie");
  int idx = cookie.indexOf("sh_tok=");
  if (idx == -1) return false;
  String tok = cookie.substring(idx + 7);
  int end = tok.indexOf(';');
  if (end != -1) tok = tok.substring(0, end);
  tok.trim();
  if (sesOk(tok)) { lastActivityMs = millis(); return true; }
  return false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  MODEM SLEEP
// ═══════════════════════════════════════════════════════════════════════════
void enterSleep() {
  if (inSleep || cfg.desplegado) return; // No dormir en modo Desplegado
  Serial.println(F("\n>>> Modem Sleep: apagando WiFi <<<"));
  if (bot && WiFi.status() == WL_CONNECTED && strlen(cfg.admID) > 3) {
    bot->sendMessage(cfg.admID, "💤 *Sistema en reposo (WiFi apagado)*\nPresiona GPIO2 para despertar.", "Markdown");
    delay(500);
  }
  digitalWrite(PIN_LED, LED_OFF);
  WiFi.forceSleepBegin();
  delay(1);
  inSleep = true;
}

void wakeFromSleep() {
  if (!inSleep) return;
  inSleep = false;
  Serial.println(F("\n>>> GPIO2 — despertando <<<"));
  btnMs = millis(); alarmSent = false; lastSec = -1; btnPrev = LOW;
  WiFi.forceSleepWake(); delay(1);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wSSID, cfg.wPass);
  for (int i = 0; i < 14 && WiFi.status() != WL_CONNECTED; i++) { delay(500); yield(); }
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(PIN_LED, LED_ON);
    if (!ntpReady) configTime(UTC_OFFS, DST_OFFS, NTP_HOST);
    Serial.println("WiFi reconectado. IP: " + WiFi.localIP().toString());
  }
  lastActivityMs = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIGURACIÓN DEL SERVIDOR WEB
// ═══════════════════════════════════════════════════════════════════════════
void webSetup() {
  webSrv.collectHeaders("Cookie");
  webSrv.on("/",              HTTP_GET,  hRoot);
  webSrv.on("/login",         HTTP_GET,  hLoginGET);
  webSrv.on("/login",         HTTP_POST, hLoginPOST);
  webSrv.on("/logout",        HTTP_POST, hLogout);
  webSrv.on("/status-json",   HTTP_GET,  hStatus);
  webSrv.on("/scan-wifi",     HTTP_GET,  hScanWifi);
  webSrv.on("/save-wifi",     HTTP_POST, hSaveWifi);
  webSrv.on("/save-token",    HTTP_POST, hSaveTok);
  webSrv.on("/save-admin",    HTTP_POST, hSaveAdmin);
  webSrv.on("/save-grupo",    HTTP_POST, hSaveGrupo);
  webSrv.on("/save-ap",       HTTP_POST, hSaveAP);
  webSrv.on("/save-pin",      HTTP_POST, hSavePin);
  webSrv.on("/test",          HTTP_POST, hTest);
  webSrv.on("/reset",         HTTP_POST, hReset);
  webSrv.on("/reboot",        HTTP_POST, hReboot);
  webSrv.on("/force-sleep",   HTTP_POST, hForceSleep);
  webSrv.on("/desplegar",     HTTP_POST, hDesplegar);
  webSrv.on("/replegar",      HTTP_POST, hReplegar);
  webSrv.on("/generate_204",        HTTP_GET, []() { webSrv.sendHeader("Location", "/"); webSrv.send(302); });
  webSrv.on("/connecttest.txt",     HTTP_GET, []() { webSrv.sendHeader("Location", "/"); webSrv.send(302); });
  webSrv.on("/hotspot-detect.html", HTTP_GET, []() { webSrv.sendHeader("Location", "/"); webSrv.send(302); });
  webSrv.on("/fwlink",              HTTP_GET, []() { webSrv.sendHeader("Location", "/"); webSrv.send(302); });
  webSrv.onNotFound(hNotFound);
}

void hNotFound() { webSrv.sendHeader("Location", "/"); webSrv.send(302, "text/plain", "Redirecting..."); }

// ── Status JSON ───────────────────────────────────────────────────────────
void hStatus() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }

  unsigned long actElapsed = millis() - lastActivityMs;
  unsigned long sleepIn    = (actElapsed < SLEEP_TIMEOUT_MS) ? (SLEEP_TIMEOUT_MS - actElapsed) : 0;

  // Cuenta regresiva auto-alarma
  int cdSec = 0;
  if (autoCountdown) {
    unsigned long elapsed = millis() - autoStartMs;
    int rem = (int)(AUTO_ALARM_MS / 1000) - (int)(elapsed / 1000);
    cdSec = rem > 0 ? rem : 0;
  }

  String j = "{";
  j += "\"ssid\":\""       + String(cfg.wSSID)  + "\",";
  j += "\"rssi\":"         + String(apMode ? 0 : WiFi.RSSI()) + ",";
  j += "\"ip\":\""         + (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  j += "\"boton\":"        + String(btnCur == LOW ? "true" : "false") + ",";
  j += "\"botOk\":"        + String(bot != nullptr ? "true" : "false") + ",";
  j += "\"admin\":\""      + String(cfg.admNom) + "\",";
  j += "\"admID\":\""      + String(cfg.admID)  + "\",";
  j += "\"grpID\":\""      + String(cfg.grpID)  + "\",";
  j += "\"grupoOk\":"      + String(strlen(cfg.grpID) > 3 ? "true" : "false") + ",";
  j += "\"apSSID\":\""     + String(cfg.apSSID) + "\",";
  j += "\"apMode\":"       + String(apMode ? "true" : "false") + ",";
  j += "\"uptime\":"       + String(millis() - bootT) + ",";
  j += "\"sleepIn\":"      + String(sleepIn / 1000) + ",";
  j += "\"desplegado\":"   + String(cfg.desplegado ? "true" : "false") + ",";
  j += "\"alarmActive\":"  + String(alarmActive ? "true" : "false") + ",";
  j += "\"autoCD\":"       + String(autoCountdown ? "true" : "false") + ",";
  j += "\"cdSec\":"        + String(cdSec) + ",";
  j += "\"lastAlarm\":\""  + String(lastAlarmTime) + "\"";
  j += "}";
  webSrv.send(200, "application/json", j);
}

// ── Desplegar / Replegar ──────────────────────────────────────────────────
void hDesplegar() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  if (!bot || WiFi.status() != WL_CONNECTED) {
    webSrv.send(503, "text/plain", "SIN_WIFI_O_BOT");
    return;
  }
  if (strlen(cfg.admID) < 4 || strlen(cfg.botTok) < 10) {
    webSrv.send(400, "text/plain", "SIN_CONFIG_TG");
    return;
  }
  cfg.desplegado = true;
  cfgSave();
  // Notificar
  if (strlen(cfg.admID) > 3)
    bot->sendMessage(cfg.admID,
      "🔴 *¡Sistema DESPLEGADO desde el portal web!*\n\n"
      "El próximo encendido (restauración de 3.3V) disparará\n"
      "la alarma *de inmediato*.",
      "Markdown");
  webSrv.send(200, "text/plain", "OK");
}

void hReplegar() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  cfg.desplegado = false;
  alarmActive    = false;
  autoCountdown  = false;
  cfgSave();
  if (bot && WiFi.status() == WL_CONNECTED && strlen(cfg.admID) > 3)
    bot->sendMessage(cfg.admID, "🟢 *Sistema REPLEGADO desde el portal web.*", "Markdown");
  webSrv.send(200, "text/plain", "OK");
}

// ── Scan WiFi ─────────────────────────────────────────────────────────────
void hScanWifi() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) j += ",";
    j += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    j += "\"rssi\":"    + String(WiFi.RSSI(i)) + ",";
    j += "\"secured\":" + String(WiFi.encryptionType(i) != ENC_TYPE_NONE ? "true" : "false") + "}";
    delay(5); yield();
  }
  j += "]"; WiFi.scanDelete();
  webSrv.send(200, "application/json", j);
}

// ── Save WiFi ─────────────────────────────────────────────────────────────
void hSaveWifi() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  if (!webSrv.hasArg("ssid")) { webSrv.send(400, "text/plain", "MISSING"); return; }
  char oldSSID[32], oldPass[64];
  strncpy(oldSSID, cfg.wSSID, 32); strncpy(oldPass, cfg.wPass, 64);
  webSrv.arg("ssid").toCharArray(cfg.wSSID, 32);
  webSrv.arg("password").toCharArray(cfg.wPass, 64);
  if (wifiTry(cfg.wSSID, cfg.wPass, 2)) {
    cfgSave();
    webSrv.send(200, "text/plain", "OK");
    delay(800); ESP.restart();
  } else {
    strncpy(cfg.wSSID, oldSSID, 32); strncpy(cfg.wPass, oldPass, 64);
    wifiTry(oldSSID, oldPass, 2);
    webSrv.send(200, "text/plain", "FALLBACK");
  }
}

// ── Save Token ────────────────────────────────────────────────────────────
void hSaveTok() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  if (!webSrv.hasArg("token")) { webSrv.send(400, "text/plain", "MISSING"); return; }
  webSrv.arg("token").toCharArray(cfg.botTok, 64);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
  delay(1000); ESP.restart();
}

// ── Save Admin ────────────────────────────────────────────────────────────
void hSaveAdmin() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  webSrv.arg("nombre").toCharArray(cfg.admNom, 32);
  webSrv.arg("chatID").toCharArray(cfg.admID,  20);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
}

// ── Save Grupo ────────────────────────────────────────────────────────────
void hSaveGrupo() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  webSrv.arg("chatID").toCharArray(cfg.grpID, 20);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
}

// ── Save AP ───────────────────────────────────────────────────────────────
void hSaveAP() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  String n = webSrv.arg("ssid"); n.trim();
  String p = webSrv.arg("password"); p.trim();
  if (n.length() < 1 || p.length() < 8) { webSrv.send(400, "text/plain", "INVALID"); return; }
  n.toCharArray(cfg.apSSID, 32);
  p.toCharArray(cfg.apPass, 32);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
}

// ── Save PIN ──────────────────────────────────────────────────────────────
void hSavePin() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  String p = webSrv.arg("pin"); p.trim();
  if (p.length() < 4) { webSrv.send(400, "text/plain", "TOO_SHORT"); return; }
  p.toCharArray(cfg.webPin, 16);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
}

// ── Test ──────────────────────────────────────────────────────────────────
void hTest() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  if (!bot || WiFi.status() != WL_CONNECTED) { webSrv.send(503, "text/plain", "UNAVAILABLE"); return; }
  if (strlen(cfg.admID) < 4) { webSrv.send(400, "text/plain", "NO_ADMIN"); return; }
  String m = "🔒 *Prueba del Sistema (ESP-01 3V3)*\n\n";
  m += "✅ Conexión verificada\n";
  m += "🔴 Modo: " + String(cfg.desplegado ? "*DESPLEGADO*" : "No Desplegado") + "\n";
  m += "📶 WiFi: " + String(WiFi.RSSI()) + " dBm\n";
  m += "🌐 IP: `" + WiFi.localIP().toString() + "`\n\n";
  m += "🛡 *Sistema armado y activo.*";
  bool ok = bot->sendMessage(cfg.admID, m, "Markdown");
  webSrv.send(200, "text/plain", ok ? "OK" : "ERROR");
}

// ── Reset fábrica ─────────────────────────────────────────────────────────
void hReset() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  for (int i = 0; i < EEPROM_SZ; i++) EEPROM.write(i, 0);
  EEPROM.commit();
  webSrv.send(200, "text/plain", "OK");
  delay(2000); ESP.restart();
}

// ── Reboot ────────────────────────────────────────────────────────────────
void hReboot() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  webSrv.send(200, "text/plain", "OK");
  delay(800); ESP.restart();
}

// ── Force Sleep ───────────────────────────────────────────────────────────
void hForceSleep() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  if (cfg.desplegado) { webSrv.send(400, "text/plain", "DESPLEGADO_NO_SLEEP"); return; }
  webSrv.send(200, "text/plain", "OK");
  delay(400);
  enterSleep();
}

// ── Logout ───────────────────────────────────────────────────────────────
void hLogout() {
  sesTok = "";
  webSrv.sendHeader("Set-Cookie", "sh_tok=; Max-Age=0; Path=/");
  webSrv.sendHeader("Location", "/login");
  webSrv.send(302);
}

// ═══════════════════════════════════════════════════════════════════════════
//  PÁGINA DE LOGIN
// ═══════════════════════════════════════════════════════════════════════════
void hLoginGET() {
  if (httpAuth()) { webSrv.sendHeader("Location", "/"); webSrv.send(302); return; }
  static const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es" data-theme="dark"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Alarma ESP01 3V3 — Acceso</title>
<style>
:root{--bg:#070b14;--sur:rgba(255,255,255,.048);--bor:rgba(255,255,255,.09);--ac:#ff4d1c;--ac2:#ff9500;--tx:#e8ecf4;--mu:#5a6a8a}
[data-theme=light]{--bg:#f4f1ee;--sur:rgba(0,0,0,.04);--bor:rgba(0,0,0,.10);--tx:#1a1207;--mu:#6b7280}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',Courier,monospace;background:var(--bg);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px;transition:background .3s}
body::before{content:'';position:fixed;inset:0;background-image:linear-gradient(rgba(255,77,28,.018) 1px,transparent 1px),linear-gradient(90deg,rgba(255,77,28,.018) 1px,transparent 1px);background-size:40px 40px;pointer-events:none}
.orb{position:fixed;border-radius:50%;filter:blur(100px);pointer-events:none}
.o1{width:500px;height:500px;background:rgba(255,77,28,.06);top:-150px;right:-100px;animation:drift 10s ease-in-out infinite}
.o2{width:380px;height:380px;background:rgba(255,149,0,.05);bottom:-120px;left:-120px;animation:drift 10s ease-in-out infinite;animation-delay:-5s}
@keyframes drift{0%,100%{transform:translate(0,0)}50%{transform:translate(20px,-20px)}}
.card{width:100%;max-width:380px;background:var(--sur);border:1px solid var(--bor);border-radius:4px;padding:44px 38px;position:relative;z-index:1;animation:up .45s cubic-bezier(.16,1,.3,1)}
@keyframes up{from{opacity:0;transform:translateY(16px)}to{opacity:1;transform:translateY(0)}}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;background:linear-gradient(90deg,transparent,var(--ac),var(--ac2),transparent)}
.logo{text-align:center;margin-bottom:38px}
.hex{width:60px;height:60px;margin:0 auto 16px;display:flex;align-items:center;justify-content:center;border:1.5px solid var(--ac);transform:rotate(45deg)}
.hex-i{transform:rotate(-45deg)}
.logo h1{font-size:13px;font-weight:700;color:var(--tx);letter-spacing:4px;text-transform:uppercase}
.logo h1 em{font-style:normal;color:var(--ac)}
.logo p{font-size:10px;color:var(--mu);margin-top:8px;letter-spacing:3px;text-transform:uppercase}
.label{display:block;font-size:10px;font-weight:700;color:var(--mu);text-transform:uppercase;letter-spacing:3px;margin-bottom:8px}
.inp-wrap{position:relative;margin-bottom:22px}
.inp-ico{position:absolute;left:13px;top:50%;transform:translateY(-50%);color:var(--mu);transition:color .2s;pointer-events:none;font-size:14px}
.inp-wrap:focus-within .inp-ico{color:var(--ac)}
input[type=password]{width:100%;padding:11px 13px 11px 40px;background:rgba(255,255,255,.035);border:1px solid var(--bor);border-radius:2px;color:var(--tx);font-size:16px;letter-spacing:.5em;font-family:'Courier New',monospace;transition:border .2s,box-shadow .2s}
[data-theme=light] input[type=password]{background:rgba(0,0,0,.04)}
input:focus{outline:none;border-color:rgba(255,77,28,.55);box-shadow:0 0 0 3px rgba(255,77,28,.08)}
.btn{width:100%;padding:12px;border:none;border-radius:2px;background:var(--ac);color:#fff;font-size:11px;font-weight:700;letter-spacing:3px;text-transform:uppercase;cursor:pointer;font-family:'Courier New',monospace;transition:opacity .2s,transform .15s}
.btn:hover{opacity:.85;transform:translateY(-1px)}
.err{background:rgba(255,77,28,.10);border:1px solid rgba(255,77,28,.3);color:var(--ac);border-radius:2px;padding:9px 13px;font-size:12px;text-align:center;margin-bottom:14px;display:none}
.err.show{display:block}
.theme-btn{position:absolute;top:14px;right:14px;background:none;border:1px solid var(--bor);border-radius:2px;padding:5px 9px;cursor:pointer;color:var(--mu);font-size:12px;font-family:'Courier New',monospace}
</style></head>
<body>
<div class="orb o1"></div><div class="orb o2"></div>
<button class="theme-btn" onclick="tgl()">◐</button>
<div class="card">
  <div class="logo">
    <div class="hex"><div class="hex-i">
      <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="#ff4d1c" stroke-width="1.5"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><path d="M9 12l2 2 4-4"/></svg>
    </div></div>
    <h1>ALARMA <em>3V3</em></h1>
    <p>Sistema · ESP-01 · Activación por 3.3V</p>
  </div>
  <form id="frm" onsubmit="login(event)">
    <label class="label">Código de acceso</label>
    <div class="inp-wrap">
      <span class="inp-ico">▶</span>
      <input type="password" id="pin" autocomplete="current-password" autofocus required>
    </div>
    <div class="err" id="err">PIN incorrecto. Intenta de nuevo.</div>
    <button class="btn" type="submit" id="btn">INGRESAR</button>
  </form>
</div>
<script>
const h=document.documentElement;
h.setAttribute('data-theme',localStorage.getItem('sh_theme')||'dark');
function tgl(){const t=h.getAttribute('data-theme')==='dark'?'light':'dark';h.setAttribute('data-theme',t);localStorage.setItem('sh_theme',t);}
async function login(e){
  e.preventDefault();
  const btn=document.getElementById('btn');
  btn.textContent='...';btn.disabled=true;
  const fd=new URLSearchParams({pin:document.getElementById('pin').value});
  const r=await fetch('/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd});
  const t=await r.text();
  if(t==='OK'){window.location.replace('/');}
  else{document.getElementById('err').classList.add('show');document.getElementById('pin').value='';btn.textContent='INGRESAR';btn.disabled=false;}
}
</script></body></html>
)rawliteral";
  webSrv.send(200, "text/html", PAGE);
}

void hLoginPOST() {
  String pin = webSrv.arg("pin"); pin.trim();
  String stored = String(cfg.webPin); stored.trim();
  if (pin == stored) {
    sesTok = sesGen(); sesMs = millis();
    webSrv.sendHeader("Set-Cookie", "sh_tok=" + sesTok + "; Path=/; HttpOnly");
    webSrv.send(200, "text/plain", "OK");
  } else {
    webSrv.send(200, "text/plain", "WRONG");
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PORTAL PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════
void hRoot() {
  if (!httpAuth()) { webSrv.sendHeader("Location", "/login"); webSrv.send(302); return; }
  static const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es" data-theme="dark"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Alarma ESP01 3V3</title>
<style>
:root{--bg:#070b14;--bg2:#0b1020;--card:rgba(255,255,255,.04);--bor:rgba(255,255,255,.08);--tx:#e8ecf4;--mu:#5a6a8a;--ac:#ff4d1c;--ac2:#ff9500;--ok:#00e676;--er:#ff4757;--wa:#ffc107;--nh:60px;--sw:240px}
[data-theme=light]{--bg:#f4f1ee;--bg2:#ede9e4;--card:rgba(0,0,0,.04);--bor:rgba(0,0,0,.10);--tx:#1a1207;--mu:#6b7280}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',Courier,monospace;background:var(--bg);color:var(--tx);min-height:100vh;transition:background .3s,color .3s}
body::before{content:'';position:fixed;inset:0;background-image:linear-gradient(rgba(255,77,28,.015) 1px,transparent 1px),linear-gradient(90deg,rgba(255,77,28,.015) 1px,transparent 1px);background-size:40px 40px;pointer-events:none;z-index:0}
.nav{position:fixed;top:0;left:0;right:0;height:var(--nh);z-index:100;background:rgba(7,11,20,.9);backdrop-filter:blur(18px);border-bottom:1px solid var(--bor);display:flex;align-items:center;gap:14px;padding:0 20px}
[data-theme=light] .nav{background:rgba(244,241,238,.9)}
.nav-logo{display:flex;align-items:center;gap:9px;font-weight:700;font-size:12px;letter-spacing:2px;text-transform:uppercase;color:var(--tx);flex-shrink:0}
.nav-logo em{font-style:normal;color:var(--ac)}
.nav-stat{display:flex;align-items:center;gap:16px;margin-left:auto;font-size:11px}
.ni{display:flex;align-items:center;gap:6px;color:var(--mu)}
.dot{width:7px;height:7px;border-radius:50%;background:var(--ok);box-shadow:0 0 6px var(--ok)}
.dot.w{background:var(--wa);box-shadow:0 0 6px var(--wa)}.dot.e{background:var(--er);box-shadow:0 0 6px var(--er)}.dot.a{background:var(--ac);box-shadow:0 0 8px var(--ac);animation:pulse .7s ease-in-out infinite alternate}
@keyframes pulse{from{opacity:.6}to{opacity:1}}
.ibtn{background:none;border:1px solid var(--bor);border-radius:2px;padding:5px 10px;cursor:pointer;color:var(--mu);font-size:11px;font-family:'Courier New',monospace;transition:all .2s}
.ibtn:hover{border-color:var(--ac);color:var(--ac)}
.hbtn{display:none;align-items:center;justify-content:center;background:none;border:none;cursor:pointer;color:var(--tx);padding:4px}
.side{position:fixed;top:var(--nh);left:0;bottom:0;width:var(--sw);background:var(--bg2);border-right:1px solid var(--bor);padding:18px 10px;overflow-y:auto;z-index:90;transition:transform .3s}
.slbl{font-size:9px;font-weight:700;color:var(--mu);letter-spacing:3px;text-transform:uppercase;padding:0 10px;margin:16px 0 6px}
.sbtn{display:flex;align-items:center;gap:9px;padding:9px 10px;border-radius:2px;cursor:pointer;font-size:12px;font-weight:600;color:var(--mu);transition:all .2s;width:100%;border:none;background:none;font-family:'Courier New',monospace}
.sbtn:hover{background:var(--card);color:var(--tx)}.sbtn.on{background:rgba(255,77,28,.1);color:var(--ac);border:1px solid rgba(255,77,28,.22)}
.sbtn svg{width:14px;height:14px;flex-shrink:0}
.main{margin-left:var(--sw);padding:calc(var(--nh) + 28px) 32px 40px;position:relative;z-index:1}
.sec{display:none;animation:fi .25s ease}.sec.on{display:block}
@keyframes fi{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:translateY(0)}}
.ptit{font-size:20px;font-weight:700;margin-bottom:3px;letter-spacing:1px}.psub{font-size:12px;color:var(--mu);margin-bottom:24px;letter-spacing:.5px}
.card{background:var(--card);border:1px solid var(--bor);border-radius:3px;padding:22px;margin-bottom:16px;position:relative;transition:border-color .2s}
.card:hover{border-color:rgba(255,77,28,.2)}
.card::before{content:'';position:absolute;top:0;left:10%;right:10%;height:1px;background:linear-gradient(90deg,transparent,rgba(255,77,28,.3),transparent)}
.ctit{font-size:10px;font-weight:700;color:var(--mu);text-transform:uppercase;letter-spacing:2px;margin-bottom:18px;display:flex;align-items:center;gap:7px}
/* STATUS CARDS GRID */
.sgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:12px;margin-bottom:18px}
.sc{background:var(--card);border:1px solid var(--bor);border-radius:3px;padding:16px;transition:all .2s}
.sc:hover{transform:translateY(-2px);border-color:rgba(255,77,28,.25)}
.sico{width:32px;height:32px;border-radius:3px;display:flex;align-items:center;justify-content:center;margin-bottom:10px}
.ic{color:var(--ac);background:rgba(255,77,28,.12)}.iv{color:var(--ac2);background:rgba(255,149,0,.12)}.ig{color:var(--ok);background:rgba(0,230,118,.11)}.ir{color:var(--er);background:rgba(255,71,87,.11)}
.sv{font-size:16px;font-weight:700}.sl{font-size:10px;color:var(--mu);margin-top:3px;letter-spacing:.5px}
/* DESPLEGADO BANNER */
.mode-banner{border-radius:3px;padding:18px 20px;margin-bottom:18px;display:flex;align-items:center;gap:14px;border:1px solid}
.mode-banner.armed{background:rgba(255,77,28,.07);border-color:rgba(255,77,28,.35)}
.mode-banner.safe{background:rgba(0,230,118,.06);border-color:rgba(0,230,118,.28)}
.mode-icon{font-size:28px;flex-shrink:0}
.mode-title{font-size:14px;font-weight:700;letter-spacing:1px}
.mode-sub{font-size:11px;color:var(--mu);margin-top:4px}
/* COUNTDOWN */
.countdown{font-size:42px;font-weight:700;color:var(--ac);letter-spacing:3px;text-align:center;padding:14px 0;animation:pulse .5s ease-in-out infinite alternate;font-family:'Courier New',monospace}
/* FORMS */
.fr{margin-bottom:16px}.fr label{display:block;font-size:10px;font-weight:700;color:var(--mu);letter-spacing:2px;text-transform:uppercase;margin-bottom:7px}
.inp{width:100%;padding:9px 12px;border-radius:2px;background:rgba(255,255,255,.04);border:1px solid var(--bor);color:var(--tx);font-size:12px;font-family:'Courier New',monospace;transition:border .2s}
[data-theme=light] .inp{background:rgba(0,0,0,.04)}.inp:focus{outline:none;border-color:rgba(255,77,28,.5);box-shadow:0 0 0 3px rgba(255,77,28,.07)}
.hint{font-size:10px;color:var(--mu);margin-top:4px}.r2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
@media(max-width:560px){.r2{grid-template-columns:1fr}}
/* BUTTONS */
.btn{padding:9px 20px;border-radius:2px;font-size:11px;font-weight:700;cursor:pointer;transition:all .2s;border:none;display:inline-flex;align-items:center;gap:7px;letter-spacing:1.5px;font-family:'Courier New',monospace;text-transform:uppercase}
.bp{background:var(--ac);color:#fff;box-shadow:0 3px 16px rgba(255,77,28,.25)}.bp:hover{opacity:.85;transform:translateY(-1px)}
.bg{background:var(--card);border:1px solid var(--bor);color:var(--tx)}.bg:hover{border-color:var(--ac);color:var(--ac)}
.bd{background:rgba(255,71,87,.1);border:1px solid rgba(255,71,87,.28);color:var(--er)}.bd:hover{background:rgba(255,71,87,.18)}
.bsafe{background:rgba(0,230,118,.1);border:1px solid rgba(0,230,118,.28);color:var(--ok)}.bsafe:hover{background:rgba(0,230,118,.18)}
.brow{display:flex;flex-wrap:wrap;gap:9px;margin-top:6px}
/* WIFI LIST */
.wi{display:flex;align-items:center;gap:10px;padding:9px 12px;border-radius:2px;cursor:pointer;transition:background .2s;border:1px solid transparent}
.wi:hover{background:var(--card);border-color:var(--bor)}.wi.sel{background:rgba(255,77,28,.06);border-color:rgba(255,77,28,.22)}
.wss{font-size:12px;font-weight:600;flex:1}.wlk{font-size:10px;color:var(--mu)}
.bars{display:flex;align-items:flex-end;gap:2px;height:13px}.bar{width:3px;border-radius:1px;background:var(--mu)}.wi.sel .bar{background:var(--ac)}
/* TOASTS */
.ta{position:fixed;bottom:20px;right:20px;z-index:999;display:flex;flex-direction:column;gap:7px}
.toast{padding:10px 16px;border-radius:2px;font-size:11px;font-weight:700;animation:tsi .3s cubic-bezier(.16,1,.3,1);min-width:200px;display:flex;align-items:center;gap:9px;font-family:'Courier New',monospace;letter-spacing:.5px}
.tok{background:rgba(0,230,118,.14);border:1px solid rgba(0,230,118,.3);color:var(--ok)}
.ter{background:rgba(255,71,87,.14);border:1px solid rgba(255,71,87,.3);color:var(--er)}
.tin{background:rgba(255,77,28,.12);border:1px solid rgba(255,77,28,.3);color:var(--ac)}
@keyframes tsi{from{opacity:0;transform:translateX(30px)}to{opacity:1;transform:translateX(0)}}
/* MODAL */
.mbg{position:fixed;inset:0;background:rgba(0,0,0,.7);backdrop-filter:blur(4px);z-index:200;display:none;align-items:center;justify-content:center}.mbg.on{display:flex}
.modal{background:var(--bg2);border:1px solid var(--bor);border-top:2px solid var(--ac);border-radius:2px;padding:28px;max-width:360px;width:90%;animation:fi .3s cubic-bezier(.16,1,.3,1)}
.modal h3{font-size:15px;font-weight:700;margin-bottom:8px;letter-spacing:1px}.modal p{color:var(--mu);font-size:12px;margin-bottom:20px;line-height:1.6}
.spin{width:12px;height:12px;border:2px solid rgba(255,255,255,.2);border-top-color:#fff;border-radius:50%;animation:rot .6s linear infinite;flex-shrink:0}
@keyframes rot{to{transform:rotate(360deg)}}
.ibox{border-radius:2px;padding:12px 14px;font-size:11px;color:var(--mu);margin-bottom:16px;line-height:1.6}
.ibox.red{background:rgba(255,77,28,.06);border:1px solid rgba(255,77,28,.18);color:rgba(255,180,150,.85)}
.ibox.grn{background:rgba(0,230,118,.06);border:1px solid rgba(0,230,118,.18);color:rgba(120,220,160,.85)}
.ibox.org{background:rgba(255,149,0,.06);border:1px solid rgba(255,149,0,.18)}
@media(max-width:768px){.side{transform:translateX(-100%)}.side.on{transform:translateX(0)}.main{margin-left:0;padding-left:16px;padding-right:16px}.hbtn{display:flex!important}.nav-ip{display:none}}
#overlay{display:none;position:fixed;inset:0;z-index:89;background:rgba(0,0,0,.5)}
</style></head>
<body>
<nav class="nav">
  <button class="hbtn" onclick="tgSide()">
    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="3" y1="6" x2="21" y2="6"/><line x1="3" y1="12" x2="21" y2="12"/><line x1="3" y1="18" x2="21" y2="18"/></svg>
  </button>
  <div class="nav-logo">
    <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#ff4d1c" stroke-width="1.5"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><path d="M9 12l2 2 4-4"/></svg>
    ALARMA <em>&nbsp;3V3</em> <span style="font-size:9px;color:var(--mu);margin-left:4px">ESP-01</span>
  </div>
  <div class="nav-stat">
    <div class="ni"><span class="dot" id="wdot"></span><span id="wlbl">...</span></div>
    <div class="ni nav-ip" id="iplbl" style="font-size:10px"></div>
    <div class="ni" id="modeBadge" style="display:none"><span class="dot a"></span><span style="color:var(--ac);font-weight:700;font-size:10px">DESPLEGADO</span></div>
    <button class="ibtn" onclick="tgTheme()">◐</button>
    <form action="/logout" method="POST" style="margin:0"><button class="ibtn" type="submit">⎋ SALIR</button></form>
  </div>
</nav>

<aside class="side" id="side">
  <div class="slbl">Principal</div>
  <button class="sbtn on" onclick="go('dash',this)">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/></svg>Dashboard</button>
  <button class="sbtn" onclick="go('deploy',this)" id="sbDeploy">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>Desplegar / Replegar</button>
  <div class="slbl">Configuración</div>
  <button class="sbtn" onclick="go('wifi',this)">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12.55a11 11 0 0114.08 0"/><path d="M1.42 9a16 16 0 0121.16 0"/><path d="M8.53 16.11a6 6 0 016.95 0"/><circle cx="12" cy="20" r="1" fill="currentColor"/></svg>Red WiFi</button>
  <button class="sbtn" onclick="go('tg',this)">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg>Telegram Bot</button>
  <button class="sbtn" onclick="go('grp',this)">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 21v-2a4 4 0 00-4-4H5a4 4 0 00-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 00-3-3.87"/><path d="M16 3.13a4 4 0 010 7.75"/></svg>Grupo Emergencia</button>
  <button class="sbtn" onclick="go('ap',this)">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="2"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg>Access Point</button>
  <button class="sbtn" onclick="go('sys',this)">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.07 4.93a10 10 0 010 14.14M4.93 4.93a10 10 0 000 14.14"/></svg>Sistema</button>
</aside>

<main class="main">

<!-- DASHBOARD -->
<div class="sec on" id="s-dash">
  <div class="ptit">Dashboard</div><div class="psub">Estado en tiempo real — ESP-01 · Activación 3.3V</div>

  <!-- Banner de modo -->
  <div class="mode-banner safe" id="modeBanner">
    <div class="mode-icon" id="modeIcon">🟢</div>
    <div>
      <div class="mode-title" id="modeTitle">NO DESPLEGADO</div>
      <div class="mode-sub" id="modeSub">Sistema en modo configuración. Seguro.</div>
    </div>
  </div>

  <!-- Cuenta regresiva auto-alarma -->
  <div class="card" id="cdCard" style="display:none;border-color:rgba(255,77,28,.5)">
    <div class="ctit" style="color:var(--ac)">⚡ CUENTA REGRESIVA — ALARMA AUTOMÁTICA</div>
    <div class="countdown" id="cdVal">6</div>
    <p style="text-align:center;font-size:11px;color:var(--mu);margin-top:8px">Segundos hasta disparo automático por restauración de 3.3V</p>
  </div>

  <div class="sgrid">
    <div class="sc"><div class="sico ic"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12.55a11 11 0 0114.08 0"/><path d="M1.42 9a16 16 0 0121.16 0"/><path d="M8.53 16.11a6 6 0 016.95 0"/><circle cx="12" cy="20" r="1" fill="currentColor"/></svg></div><div class="sv" id="dRSSI">—</div><div class="sl">WiFi · señal</div></div>
    <div class="sc"><div class="sico iv"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 10c0 7-9 13-9 13S3 17 3 10a9 9 0 0118 0z"/><circle cx="12" cy="10" r="3"/></svg></div><div class="sv" id="dIP">—</div><div class="sl">Dirección IP</div></div>
    <div class="sc"><div class="sico ig" id="dBtnIco"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg></div><div class="sv" id="dBtn">—</div><div class="sl">Botón GPIO2</div></div>
    <div class="sc"><div class="sico" id="dBotIco"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg></div><div class="sv" id="dBot">—</div><div class="sl">Bot Telegram</div></div>
    <div class="sc"><div class="sico" id="dAlmIco"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg></div><div class="sv" id="dAlm">—</div><div class="sl">Última alarma</div></div>
  </div>

  <div class="card">
    <div class="ctit">Información del Sistema</div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:14px;font-size:12px">
      <div><span style="color:var(--mu);font-size:10px">ADMINISTRADOR</span><br><strong id="dAdm">—</strong></div>
      <div><span style="color:var(--mu);font-size:10px">GRUPO EMERGENCIA</span><br><strong id="dGrp">—</strong></div>
      <div><span style="color:var(--mu);font-size:10px">RED WIFI</span><br><strong id="dSSID">—</strong></div>
      <div><span style="color:var(--mu);font-size:10px">TIEMPO ACTIVO</span><br><strong id="dUp">—</strong></div>
    </div>
    <div style="margin-top:16px">
      <button class="btn bp" onclick="doTest()">▶ ENVIAR PRUEBA</button>
    </div>
  </div>
</div>

<!-- DESPLEGAR / REPLEGAR -->
<div class="sec" id="s-deploy">
  <div class="ptit">Desplegar / Replegar</div>
  <div class="psub">Control del modo de activación por corte de 3.3V</div>

  <div class="card" id="deployCard">
    <div class="ctit">⚡ MODO ACTUAL</div>
    <div class="mode-banner safe" id="deployBanner" style="margin-bottom:20px">
      <div class="mode-icon" id="deployIcon">🟢</div>
      <div>
        <div class="mode-title" id="deployTitle">NO DESPLEGADO</div>
        <div class="mode-sub" id="deploySub">El dispositivo NO dispara alarma al encender.</div>
      </div>
    </div>

    <div class="ibox red" id="infoDesplegar">
      🚨 <strong>Al hacer clic en DESPLEGAR:</strong><br><br>
      • El sistema quedará en modo armado (guardado en EEPROM).<br>
      • La próxima vez que se restaure la alimentación 3.3V, el ESP-01 arrancará y disparará la alarma <strong>de inmediato</strong>.<br>
      • La primera alerta se enviará al grupo de emergencia (×4) y al administrador.<br>
      • Las alertas se repetirán cada 1 minuto <strong>solo al canal</strong> mientras el dispositivo esté encendido.<br><br>
      ⚠️ Asegúrate de tener WiFi, Bot Token y Chat IDs configurados antes de desplegar.
    </div>

    <div class="ibox grn" id="infoReplegar" style="display:none">
      🟢 <strong>Al hacer clic en REPLEGAR:</strong><br><br>
      • El sistema volverá a modo configuración (No Desplegado).<br>
      • El próximo encendido NO disparará alarma automática.<br>
      • El botón GPIO2 (hold 6s) seguirá funcionando como pánico manual.<br>
    </div>

    <div class="brow">
      <button class="btn bp" id="btnDesplegar" onclick="askDesplegar()">🔴 DESPLEGAR</button>
      <button class="btn bsafe" id="btnReplegar" onclick="doReplegar()" style="display:none">🟢 REPLEGAR</button>
    </div>
  </div>

  <div class="card">
    <div class="ctit">⚙️ CÓMO FUNCIONA LA ACTIVACIÓN POR 3.3V</div>
    <div class="ibox org">
      <strong>Circuito recomendado:</strong><br><br>
      <code style="color:var(--ac2)">3.3V_FUENTE ──[Reed/Interruptor/MOSFET]── VCC_ESP01</code><br><br>
      1. Cuando el interruptor/sensor <strong>abre</strong> → corta 3.3V → ESP-01 apagado (0 mA).<br>
      2. Cuando <strong>cierra</strong> (restaura alimentación) → ESP-01 arranca → cuenta 6s → ALARMA.<br><br>
      <strong>Opciones de disparo:</strong><br>
      • Reed switch en puerta/ventana<br>
      • PIR con relay de baja potencia<br>
      • MOSFET P-channel controlado por microcontrolador externo<br>
      • Interruptor manual de prueba
    </div>
  </div>
</div>

<!-- WIFI -->
<div class="sec" id="s-wifi">
  <div class="ptit">Red WiFi</div><div class="psub">Configura la red a la que conectará el dispositivo</div>
  <div class="card">
    <div class="ctit">Redes disponibles</div>
    <button class="btn bg" onclick="doScan()" id="scanBtn">↺ ESCANEAR</button>
    <div id="wlist" style="margin-top:12px;display:flex;flex-direction:column;gap:4px"></div>
  </div>
  <div class="card">
    <div class="ctit">Credenciales</div>
    <div class="ibox org">⚠️ En modo <strong>DESPLEGADO</strong> el sistema hace <strong>40 intentos</strong> antes de volver a modo configuración, para evitar falsos despliegues por pérdida temporal de señal.</div>
    <form onsubmit="svWifi(event)">
      <div class="fr"><label>SSID</label><input class="inp" type="text" id="wSSID" maxlength="31"></div>
      <div class="fr"><label>Password</label><input class="inp" type="password" id="wPass" maxlength="63"></div>
      <button class="btn bp" type="submit" id="wBtn">GUARDAR Y CONECTAR</button>
    </form>
  </div>
</div>

<!-- TELEGRAM -->
<div class="sec" id="s-tg">
  <div class="ptit">Telegram Bot</div><div class="psub">Token del bot y administrador</div>
  <div class="card">
    <div class="ctit">Token del Bot</div>
    <form onsubmit="svTok(event)">
      <div class="fr"><label>Bot Token</label><input class="inp" type="password" id="bTok" placeholder="123456789:ABC..." maxlength="63"></div>
      <button class="btn bp" type="submit">GUARDAR TOKEN</button>
    </form>
  </div>
  <div class="card">
    <div class="ctit">Administrador Principal</div>
    <div class="ibox org">💡 El administrador recibe notificaciones de arranque, reporte diario 18:00 y respuestas a comandos: /status /ip /pin /desplegar /replegar etc.</div>
    <form onsubmit="svAdm(event)">
      <div class="r2">
        <div class="fr"><label>Nombre</label><input class="inp" type="text" id="aNom" maxlength="31"></div>
        <div class="fr"><label>Chat ID</label><input class="inp" type="text" id="aID" maxlength="19"><div class="hint">Envía /getchatid al bot</div></div>
      </div>
      <button class="btn bp" type="submit">GUARDAR ADMINISTRADOR</button>
    </form>
  </div>
</div>

<!-- GRUPO -->
<div class="sec" id="s-grp">
  <div class="ptit">Grupo de Emergencia</div><div class="psub">Grupo Telegram que recibe las alarmas</div>
  <div class="card">
    <div class="ctit">Configurar Grupo</div>
    <div class="ibox red">🚨 Al activarse la alarma (restauración 3.3V), se envían <strong>4 mensajes de alerta</strong> a este grupo, más 1 al administrador. Las repeticiones son cada 1 minuto <strong>solo a este canal</strong>.
    <br><br><strong>¿Cómo obtener el Chat ID del grupo?</strong><br>
    1. Agrega el bot al grupo y envía un mensaje<br>
    2. Abre: <code style="color:var(--ac)">api.telegram.org/bot&lt;TOKEN&gt;/getUpdates</code><br>
    3. El <code>chat.id</code> es un número negativo (ej: <code>-1001234567890</code>)</div>
    <form onsubmit="svGrp(event)">
      <div class="fr"><label>Chat ID del Grupo</label><input class="inp" type="text" id="gID" placeholder="-1001234567890" maxlength="19"></div>
      <button class="btn bp" type="submit">GUARDAR GRUPO</button>
    </form>
  </div>
</div>

<!-- ACCESS POINT -->
<div class="sec" id="s-ap">
  <div class="ptit">Access Point</div><div class="psub">Punto de acceso cuando no hay red disponible</div>
  <div class="card">
    <div class="ctit">Credenciales del AP</div>
    <form onsubmit="svAP(event)">
      <div class="r2">
        <div class="fr"><label>Nombre (SSID)</label><input class="inp" type="text" id="apN" maxlength="31"></div>
        <div class="fr"><label>Password</label><input class="inp" type="password" id="apP" maxlength="31"><div class="hint">Mínimo 8 caracteres</div></div>
      </div>
      <button class="btn bp" type="submit">GUARDAR ACCESS POINT</button>
    </form>
  </div>
</div>

<!-- SISTEMA -->
<div class="sec" id="s-sys">
  <div class="ptit">Sistema</div><div class="psub">PIN del portal y acciones del dispositivo</div>
  <div class="card">
    <div class="ctit">PIN del Portal Web</div>
    <form onsubmit="svPin(event)" style="display:flex;align-items:flex-end;gap:12px;flex-wrap:wrap">
      <div class="fr" style="margin:0;flex:1;min-width:150px"><label>Nuevo PIN</label><input class="inp" type="password" id="nPin" placeholder="Mínimo 4 caracteres" maxlength="15"></div>
      <button class="btn bp" type="submit" style="margin-bottom:16px">CAMBIAR PIN</button>
    </form>
  </div>
  <div class="card">
    <div class="ctit">Acciones del Dispositivo</div>
    <div class="brow">
      <button class="btn bg" onclick="doReboot()">↺ REINICIAR ESP-01</button>
      <button class="btn bg" onclick="askSleep()" id="btnSleep">💤 PONER A DORMIR</button>
      <button class="btn bd" onclick="askReset()">✕ BORRAR CONFIGURACIÓN</button>
    </div>
  </div>
</div>

</main>

<div class="mbg" id="mbg"><div class="modal"><h3 id="mtit">⚠️ Confirmar</h3><p id="mtxt"></p><div class="brow"><button class="btn bd" id="mok">CONFIRMAR</button><button class="btn bg" onclick="closeMod()">CANCELAR</button></div></div></div>
<div class="ta" id="ta"></div>
<div id="overlay" onclick="closeSide()"></div>

<script>
const H=document.documentElement;
H.setAttribute('data-theme',localStorage.getItem('sh_theme')||'dark');
function tgTheme(){const t=H.getAttribute('data-theme')==='dark'?'light':'dark';H.setAttribute('data-theme',t);localStorage.setItem('sh_theme',t);}
function tgSide(){document.getElementById('side').classList.toggle('on');const o=document.getElementById('overlay');o.style.display=document.getElementById('side').classList.contains('on')?'block':'none';}
function closeSide(){document.getElementById('side').classList.remove('on');document.getElementById('overlay').style.display='none';}
function go(id,btn){
  document.querySelectorAll('.sec').forEach(s=>s.classList.remove('on'));
  document.querySelectorAll('.sbtn').forEach(b=>b.classList.remove('on'));
  document.getElementById('s-'+id).classList.add('on');
  btn.classList.add('on');
  if(window.innerWidth<768)closeSide();
  if(id==='dash'||id==='deploy')loadSt();
}
function toast(m,t){
  const a=document.getElementById('ta'),e=document.createElement('div');
  e.className='toast t'+t;
  e.innerHTML='<span>'+(t==='ok'?'✓':t==='er'?'✕':'!')+'</span><span>'+m+'</span>';
  a.appendChild(e);
  setTimeout(()=>{e.style.opacity='0';e.style.transform='translateX(30px)';e.style.transition='.3s';setTimeout(()=>e.remove(),300);},3500);
}
let mCb=null;
function askReset(){
  document.getElementById('mtit').textContent='⚠️ BORRAR CONFIGURACIÓN';
  document.getElementById('mtxt').textContent='Se borrarán TODOS los datos incluyendo el modo Desplegado. El dispositivo reiniciará con valores de fábrica.';
  mCb=()=>{post('/reset',{}).then(()=>{toast('Reseteando...','in');setTimeout(()=>location.reload(),4000);});};
  document.getElementById('mbg').classList.add('on');
  document.getElementById('mok').onclick=()=>{closeMod();mCb&&mCb();};
}
function askSleep(){
  document.getElementById('mtit').textContent='💤 MODEM SLEEP';
  document.getElementById('mtxt').textContent='El ESP-01 apagará el WiFi. El CPU sigue activo. Presiona GPIO2 para despertar. NOTA: Solo disponible en modo No Desplegado.';
  mCb=()=>{post('/force-sleep',{}).then(r=>{r==='OK'?toast('Entrando en reposo...','in'):toast('Error: '+r,'er');});};
  document.getElementById('mbg').classList.add('on');
  document.getElementById('mok').onclick=()=>{closeMod();mCb&&mCb();};
}
function askDesplegar(){
  document.getElementById('mtit').textContent='🔴 DESPLEGAR SISTEMA';
  document.getElementById('mtxt').textContent='El sistema quedará ARMADO. La próxima restauración de alimentación 3.3V disparará la alarma de inmediato. Las repeticiones serán solo al canal cada 1 minuto. ¿Confirmas?';
  mCb=()=>{
    post('/desplegar',{}).then(r=>{
      if(r==='OK'){toast('Sistema DESPLEGADO','ok');setTimeout(loadSt,800);}
      else if(r==='SIN_WIFI_O_BOT'){toast('ERROR: Sin WiFi o Bot Token','er');}
      else if(r==='SIN_CONFIG_TG'){toast('ERROR: Configura Bot Token y Chat ID primero','er');}
      else toast('Error: '+r,'er');
    });
  };
  document.getElementById('mbg').classList.add('on');
  document.getElementById('mok').onclick=()=>{closeMod();mCb&&mCb();};
}
function doReplegar(){
  post('/replegar',{}).then(r=>{
    if(r==='OK'){toast('Sistema REPLEGADO','ok');setTimeout(loadSt,800);}
    else toast('Error: '+r,'er');
  });
}
function closeMod(){document.getElementById('mbg').classList.remove('on');}
async function post(url,data){
  const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});
  return r.text();
}
let lastDesp=false;
async function loadSt(){
  try{
    const d=await(await fetch('/status-json')).json();
    // NAV
    const dot=document.getElementById('wdot'),lbl=document.getElementById('wlbl');
    if(d.apMode){dot.className='dot w';lbl.textContent='Modo AP';}
    else if(d.rssi>-65){dot.className='dot';lbl.textContent='WiFi '+d.rssi+'dBm';}
    else if(d.rssi>-80){dot.className='dot w';lbl.textContent='WiFi '+d.rssi+'dBm';}
    else{dot.className='dot e';lbl.textContent='Señal débil';}
    document.getElementById('iplbl').textContent=d.ip||'';
    document.getElementById('modeBadge').style.display=d.desplegado?'flex':'none';
    // DASHBOARD BANNER
    const banner=document.getElementById('modeBanner');
    document.getElementById('modeIcon').textContent=d.desplegado?(d.alarmActive?'🚨':'🔴'):'🟢';
    document.getElementById('modeTitle').textContent=d.desplegado?(d.alarmActive?'ALARMA ACTIVA':'DESPLEGADO — ARMADO'):'NO DESPLEGADO';
    document.getElementById('modeSub').textContent=d.desplegado?(d.alarmActive?'Enviando alertas cada 1 min (solo canal)...':'Sistema armado. Próximo encendido → alarma inmediata'):'Sistema en modo configuración. Seguro.';
    banner.className='mode-banner '+(d.desplegado?'armed':'safe');
    // COUNTDOWN
    const cdCard=document.getElementById('cdCard'),cdVal=document.getElementById('cdVal');
    if(d.autoCD&&d.cdSec>0){cdCard.style.display='block';cdVal.textContent=d.cdSec;}
    else{cdCard.style.display='none';}
    // STATS
    document.getElementById('dRSSI').textContent=d.apMode?'AP Mode':(d.rssi+'dBm');
    document.getElementById('dIP').textContent=d.ip||'—';
    const bi=document.getElementById('dBtnIco'),bv=document.getElementById('dBtn');
    bv.textContent=d.boton?'PRESIONADO':'Libre';bi.className='sico '+(d.boton?'ir':'ig');
    const oi=document.getElementById('dBotIco'),ov=document.getElementById('dBot');
    ov.textContent=d.botOk?'Conectado':'Sin token';oi.className='sico '+(d.botOk?'ic':'ir');
    document.getElementById('dAlm').textContent=d.lastAlarm||'—';
    document.getElementById('dAlmIco').className='sico '+(d.alarmActive?'ir':'ig');
    document.getElementById('dAdm').textContent=d.admin||'—';
    document.getElementById('dGrp').textContent=d.grupoOk?'✓ Configurado':'⚠ No configurado';
    document.getElementById('dSSID').textContent=d.ssid||'—';
    document.getElementById('dUp').textContent=uptime(d.uptime||0);
    // PRE-FILL INPUTS
    if(d.ssid)document.getElementById('wSSID').value=d.ssid;
    if(d.admin)document.getElementById('aNom').value=d.admin;
    if(d.admID)document.getElementById('aID').value=d.admID;
    if(d.grpID)document.getElementById('gID').value=d.grpID;
    if(d.apSSID)document.getElementById('apN').value=d.apSSID;
    // DEPLOY SECTION
    const depBanner=document.getElementById('deployBanner');
    document.getElementById('deployIcon').textContent=d.desplegado?'🔴':'🟢';
    document.getElementById('deployTitle').textContent=d.desplegado?'DESPLEGADO — ARMADO':'NO DESPLEGADO';
    document.getElementById('deploySub').textContent=d.desplegado?'La próxima restauración de 3.3V dispara alarma de inmediato':'El dispositivo NO dispara alarma al encender.';
    depBanner.className='mode-banner '+(d.desplegado?'armed':'safe');
    document.getElementById('infoDesplegar').style.display=d.desplegado?'none':'block';
    document.getElementById('infoReplegar').style.display=d.desplegado?'block':'none';
    document.getElementById('btnDesplegar').style.display=d.desplegado?'none':'inline-flex';
    document.getElementById('btnReplegar').style.display=d.desplegado?'inline-flex':'none';
    // SLEEP BTN
    document.getElementById('btnSleep').disabled=d.desplegado;
  }catch(e){}
}
function uptime(ms){const s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60),d=Math.floor(h/24);return d>0?d+'d '+(h%24)+'h':h>0?h+'h '+(m%60)+'m':m+'m '+(s%60)+'s';}
async function doScan(){
  const b=document.getElementById('scanBtn'),l=document.getElementById('wlist');
  b.disabled=true;b.textContent='...';
  l.innerHTML='<p style="color:var(--mu);font-size:12px">Buscando redes...</p>';
  try{
    const ns=await(await fetch('/scan-wifi')).json();
    l.innerHTML='';
    if(!ns.length){l.innerHTML='<p style="color:var(--mu);font-size:12px">Sin redes encontradas</p>';return;}
    ns.sort((a,b)=>b.rssi-a.rssi);
    ns.forEach(n=>{
      const lvl=n.rssi>-55?4:n.rssi>-65?3:n.rssi>-75?2:1;
      const bars=[5,8,11,14].map((h,i)=>`<div class="bar" style="height:${h}px;opacity:${i<lvl?1:.25}"></div>`).join('');
      const el=document.createElement('div');el.className='wi';
      el.innerHTML=`<div class="bars">${bars}</div><span class="wss">${n.ssid}</span><span class="wlk">${n.secured?'🔒':'🔓'} ${n.rssi}dBm</span>`;
      el.onclick=()=>{document.getElementById('wSSID').value=n.ssid;document.querySelectorAll('.wi').forEach(x=>x.classList.remove('sel'));el.classList.add('sel');};
      l.appendChild(el);
    });
  }catch(e){l.innerHTML='<p style="color:var(--er);font-size:12px">Error al escanear</p>';}
  b.disabled=false;b.textContent='↺ ESCANEAR';
}
async function svWifi(e){
  e.preventDefault();const btn=e.target.querySelector('[type=submit]');
  const o=btn.innerHTML;btn.disabled=true;btn.innerHTML='<div class="spin"></div>';
  const r=await post('/save-wifi',{ssid:document.getElementById('wSSID').value,password:document.getElementById('wPass').value});
  btn.disabled=false;btn.innerHTML=o;
  if(r==='OK')toast('WiFi guardado. Reconectando...','ok');
  else if(r==='FALLBACK')toast('No se pudo conectar. Red anterior restaurada.','er');
  else toast('Error al guardar','er');
}
async function svTok(e){e.preventDefault();const r=await post('/save-token',{token:document.getElementById('bTok').value});r==='OK'?toast('Token guardado. Reiniciando...','ok'):toast('Error','er');}
async function svAdm(e){e.preventDefault();const r=await post('/save-admin',{nombre:document.getElementById('aNom').value,chatID:document.getElementById('aID').value});r==='OK'?toast('Administrador guardado','ok'):toast('Error','er');}
async function svGrp(e){e.preventDefault();const r=await post('/save-grupo',{chatID:document.getElementById('gID').value});r==='OK'?toast('Grupo guardado','ok'):toast('Error','er');}
async function svAP(e){
  e.preventDefault();const p=document.getElementById('apP').value;
  if(p.length>0&&p.length<8){toast('Password mínimo 8 caracteres','er');return;}
  const r=await post('/save-ap',{ssid:document.getElementById('apN').value,password:p});
  r==='OK'?toast('Access Point guardado','ok'):toast('Error: '+r,'er');
}
async function svPin(e){
  e.preventDefault();const p=document.getElementById('nPin').value;
  if(p.length<4){toast('PIN mínimo 4 caracteres','er');return;}
  const r=await post('/save-pin',{pin:p});r==='OK'?toast('PIN actualizado','ok'):toast('Error','er');
  document.getElementById('nPin').value='';
}
async function doTest(){const r=await post('/test',{});r==='OK'?toast('Mensaje enviado al administrador','ok'):toast('Error: '+r,'er');}
async function doReboot(){await post('/reboot',{});toast('Reiniciando ESP-01...','in');}
loadSt();setInterval(loadSt,3000);
</script>
</body></html>
)rawliteral";
  webSrv.send(200, "text/html", PAGE);
}
