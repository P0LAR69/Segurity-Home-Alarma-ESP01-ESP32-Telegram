/**
 * ╔═══════════════════════════════════════════════════════════╗
 * ║   SEGURITY HOME  ·  Sistema de Alarma ESP32 + Telegram   ║
 * ║   Autor: Heraldo Rosero                                   ║
 * ║   Deep Sleep añadido: 2026                                ║
 * ╚═══════════════════════════════════════════════════════════╝
 *
 * LIBRERÍAS REQUERIDAS (Gestor de Librerías de Arduino):
 *   • UniversalTelegramBot  by Brian Lough   v1.3.0+
 *   • ArduinoJson           by Benoit Blanchon v6.x
 *
 * Librerías nativas del ESP32 (NO instalar):
 *   WiFi · WebServer · DNSServer · WiFiClientSecure · EEPROM · time.h · esp_sleep.h
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <time.h>
#include <esp_sleep.h>

// ── Pines ────────────────────────────────────────────────────────
#define PIN_BTN    4      // GPIO4  – Botón de pánico (INPUT_PULLUP → GND)
#define PIN_LED    2      // GPIO2  – LED integrado (activo en HIGH)

// ── EEPROM ───────────────────────────────────────────────────────
#define EEPROM_SZ  512
#define MAGIC_V    0xCA1D   // Valor diferente al original → fuerza re-init limpio

// ── NTP / Zona horaria ────────────────────────────────────────────
#define NTP_HOST   "pool.ntp.org"
#define UTC_OFFS   -18000   // UTC-5  Colombia / Ecuador / Perú
#define DST_OFFS   0

// ── Captive Portal ────────────────────────────────────────────────
#define DNS_PORT   53

// ── Tiempos ───────────────────────────────────────────────────────
#define HOLD_MS          6000UL      // Mantener botón 6s → dispara alarma
#define TG_POLL_MS       1500UL      // Intervalo de polling Telegram
#define SES_TOUT         1800000UL   // Timeout de sesión web: 30 min
#define SLEEP_TIMEOUT_MS 1800000UL   // Deep Sleep tras 30 min sin actividad

// ═══════════════════════════════════════════════════════════════════
//  ESTRUCTURA DE CONFIGURACIÓN  (todo guardado en EEPROM)
// ═══════════════════════════════════════════════════════════════════
struct Config {
  uint16_t magic;
  char wSSID[32];     // SSID de la red WiFi local
  char wPass[64];     // Password de la red WiFi local
  char botTok[64];    // Token del bot de Telegram
  char admNom[32];    // Nombre del administrador
  char admID[20];     // Chat-ID del administrador (recibe reportes y comandos)
  char grpID[20];     // Chat-ID del grupo Telegram (recibe alarmas de pánico)
  char apSSID[32];    // SSID del Access Point (modo AP / Captive Portal)
  char apPass[32];    // Password del Access Point
  char webPin[16];    // PIN de acceso al portal web
};

Config cfg;

// ═══════════════════════════════════════════════════════════════════
//  OBJETOS GLOBALES
// ═══════════════════════════════════════════════════════════════════
WebServer             webSrv(80);
DNSServer             dnsSrv;
WiFiClientSecure      tls;
UniversalTelegramBot *bot = nullptr;

bool apMode     = false;   // true → estamos en modo Access Point
bool ntpReady   = false;
bool rptToday   = false;
int  rptDay     = -1;
unsigned long tgT   = 0;
unsigned long bootT = 0;

// ── Botón de pánico ──────────────────────────────────────────────
bool btnCur   = HIGH, btnPrev = HIGH;
unsigned long btnMs   = 0;
bool alarmSent = false;
int  lastSec   = -1;

// ── Sesión web ───────────────────────────────────────────────────
String        sesTok = "";
unsigned long sesMs  = 0;

// ── Deep Sleep ───────────────────────────────────────────────────
// lastActivityMs se reinicia en: request web autenticado, botón presionado,
// mensaje Telegram recibido. Al superar SLEEP_TIMEOUT_MS → Deep Sleep.
unsigned long lastActivityMs  = 0;
// panicWakeActive: el dispositivo despertó por EXT0 (GPIO4); cuenta 6 s
// desde el final del setup y dispara la alarma sin importar el estado actual
// del botón (el usuario pudo haberlo soltado mientras el WiFi conectaba).
bool          panicWakeActive = false;

// ═══════════════════════════════════════════════════════════════════
//  PROTOTIPOS
// ═══════════════════════════════════════════════════════════════════
void cfgSave();  void cfgLoad();  void cfgDefault();
void wifiConnect();  bool wifiTry(const char*, const char*, int);
void apStart();  void ledBlink(int);
void btnCheck(); void alarmFire();
void tgProcess(int);
void rptSend();  void rptCheck();
void webSetup();
String sesGen(); bool sesOk(String); bool httpAuth();
void hRoot();    void hLoginGET(); void hLoginPOST(); void hLogout();
void hStatus();  void hScanWifi();
void hSaveWifi();  void hSaveTok();   void hSaveAdmin();
void hSaveGrupo(); void hSaveAP();    void hSavePin();
void hTest();    void hReset();     void hReboot();
void hForceSleep();
void hNotFound();
void enterDeepSleep();

// ═══════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════
void setup() {
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n===================================================="));
  Serial.println(F("   SEGURITY HOME  ·  Heraldo Rosero"));
  Serial.println(F("===================================================="));

  // ── Detectar causa de despertar del Deep Sleep ──────────────────
  // Debe leerse lo antes posible; el registro RTC se mantiene al salir del
  // Deep Sleep pero se borra en un reset de hardware o power-on normal.
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println(F(">>> DESPERTADO POR BOTÓN DE PÁNICO (EXT0 Deep Sleep) <<<"));
    panicWakeActive = true;
  } else if (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.println(F("Arranque normal (sin Deep Sleep previo)."));
  } else {
    Serial.printf("Despertar desde Deep Sleep (causa: %d)\n", (int)wakeCause);
  }

  EEPROM.begin(EEPROM_SZ);
  cfgLoad();
  if (cfg.magic != MAGIC_V) {
    Serial.println(F("Primera ejecución: cargando configuración por defecto..."));
    cfgDefault();
  }

  wifiConnect();

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(PIN_LED, HIGH);
    apMode = false;

    configTime(UTC_OFFS, DST_OFFS, NTP_HOST);
    struct tm ti;
    if (getLocalTime(&ti, 6000)) {
      ntpReady = true;
      char buf[32]; strftime(buf, 32, "%H:%M:%S %d/%m/%Y", &ti);
      Serial.println("NTP sincronizado: " + String(buf));
    } else {
      Serial.println(F("NTP: sin sincronización (reintentará en loop)"));
    }

    if (strlen(cfg.botTok) > 10) {
      tls.setInsecure();
      bot = new UniversalTelegramBot(cfg.botTok, tls);

      // Descartar mensajes viejos antes del arranque
      int p = bot->getUpdates(bot->last_message_received + 1);
      while (p) p = bot->getUpdates(bot->last_message_received + 1);
      Serial.println(F("Mensajes anteriores descartados. Bot listo."));

      if (strlen(cfg.admID) > 3) {
        String m = panicWakeActive
          ? "🚨 *¡Despertado por BOTÓN DE PÁNICO!*\n\n"
            "El sistema salió del Deep Sleep por presión del botón.\n"
            "⏳ Activando alarma en 6 segundos...\n\n"
            "📶 WiFi: `" + String(cfg.wSSID) + "`\n"
            "🌐 IP: `" + WiFi.localIP().toString() + "`"
          : "🔒 *Segurity Home Iniciado*\n\n"
            "📶 WiFi: `" + String(cfg.wSSID) + "`\n"
            "🌐 IP: `" + WiFi.localIP().toString() + "`\n"
            "🖥 Portal: http://" + WiFi.localIP().toString() + "\n"
            "🚨 Grupo alarma: " + (strlen(cfg.grpID) > 3 ? "✅ Configurado\n" : "⚠️ No configurado\n") +
            "✅ Sistema operativo\n"
            "💤 Deep Sleep en: 30 minutos sin actividad";
        bot->sendMessage(cfg.admID, m, "Markdown");
        Serial.println("Mensaje de arranque enviado a: " + String(cfg.admNom));
      }
    }
    Serial.println("Portal web → http://" + WiFi.localIP().toString());

  } else {
    Serial.println(F("WiFi no disponible → iniciando modo AP (Captive Portal)"));
    apStart();
  }

  bootT = millis();
  webSetup();
  webSrv.begin();
  Serial.println(F("Servidor web iniciado."));
  btnPrev = digitalRead(PIN_BTN);

  // ── Inicializar temporizador de Deep Sleep ───────────────────────
  lastActivityMs = millis();
  if (panicWakeActive) {
    // Iniciar cuenta regresiva de alarma desde que el sistema está operativo.
    // El usuario pudo haber soltado el botón mientras el WiFi conectaba, por
    // eso no dependemos del estado actual del pin — disparamos siempre.
    btnMs   = millis();
    btnPrev = LOW;
    Serial.println(F(">>> Cuenta regresiva de alarma de pánico iniciada (6 s) <<<"));
  }
  Serial.printf("Deep Sleep en: %.0f minutos sin actividad.\n", SLEEP_TIMEOUT_MS / 60000.0);
}

// ═══════════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL
// ═══════════════════════════════════════════════════════════════════
void loop() {
  if (apMode) {
    dnsSrv.processNextRequest();   // Redirige DNS para captive portal
  } else {
    // Reconexión WiFi si se pierde
    if (WiFi.status() != WL_CONNECTED) {
      digitalWrite(PIN_LED, LOW);
      Serial.println(F("WiFi perdido. Reconectando..."));
      wifiConnect();
      if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(PIN_LED, HIGH);
        if (!ntpReady) configTime(UTC_OFFS, DST_OFFS, NTP_HOST);
      } else {
        apStart();   // Si no reconecta → modo AP
      }
    }

    rptCheck();   // Verificar reporte diario 18:00

    // Polling de mensajes Telegram
    if (bot && millis() - tgT > TG_POLL_MS) {
      int n = bot->getUpdates(bot->last_message_received + 1);
      while (n) { tgProcess(n); n = bot->getUpdates(bot->last_message_received + 1); }
      tgT = millis();
    }
  }

  webSrv.handleClient();
  btnCheck();
  delay(30);

  // ── Temporizador de Deep Sleep (30 min sin actividad) ───────────
  if (millis() - lastActivityMs > SLEEP_TIMEOUT_MS) {
    Serial.println(F("30 minutos sin actividad → entrando en Deep Sleep."));
    enterDeepSleep();
  }
}

// ═══════════════════════════════════════════════════════════════════
//  EEPROM
// ═══════════════════════════════════════════════════════════════════
void cfgSave() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println(F("Configuración guardada en EEPROM."));
}

void cfgLoad() {
  EEPROM.get(0, cfg);
  if (cfg.magic == MAGIC_V) {
    Serial.println(F("Configuración cargada desde EEPROM."));
    // Validar webPin
    int len = strnlen(cfg.webPin, 16);
    bool valid = (len >= 1 && len < 16);
    for (int i = 0; valid && i < len; i++)
      if ((byte)cfg.webPin[i] < 32 || (byte)cfg.webPin[i] > 126) valid = false;
    if (!valid) { strcpy(cfg.webPin, "1234"); cfgSave(); }
  }
}

void cfgDefault() {
  cfg.magic = MAGIC_V;
  strcpy(cfg.wSSID,   "MiRedWiFi");
  strcpy(cfg.wPass,   "");
  strcpy(cfg.botTok,  "INGRESA_TOKEN_AQUI");
  strcpy(cfg.admNom,  "Administrador");
  strcpy(cfg.admID,   "INGRESA_CHATID");
  strcpy(cfg.grpID,   "");
  strcpy(cfg.apSSID,  "Segurity Home");
  strcpy(cfg.apPass,  "00000000");
  strcpy(cfg.webPin,  "1234");
  cfgSave();
}

// ═══════════════════════════════════════════════════════════════════
//  WIFI  ·  ACCESS POINT  ·  LED
// ═══════════════════════════════════════════════════════════════════
bool wifiTry(const char* ssid, const char* pass, int tries) {
  for (int t = 1; t <= tries; t++) {
    Serial.printf("  Intento %d/%d → %s\n", t, tries, ssid);
    WiFi.disconnect(true); delay(300);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("  Conectado. IP: " + WiFi.localIP().toString());
      return true;
    }
    Serial.println(F("  Intento fallido."));
  }
  return false;
}

void wifiConnect() {
  Serial.println("Conectando a: " + String(cfg.wSSID));
  wifiTry(cfg.wSSID, cfg.wPass, 3);
}

void apStart() {
  if (apMode) return;   // Ya está en modo AP
  WiFi.disconnect(true); delay(300);
  WiFi.mode(WIFI_AP);

  const char* apN = (strlen(cfg.apSSID) > 0) ? cfg.apSSID : "Segurity Home";
  const char* apP = (strlen(cfg.apPass) >= 8) ? cfg.apPass : "00000000";
  WiFi.softAP(apN, apP);
  delay(500);

  // DNS hijacking: todas las peticiones DNS → 192.168.4.1
  dnsSrv.start(DNS_PORT, "*", WiFi.softAPIP());

  apMode = true;
  digitalWrite(PIN_LED, LOW);
  Serial.println("Modo AP activo: " + String(apN));
  Serial.println("IP del AP: " + WiFi.softAPIP().toString());
  Serial.println(F("Captive Portal activado."));
}

void ledBlink(int n) {
  for (int i = 0; i < n; i++) {
    digitalWrite(PIN_LED, HIGH); delay(150);
    digitalWrite(PIN_LED, LOW);  delay(150);
  }
  if (!apMode && WiFi.status() == WL_CONNECTED) digitalWrite(PIN_LED, HIGH);
}

// ═══════════════════════════════════════════════════════════════════
//  BOTÓN DE PÁNICO
// ═══════════════════════════════════════════════════════════════════
void btnCheck() {
  btnCur = digitalRead(PIN_BTN);

  // Cualquier pulsación del botón reinicia el temporizador de Deep Sleep
  if (btnCur == LOW) lastActivityMs = millis();

  // ── Modo despertar por pánico ───────────────────────────────────
  // El dispositivo despertó por EXT0 (GPIO4 LOW). Se dispara la alarma 6 s
  // después de que el setup terminó, sin importar si el botón sigue presionado.
  if (panicWakeActive) {
    unsigned long elapsed = millis() - btnMs;
    int sec = elapsed / 1000;
    if (sec != lastSec && sec >= 1) {
      lastSec = sec;
      Serial.printf("  [PÁNICO-WAKE] %d / 6 s\n", sec);
    }
    if (elapsed >= HOLD_MS) {
      panicWakeActive = false;
      alarmFire();
      btnMs = millis(); lastSec = -1;
    }
    btnPrev = btnCur;
    return;
  }

  // ── Lógica normal del botón ─────────────────────────────────────
  if (btnCur != btnPrev) {
    if (btnCur == LOW) {
      btnMs     = millis();
      alarmSent = false;
      lastSec   = -1;
      Serial.println(F("BOTÓN PRESIONADO — contando 6 segundos..."));
    } else {
      if (!alarmSent) Serial.println(F("Botón liberado antes del tiempo."));
      btnMs = 0; alarmSent = false; lastSec = -1;
    }
    btnPrev = btnCur;
  }

  if (btnCur == LOW && !alarmSent) {
    unsigned long elapsed = millis() - btnMs;
    int sec = elapsed / 1000;
    if (sec != lastSec && sec >= 1) { lastSec = sec; Serial.printf("  %d / 6 s\n", sec); }
    if (elapsed >= HOLD_MS) {
      alarmFire();
      // Permite reactivar si se mantiene presionado
      btnMs = millis(); alarmSent = false; lastSec = -1;
    }
  }
}

void alarmFire() {
  alarmSent = true;
  Serial.println(F("======================================"));
  Serial.println(F("  !!!  ALARMA DE PÁNICO ACTIVADA  !!!"));
  Serial.println(F("======================================"));
  ledBlink(5);

  if (!bot || WiFi.status() != WL_CONNECTED) {
    Serial.println(F("ERROR: Bot o WiFi no disponibles. Alarma NO enviada."));
    return;
  }

  bool hayDestino = (strlen(cfg.grpID) > 3) || (strlen(cfg.admID) > 3);
  if (!hayDestino) {
    Serial.println(F("AVISO: No hay grupo ni administrador configurado."));
    return;
  }

  struct tm ti;
  char hora[20] = "—";
  if (getLocalTime(&ti, 500)) strftime(hora, 20, "%H:%M:%S", &ti);

  String msg  = "🚨 *¡ALARMA DE PÁNICO!* 🚨\n\n";
  msg += "📍 *Casa de Heraldo Rosero*\n";
  msg += "⏰ Hora: `" + String(hora) + "`\n";
  msg += "⏱ Botón mantenido 6 segundos\n\n";
  msg += "‼️ *SE REQUIERE AYUDA URGENTE* ‼️";

  // Enviar al grupo de emergencia (4 veces para garantizar entrega)
  if (strlen(cfg.grpID) > 3) {
    Serial.println("Enviando alarma al grupo: " + String(cfg.grpID));
    for (int i = 0; i < 4; i++) {
      bool ok = bot->sendMessage(cfg.grpID, msg, "Markdown");
      Serial.printf("  Envío %d: %s\n", i+1, ok ? "OK" : "FALLO");
      delay(200);
    }
  }

  // Notificar también al administrador
  if (strlen(cfg.admID) > 3) {
    String admMsg = msg + "\n\n_[Notificación al administrador]_";
    bot->sendMessage(cfg.admID, admMsg, "Markdown");
    Serial.println("Notificación enviada al administrador.");
  }
}

// ═══════════════════════════════════════════════════════════════════
//  MENSAJES TELEGRAM ENTRANTES
// ═══════════════════════════════════════════════════════════════════
void tgProcess(int n) {
  for (int i = 0; i < n; i++) {
    String cid  = String(bot->messages[i].chat_id); cid.trim();
    String txt  = bot->messages[i].text;            txt.trim();
    String from = bot->messages[i].from_name;

    // Quitar sufijo @bot de comandos en grupos
    int at = txt.indexOf('@');
    if (at != -1) txt = txt.substring(0, at);
    txt.trim();

    Serial.println("TG | " + from + " [" + cid + "] → " + txt);

    // Solo el administrador puede enviar comandos
    String admCID = String(cfg.admID); admCID.trim();
    if (cid != admCID) {
      bot->sendMessage(cid, "🔒 Acceso denegado.\n\n🆔 Tu Chat ID: `" + cid + "`", "Markdown");
      continue;
    }

    // Cualquier mensaje del admin reinicia el temporizador de Deep Sleep
    lastActivityMs = millis();

    if (txt == "/start") {
      String s = "👋 Hola *" + String(cfg.admNom) + "*\n\n";
      s += "🛡 *Segurity Home*\n";
      s += "Casa de Heraldo Rosero\n\n";
      s += "Usa /help para ver los comandos disponibles.";
      bot->sendMessage(cid, s, "Markdown");

    } else if (txt == "/status" || txt == "/estado") {
      unsigned long sleepInSec = 0;
      unsigned long elapsed = millis() - lastActivityMs;
      if (elapsed < SLEEP_TIMEOUT_MS)
        sleepInSec = (SLEEP_TIMEOUT_MS - elapsed) / 1000;

      String s = "📊 *Estado del Sistema*\n\n";
      s += "🔘 Botón: " + String(btnCur == LOW ? "⚠️ PRESIONADO" : "✅ Libre") + "\n";
      s += "📶 WiFi: " + String(WiFi.RSSI()) + " dBm\n";
      s += "🌐 IP: `" + WiFi.localIP().toString() + "`\n";
      s += "🖥 Portal: http://" + WiFi.localIP().toString() + "\n";
      s += "🚨 Grupo: " + String(strlen(cfg.grpID)>3 ? "✅ Config." : "⚠️ Sin configurar") + "\n";
      s += "💤 Duerme en: " + String(sleepInSec / 60) + "m " + String(sleepInSec % 60) + "s\n\n";
      s += "✅ Sistema operativo";
      bot->sendMessage(cid, s, "Markdown");

    } else if (txt == "/ip") {
      String s = "🌐 *IP del Sistema*\n\n`" + WiFi.localIP().toString() + "`\n\n";
      s += "http://" + WiFi.localIP().toString();
      bot->sendMessage(cid, s, "Markdown");

    } else if (txt == "/pin") {
      String s = "🔑 PIN del portal web:\n\n`" + String(cfg.webPin) + "`\n\n";
      s += "http://" + WiFi.localIP().toString();
      bot->sendMessage(cid, s, "Markdown");

    } else if (txt == "/test") {
      bot->sendMessage(cid, "✅ Sistema funcionando correctamente.", "");

    } else if (txt == "/getchatid") {
      bot->sendMessage(cid, "🆔 Tu Chat ID:\n\n`" + cid + "`", "Markdown");

    } else if (txt == "/dormir") {
      // Poner el dispositivo en Deep Sleep desde Telegram
      bot->sendMessage(cid,
        "💤 *Entrando en Deep Sleep...*\n\n"
        "Presiona el _botón de pánico_ para despertar.\n"
        "Al despertar, la alarma se activa en 6 segundos.",
        "Markdown");
      bot->getUpdates(bot->last_message_received + 1);
      delay(1500);
      enterDeepSleep();

    } else if (txt == "/reiniciar") {
      bot->sendMessage(cid, "🔄 *Reiniciando ESP32...*\nVuelve en unos segundos.", "Markdown");
      bot->getUpdates(bot->last_message_received + 1);
      delay(1500); ESP.restart();

    } else if (txt == "/help") {
      String s = "📋 *Comandos disponibles:*\n\n";
      s += "/status — Estado del sistema\n";
      s += "/ip — IP y enlace al portal\n";
      s += "/pin — PIN del portal web\n";
      s += "/test — Prueba de conectividad\n";
      s += "/dormir — Poner en Deep Sleep\n";
      s += "/reiniciar — Reiniciar el ESP32\n";
      s += "/getchatid — Tu Chat ID\n";
      s += "/help — Esta ayuda";
      bot->sendMessage(cid, s, "Markdown");

    } else {
      bot->sendMessage(cid, "❓ Comando no reconocido.\nUsa /help para ver los disponibles.", "");
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
//  REPORTE DIARIO  (18:00 hs)
// ═══════════════════════════════════════════════════════════════════
void rptSend() {
  if (!bot || WiFi.status() != WL_CONNECTED) return;
  if (strlen(cfg.admID) < 4) return;

  struct tm ti;
  if (!getLocalTime(&ti)) return;
  char dt[40]; strftime(dt, 40, "%d/%m/%Y %H:%M", &ti);

  String m = "📅 *Reporte Diario — 18:00 hs*\n\n";
  m += "🗓 Fecha: `" + String(dt) + "`\n\n";
  m += "🔘 Botón: " + String(btnCur == LOW ? "⚠️ PRESIONADO" : "✅ Libre") + "\n";
  m += "📶 WiFi: " + String(WiFi.RSSI()) + " dBm\n";
  m += "🌐 IP: `" + WiFi.localIP().toString() + "`\n";
  m += "🚨 Grupo alarma: " + String(strlen(cfg.grpID)>3 ? "✅ Config." : "⚠️ Sin configurar") + "\n\n";
  m += "🛡 *Sistema operativo y monitoreando.*";

  bot->sendMessage(cfg.admID, m, "Markdown");
  Serial.println(F("Reporte diario enviado."));
}

void rptCheck() {
  if (!ntpReady) {
    struct tm ti;
    if (getLocalTime(&ti, 100)) ntpReady = true; else return;
  }
  struct tm ti;
  if (!getLocalTime(&ti, 100)) return;
  if (ti.tm_hour == 18 && ti.tm_min == 0 && rptDay != ti.tm_yday) {
    rptDay = ti.tm_yday; rptToday = true;
    rptSend();
  }
}

// ═══════════════════════════════════════════════════════════════════
//  SESIÓN WEB
// ═══════════════════════════════════════════════════════════════════
String sesGen() { return String(millis()) + String(esp_random()); }

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
  if (sesOk(tok)) {
    lastActivityMs = millis();   // Cualquier request autenticado reinicia el timer
    return true;
  }
  return false;
}

// ═══════════════════════════════════════════════════════════════════
//  DEEP SLEEP
// ═══════════════════════════════════════════════════════════════════
void enterDeepSleep() {
  Serial.println(F("\n>>> Entrando en Deep Sleep <<<"));
  Serial.println(F("    Fuente de despertar: GPIO4 (botón de pánico, LOW)"));

  if (bot && WiFi.status() == WL_CONNECTED && strlen(cfg.admID) > 3) {
    bot->sendMessage(cfg.admID,
      "💤 *Sistema en reposo (Deep Sleep)*\n\n"
      "Modo de bajo consumo activado.\n"
      "Presiona el _botón de pánico_ para despertar.\n"
      "Al despertar la alarma se activa en 6 segundos.",
      "Markdown");
    delay(600);
  }

  digitalWrite(PIN_LED, LOW);
  WiFi.disconnect(true);
  delay(300);

  // GPIO4 LOW activa el despertar (botón presionado → GND)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);
  Serial.println(F("Deep Sleep activo. Presiona el botón de pánico para despertar."));
  Serial.flush();
  esp_deep_sleep_start();
  // La ejecución no continúa desde aquí; el siguiente boot corre setup() de nuevo
}

// ═══════════════════════════════════════════════════════════════════
//  CONFIGURACIÓN DEL SERVIDOR WEB
// ═══════════════════════════════════════════════════════════════════
void webSetup() {
  const char* hdrs[] = {"Cookie"};
  webSrv.collectHeaders(hdrs, 1);

  webSrv.on("/",             HTTP_GET,  hRoot);
  webSrv.on("/login",        HTTP_GET,  hLoginGET);
  webSrv.on("/login",        HTTP_POST, hLoginPOST);
  webSrv.on("/logout",       HTTP_POST, hLogout);
  webSrv.on("/status-json",  HTTP_GET,  hStatus);
  webSrv.on("/scan-wifi",    HTTP_GET,  hScanWifi);
  webSrv.on("/save-wifi",    HTTP_POST, hSaveWifi);
  webSrv.on("/save-token",   HTTP_POST, hSaveTok);
  webSrv.on("/save-admin",   HTTP_POST, hSaveAdmin);
  webSrv.on("/save-grupo",   HTTP_POST, hSaveGrupo);
  webSrv.on("/save-ap",      HTTP_POST, hSaveAP);
  webSrv.on("/save-pin",     HTTP_POST, hSavePin);
  webSrv.on("/test",         HTTP_POST, hTest);
  webSrv.on("/reset",        HTTP_POST, hReset);
  webSrv.on("/reboot",       HTTP_POST, hReboot);
  webSrv.on("/force-sleep",  HTTP_POST, hForceSleep);

  // Rutas especiales del captive portal (iOS, Android, Windows)
  webSrv.on("/generate_204",         HTTP_GET, []() { webSrv.sendHeader("Location", "/"); webSrv.send(302); });
  webSrv.on("/connecttest.txt",      HTTP_GET, []() { webSrv.sendHeader("Location", "/"); webSrv.send(302); });
  webSrv.on("/hotspot-detect.html",  HTTP_GET, []() { webSrv.sendHeader("Location", "/"); webSrv.send(302); });
  webSrv.on("/fwlink",               HTTP_GET, []() { webSrv.sendHeader("Location", "/"); webSrv.send(302); });
  webSrv.onNotFound(hNotFound);
}

// ── Captive portal: redirige cualquier URL desconocida a / ────────
void hNotFound() {
  webSrv.sendHeader("Location", "/");
  webSrv.send(302, "text/plain", "Redirecting...");
}

// ── Status JSON ───────────────────────────────────────────────────
void hStatus() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }

  unsigned long actElapsed = millis() - lastActivityMs;
  unsigned long sleepIn    = (actElapsed < SLEEP_TIMEOUT_MS)
                             ? (SLEEP_TIMEOUT_MS - actElapsed) : 0;

  String j = "{";
  j += "\"ssid\":\""    + String(cfg.wSSID)  + "\",";
  j += "\"rssi\":"      + String(apMode ? 0 : WiFi.RSSI()) + ",";
  j += "\"ip\":\""      + (apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  j += "\"boton\":"     + String(btnCur == LOW ? "true" : "false") + ",";
  j += "\"botOk\":"     + String(bot != nullptr ? "true" : "false") + ",";
  j += "\"admin\":\""   + String(cfg.admNom) + "\",";
  j += "\"admID\":\""   + String(cfg.admID)  + "\",";
  j += "\"grpID\":\""   + String(cfg.grpID)  + "\",";
  j += "\"grupoOk\":"   + String(strlen(cfg.grpID) > 3 ? "true" : "false") + ",";
  j += "\"apSSID\":\""  + String(cfg.apSSID) + "\",";
  j += "\"apMode\":"    + String(apMode ? "true" : "false") + ",";
  j += "\"uptime\":"    + String(millis() - bootT) + ",";
  j += "\"sleepIn\":"   + String(sleepIn / 1000) + ",";   // segundos hasta Deep Sleep
  j += "\"panicWake\":" + String(panicWakeActive ? "true" : "false");
  j += "}";
  webSrv.send(200, "application/json", j);
}

// ── Scan WiFi ─────────────────────────────────────────────────────
void hScanWifi() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) j += ",";
    j += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    j += "\"rssi\":"    + String(WiFi.RSSI(i)) + ",";
    j += "\"secured\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    delay(5);
  }
  j += "]"; WiFi.scanDelete();
  webSrv.send(200, "application/json", j);
}

// ── Save WiFi ─────────────────────────────────────────────────────
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

// ── Save Token ────────────────────────────────────────────────────
void hSaveTok() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  if (!webSrv.hasArg("token")) { webSrv.send(400, "text/plain", "MISSING"); return; }
  webSrv.arg("token").toCharArray(cfg.botTok, 64);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
  delay(1000); ESP.restart();
}

// ── Save Admin ────────────────────────────────────────────────────
void hSaveAdmin() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  webSrv.arg("nombre").toCharArray(cfg.admNom, 32);
  webSrv.arg("chatID").toCharArray(cfg.admID,  20);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
}

// ── Save Grupo ────────────────────────────────────────────────────
void hSaveGrupo() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  webSrv.arg("chatID").toCharArray(cfg.grpID, 20);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
}

// ── Save AP ───────────────────────────────────────────────────────
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

// ── Save PIN ──────────────────────────────────────────────────────
void hSavePin() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  String p = webSrv.arg("pin"); p.trim();
  if (p.length() < 4) { webSrv.send(400, "text/plain", "TOO_SHORT"); return; }
  p.toCharArray(cfg.webPin, 16);
  cfgSave();
  webSrv.send(200, "text/plain", "OK");
}

// ── Test ──────────────────────────────────────────────────────────
void hTest() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  if (!bot || WiFi.status() != WL_CONNECTED) { webSrv.send(503, "text/plain", "UNAVAILABLE"); return; }
  if (strlen(cfg.admID) < 4) { webSrv.send(400, "text/plain", "NO_ADMIN"); return; }

  String m = "🔒 *Prueba del Sistema*\n\n";
  m += "✅ Conexión verificada\n";
  m += "📶 WiFi: " + String(WiFi.RSSI()) + " dBm\n";
  m += "🌐 IP: `" + WiFi.localIP().toString() + "`\n";
  m += "🚨 Grupo: " + String(strlen(cfg.grpID)>3 ? "✅ Config." : "⚠️ Sin configurar") + "\n\n";
  m += "🛡 *Sistema armado y activo.*";
  bool ok = bot->sendMessage(cfg.admID, m, "Markdown");
  webSrv.send(200, "text/plain", ok ? "OK" : "ERROR");
}

// ── Reset fábrica ─────────────────────────────────────────────────
void hReset() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  for (int i = 0; i < EEPROM_SZ; i++) EEPROM.write(i, 0);
  EEPROM.commit();
  webSrv.send(200, "text/plain", "OK");
  delay(2000); ESP.restart();
}

// ── Reboot ────────────────────────────────────────────────────────
void hReboot() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  webSrv.send(200, "text/plain", "OK");
  Serial.println(F("Reinicio solicitado desde portal."));
  delay(800); ESP.restart();
}

// ── Force Sleep ───────────────────────────────────────────────────
void hForceSleep() {
  if (!httpAuth()) { webSrv.send(401, "text/plain", "NO_AUTH"); return; }
  webSrv.send(200, "text/plain", "OK");
  Serial.println(F("Deep Sleep forzado desde portal web."));
  delay(400);
  enterDeepSleep();
}

// ── Logout ───────────────────────────────────────────────────────
void hLogout() {
  sesTok = "";
  webSrv.sendHeader("Set-Cookie", "sh_tok=; Max-Age=0; Path=/");
  webSrv.sendHeader("Location", "/login");
  webSrv.send(302);
}

// ═══════════════════════════════════════════════════════════════════
//  PÁGINA DE LOGIN
// ═══════════════════════════════════════════════════════════════════
void hLoginGET() {
  // Si ya hay sesión activa, redirigir al dashboard
  if (httpAuth()) { webSrv.sendHeader("Location", "/"); webSrv.send(302); return; }
  static const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es" data-theme="dark"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Segurity Home — Acceso</title>
<style>
:root{--bg:#09090f;--sur:rgba(255,255,255,.05);--bor:rgba(255,255,255,.09);--ac:#00d4ff;--ac2:#7c3aed;--tx:#e8ecf4;--mu:#5a6a8a}
[data-theme=light]{--bg:#f0f4ff;--sur:rgba(0,0,0,.04);--bor:rgba(0,0,0,.10);--tx:#0f172a;--mu:#64748b}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px;transition:background .3s}
body::before{content:'';position:fixed;inset:0;background-image:linear-gradient(rgba(0,212,255,.025) 1px,transparent 1px),linear-gradient(90deg,rgba(0,212,255,.025) 1px,transparent 1px);background-size:52px 52px;pointer-events:none}
.orb{position:fixed;border-radius:50%;filter:blur(90px);pointer-events:none}
.o1{width:420px;height:420px;background:rgba(0,212,255,.07);top:-120px;right:-120px;animation:drift 9s ease-in-out infinite}
.o2{width:340px;height:340px;background:rgba(124,58,237,.08);bottom:-100px;left:-100px;animation:drift 9s ease-in-out infinite;animation-delay:-4.5s}
@keyframes drift{0%,100%{transform:translate(0,0)}50%{transform:translate(18px,-18px)}}
.card{width:100%;max-width:400px;background:var(--sur);border:1px solid var(--bor);border-radius:24px;padding:44px 40px;backdrop-filter:blur(20px);position:relative;z-index:1;animation:up .5s cubic-bezier(.16,1,.3,1)}
@keyframes up{from{opacity:0;transform:translateY(20px)}to{opacity:1;transform:translateY(0)}}
.card::before{content:'';position:absolute;top:0;left:18%;right:18%;height:1px;background:linear-gradient(90deg,transparent,var(--ac),transparent)}
.logo{text-align:center;margin-bottom:36px}
.shield{width:64px;height:64px;margin:0 auto 16px;background:linear-gradient(135deg,rgba(0,212,255,.15),rgba(124,58,237,.15));border:1px solid rgba(0,212,255,.25);border-radius:16px;display:flex;align-items:center;justify-content:center}
.logo h1{font-size:18px;font-weight:700;color:var(--tx);letter-spacing:2px;text-transform:uppercase}
.logo h1 em{font-style:normal;color:var(--ac)}
.logo p{font-size:10px;color:var(--mu);margin-top:6px;letter-spacing:3px;text-transform:uppercase}
.label{display:block;font-size:10px;font-weight:600;color:var(--mu);text-transform:uppercase;letter-spacing:2px;margin-bottom:8px}
.inp-wrap{position:relative;margin-bottom:24px}
.inp-ico{position:absolute;left:14px;top:50%;transform:translateY(-50%);color:var(--mu);transition:color .2s;pointer-events:none}
.inp-wrap:focus-within .inp-ico{color:var(--ac)}
input[type=password]{width:100%;padding:13px 14px 13px 44px;background:rgba(255,255,255,.04);border:1px solid var(--bor);border-radius:12px;color:var(--tx);font-size:18px;letter-spacing:.4em;transition:border .2s,box-shadow .2s}
[data-theme=light] input[type=password]{background:rgba(0,0,0,.04)}
input:focus{outline:none;border-color:rgba(0,212,255,.45);box-shadow:0 0 0 3px rgba(0,212,255,.09)}
.btn{width:100%;padding:14px;border:none;border-radius:12px;background:linear-gradient(135deg,#009bb8,#7c3aed);color:#fff;font-size:13px;font-weight:700;letter-spacing:2px;text-transform:uppercase;cursor:pointer;box-shadow:0 4px 24px rgba(0,212,255,.2);transition:opacity .2s,transform .15s;position:relative;overflow:hidden}
.btn:hover{opacity:.88;transform:translateY(-1px)}
.btn:active{transform:translateY(0)}
.err{background:rgba(255,71,87,.12);border:1px solid rgba(255,71,87,.3);color:#ff4757;border-radius:10px;padding:10px 14px;font-size:13px;text-align:center;margin-bottom:16px;display:none}
.err.show{display:block;animation:up .3s ease}
.theme-btn{position:absolute;top:16px;right:16px;background:none;border:1px solid var(--bor);border-radius:8px;padding:6px 10px;cursor:pointer;color:var(--mu);font-size:13px;transition:all .2s}
.theme-btn:hover{border-color:var(--ac);color:var(--ac)}
</style></head>
<body>
<div class="orb o1"></div><div class="orb o2"></div>
<button class="theme-btn" onclick="tgl()">◐</button>
<div class="card">
  <div class="logo">
    <div class="shield">
      <svg width="28" height="28" viewBox="0 0 28 28" fill="none"><path d="M14 2L4 6v8c0 5.5 4.3 10.7 10 12 5.7-1.3 10-6.5 10-12V6L14 2z" fill="rgba(0,212,255,.15)" stroke="#00d4ff" stroke-width="1.5"/><path d="M10 13.5l3 3 5-5" stroke="#00d4ff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
    </div>
    <h1>SEGURITY <em>HOME</em></h1>
    <p>Sistema de Alarma ESP32</p>
  </div>
  <form id="frm" onsubmit="login(event)">
    <label class="label">Código de acceso</label>
    <div class="inp-wrap">
      <svg class="inp-ico" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0110 0v4"/></svg>
      <input type="password" id="pin" autocomplete="current-password" autofocus required>
    </div>
    <div class="err" id="err">PIN incorrecto. Intenta de nuevo.</div>
    <button class="btn" type="submit" id="btn">INGRESAR</button>
  </form>
</div>
<script>
const h=document.documentElement;
const sv=localStorage.getItem('sh_theme')||'dark';
h.setAttribute('data-theme',sv);
function tgl(){const t=h.getAttribute('data-theme')==='dark'?'light':'dark';h.setAttribute('data-theme',t);localStorage.setItem('sh_theme',t);}
async function login(e){
  e.preventDefault();
  const btn=document.getElementById('btn');
  btn.textContent='···';btn.disabled=true;
  const fd=new URLSearchParams({pin:document.getElementById('pin').value});
  const r=await fetch('/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd});
  const t=await r.text();
  if(t==='OK'){window.location.replace('/');}
  else{document.getElementById('err').classList.add('show');document.getElementById('pin').value='';btn.textContent='INGRESAR';btn.disabled=false;}
}
</script>
</body></html>
)rawliteral";
  webSrv.send(200, "text/html", PAGE);
}

void hLoginPOST() {
  String pin = webSrv.arg("pin"); pin.trim();
  String stored = String(cfg.webPin); stored.trim();

  if (pin == stored) {
    sesTok = sesGen();
    sesMs  = millis();
    webSrv.sendHeader("Set-Cookie", "sh_tok=" + sesTok + "; Path=/; HttpOnly");
    webSrv.send(200, "text/plain", "OK");
  } else {
    webSrv.send(200, "text/plain", "WRONG");
  }
}

// ═══════════════════════════════════════════════════════════════════
//  PORTAL PRINCIPAL (requiere sesión)
// ═══════════════════════════════════════════════════════════════════
void hRoot() {
  if (!httpAuth()) {
    webSrv.sendHeader("Location", "/login");
    webSrv.send(302);
    return;
  }
  static const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es" data-theme="dark"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Segurity Home</title>
<style>
:root{--bg:#09090f;--bg2:#0d0d1a;--card:rgba(255,255,255,.042);--bor:rgba(255,255,255,.085);--tx:#e8ecf4;--mu:#5a6a8a;--ac:#00d4ff;--ac2:#7c3aed;--ok:#00e676;--er:#ff4757;--wa:#ffc107;--nh:60px;--sw:240px}
[data-theme=light]{--bg:#f0f4ff;--bg2:#e8edf8;--card:rgba(0,0,0,.04);--bor:rgba(0,0,0,.10);--tx:#0f172a;--mu:#64748b}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:var(--tx);min-height:100vh;transition:background .3s,color .3s}
/* NAV */
.nav{position:fixed;top:0;left:0;right:0;height:var(--nh);z-index:100;background:rgba(9,9,15,.88);backdrop-filter:blur(18px);border-bottom:1px solid var(--bor);display:flex;align-items:center;gap:14px;padding:0 20px}
[data-theme=light] .nav{background:rgba(240,244,255,.88)}
.nav-logo{display:flex;align-items:center;gap:9px;font-weight:700;font-size:13px;letter-spacing:1.5px;text-transform:uppercase;color:var(--tx);flex-shrink:0}
.nav-logo em{font-style:normal;color:var(--ac)}
.nav-stat{display:flex;align-items:center;gap:18px;margin-left:auto;font-size:12px}
.ni{display:flex;align-items:center;gap:6px;color:var(--mu)}
.dot{width:7px;height:7px;border-radius:50%;background:var(--ok);box-shadow:0 0 5px var(--ok)}
.dot.w{background:var(--wa);box-shadow:0 0 5px var(--wa)}
.dot.e{background:var(--er);box-shadow:0 0 5px var(--er)}
.ibtn{background:none;border:1px solid var(--bor);border-radius:8px;padding:5px 10px;cursor:pointer;color:var(--mu);font-size:12px;transition:all .2s}
.ibtn:hover{border-color:var(--ac);color:var(--ac)}
.hbtn{display:none;align-items:center;justify-content:center;background:none;border:none;cursor:pointer;color:var(--tx);padding:4px}
/* SIDEBAR */
.side{position:fixed;top:var(--nh);left:0;bottom:0;width:var(--sw);background:var(--bg2);border-right:1px solid var(--bor);padding:20px 10px;overflow-y:auto;z-index:90;transition:transform .3s}
.slbl{font-size:10px;font-weight:600;color:var(--mu);letter-spacing:2px;text-transform:uppercase;padding:0 10px;margin:16px 0 6px}
.sbtn{display:flex;align-items:center;gap:9px;padding:9px 10px;border-radius:9px;cursor:pointer;font-size:13px;font-weight:500;color:var(--mu);transition:all .2s;width:100%;border:none;background:none}
.sbtn:hover{background:var(--card);color:var(--tx)}
.sbtn.on{background:linear-gradient(135deg,rgba(0,212,255,.1),rgba(124,58,237,.1));color:var(--ac);border:1px solid rgba(0,212,255,.18)}
.sbtn svg{width:15px;height:15px;flex-shrink:0}
/* MAIN */
.main{margin-left:var(--sw);padding:calc(var(--nh) + 28px) 32px 40px}
.sec{display:none;animation:fi .3s ease}
.sec.on{display:block}
@keyframes fi{from{opacity:0;transform:translateY(7px)}to{opacity:1;transform:translateY(0)}}
.ptit{font-size:22px;font-weight:700;margin-bottom:3px}
.psub{font-size:13px;color:var(--mu);margin-bottom:26px}
/* CARDS */
.card{background:var(--card);border:1px solid var(--bor);border-radius:16px;padding:22px;margin-bottom:18px;position:relative;overflow:hidden;transition:border-color .2s}
.card:hover{border-color:rgba(0,212,255,.18)}
.card::before{content:'';position:absolute;top:0;left:14%;right:14%;height:1px;background:linear-gradient(90deg,transparent,rgba(0,212,255,.28),transparent)}
.ctit{font-size:11px;font-weight:600;color:var(--mu);text-transform:uppercase;letter-spacing:1.5px;margin-bottom:18px;display:flex;align-items:center;gap:7px}
.ctit svg{width:13px;height:13px}
/* STAT GRID */
.sgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:14px;margin-bottom:20px}
.sc{background:var(--card);border:1px solid var(--bor);border-radius:13px;padding:18px;transition:all .2s}
.sc:hover{transform:translateY(-2px);border-color:rgba(0,212,255,.22);box-shadow:0 6px 20px rgba(0,212,255,.05)}
.sico{width:34px;height:34px;border-radius:9px;display:flex;align-items:center;justify-content:center;margin-bottom:12px}
.ic{color:var(--ac);background:rgba(0,212,255,.11)} .iv{color:var(--ac2);background:rgba(124,58,237,.11)} .ig{color:var(--ok);background:rgba(0,230,118,.11)} .ir{color:var(--er);background:rgba(255,71,87,.11)}
.sv{font-size:18px;font-weight:700}.sl{font-size:11px;color:var(--mu);margin-top:3px}
/* FORMS */
.fr{margin-bottom:18px}
.fr label{display:block;font-size:10px;font-weight:600;color:var(--mu);letter-spacing:1.5px;text-transform:uppercase;margin-bottom:7px}
.inp{width:100%;padding:10px 14px;border-radius:9px;background:rgba(255,255,255,.042);border:1px solid var(--bor);color:var(--tx);font-size:13px;transition:border .2s,box-shadow .2s}
[data-theme=light] .inp{background:rgba(0,0,0,.04)}
.inp:focus{outline:none;border-color:rgba(0,212,255,.45);box-shadow:0 0 0 3px rgba(0,212,255,.08)}
.hint{font-size:11px;color:var(--mu);margin-top:5px}
.r2{display:grid;grid-template-columns:1fr 1fr;gap:14px}
@media(max-width:560px){.r2{grid-template-columns:1fr}}
/* BUTTONS */
.btn{padding:10px 22px;border-radius:9px;font-size:12px;font-weight:600;cursor:pointer;transition:all .2s;border:none;display:inline-flex;align-items:center;gap:7px;letter-spacing:.5px}
.bp{background:linear-gradient(135deg,#009bb8,#7c3aed);color:#fff;box-shadow:0 3px 14px rgba(0,212,255,.18)}
.bp:hover{opacity:.88;transform:translateY(-1px)}
.bg{background:var(--card);border:1px solid var(--bor);color:var(--tx)}
.bg:hover{border-color:var(--ac);color:var(--ac)}
.bd{background:rgba(255,71,87,.1);border:1px solid rgba(255,71,87,.28);color:var(--er)}
.bd:hover{background:rgba(255,71,87,.18)}
.brow{display:flex;flex-wrap:wrap;gap:9px;margin-top:6px}
/* WIFI LIST */
.wi{display:flex;align-items:center;gap:10px;padding:10px 14px;border-radius:9px;cursor:pointer;transition:background .2s;border:1px solid transparent}
.wi:hover{background:var(--card);border-color:var(--bor)}
.wi.sel{background:rgba(0,212,255,.06);border-color:rgba(0,212,255,.22)}
.wss{font-size:13px;font-weight:500;flex:1}
.wlk{font-size:11px;color:var(--mu)}
.bars{display:flex;align-items:flex-end;gap:2px;height:14px}
.bar{width:3px;border-radius:2px;background:var(--mu)}
.wi.sel .bar{background:var(--ac)}
/* TOAST */
.ta{position:fixed;bottom:22px;right:22px;z-index:999;display:flex;flex-direction:column;gap:7px}
.toast{padding:11px 18px;border-radius:11px;font-size:12px;font-weight:500;animation:tsi .3s cubic-bezier(.16,1,.3,1);min-width:220px;max-width:320px;display:flex;align-items:center;gap:9px;backdrop-filter:blur(12px);box-shadow:0 6px 28px rgba(0,0,0,.3)}
.tok{background:rgba(0,230,118,.14);border:1px solid rgba(0,230,118,.28);color:var(--ok)}
.ter{background:rgba(255,71,87,.14);border:1px solid rgba(255,71,87,.28);color:var(--er)}
.tin{background:rgba(0,212,255,.11);border:1px solid rgba(0,212,255,.22);color:var(--ac)}
@keyframes tsi{from{opacity:0;transform:translateX(36px)}to{opacity:1;transform:translateX(0)}}
/* MODAL */
.mbg{position:fixed;inset:0;background:rgba(0,0,0,.6);backdrop-filter:blur(4px);z-index:200;display:none;align-items:center;justify-content:center}
.mbg.on{display:flex}
.modal{background:var(--bg2);border:1px solid var(--bor);border-radius:18px;padding:30px;max-width:380px;width:90%;animation:fi .3s cubic-bezier(.16,1,.3,1)}
.modal h3{font-size:17px;font-weight:700;margin-bottom:7px}
.modal p{color:var(--mu);font-size:13px;margin-bottom:22px}
/* SPIN */
.spin{width:14px;height:14px;border:2px solid rgba(255,255,255,.2);border-top-color:#fff;border-radius:50%;animation:rot .6s linear infinite;flex-shrink:0}
@keyframes rot{to{transform:rotate(360deg)}}
/* INFO BOX */
.ibox{border-radius:9px;padding:13px 16px;font-size:12px;color:var(--mu);margin-bottom:18px}
.ibox.blue{background:rgba(0,212,255,.06);border:1px solid rgba(0,212,255,.14)}
.ibox.red{background:rgba(255,71,87,.06);border:1px solid rgba(255,71,87,.18)}
/* RESPONSIVE */
@media(max-width:768px){.side{transform:translateX(-100%)}.side.on{transform:translateX(0)}.main{margin-left:0;padding-left:16px;padding-right:16px}.hbtn{display:flex!important}.nav-ip{display:none}}
#overlay{display:none;position:fixed;inset:0;z-index:89;background:rgba(0,0,0,.5)}
</style></head>
<body>
<nav class="nav">
  <button class="hbtn" onclick="tgSide()">
    <svg width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="3" y1="6" x2="21" y2="6"/><line x1="3" y1="12" x2="21" y2="12"/><line x1="3" y1="18" x2="21" y2="18"/></svg>
  </button>
  <div class="nav-logo">
    <svg width="24" height="24" viewBox="0 0 28 28" fill="none"><path d="M14 2L4 6v8c0 5.5 4.3 10.7 10 12 5.7-1.3 10-6.5 10-12V6L14 2z" fill="rgba(0,212,255,.15)" stroke="#00d4ff" stroke-width="1.5"/><path d="M10 13.5l3 3 5-5" stroke="#00d4ff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
    SEGURITY <em>&nbsp;HOME</em>
  </div>
  <div class="nav-stat">
    <div class="ni"><span class="dot" id="wdot"></span><span id="wlbl">...</span></div>
    <div class="ni nav-ip" id="iplbl" style="font-family:monospace;font-size:11px"></div>
    <button class="ibtn" onclick="tgTheme()">◐</button>
    <form action="/logout" method="POST" style="margin:0"><button class="ibtn" type="submit">⎋ Salir</button></form>
  </div>
</nav>

<aside class="side" id="side">
  <div class="slbl">Principal</div>
  <button class="sbtn on" onclick="go('dash',this)">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/></svg>Dashboard</button>
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
  <div class="ptit">Dashboard</div><div class="psub">Estado en tiempo real del sistema</div>
  <div class="sgrid">
    <div class="sc"><div class="sico ic"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12.55a11 11 0 0114.08 0"/><path d="M1.42 9a16 16 0 0121.16 0"/><path d="M8.53 16.11a6 6 0 016.95 0"/><circle cx="12" cy="20" r="1" fill="currentColor"/></svg></div><div class="sv" id="dRSSI">—</div><div class="sl">WiFi · señal</div></div>
    <div class="sc"><div class="sico iv"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 10c0 7-9 13-9 13S3 17 3 10a9 9 0 0118 0z"/><circle cx="12" cy="10" r="3"/></svg></div><div class="sv" id="dIP">—</div><div class="sl">Dirección IP</div></div>
    <div class="sc"><div class="sico ig" id="dBtnIco"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg></div><div class="sv" id="dBtn">—</div><div class="sl">Botón Pánico</div></div>
    <div class="sc"><div class="sico" id="dBotIco"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg></div><div class="sv" id="dBot">—</div><div class="sl">Bot Telegram</div></div>
    <div class="sc"><div class="sico ig" id="dSlpIco"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.79A9 9 0 1111.21 3 7 7 0 0021 12.79z"/></svg></div><div class="sv" id="dSlp">—</div><div class="sl">Deep Sleep en</div></div>
  </div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>Información del Sistema</div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:14px;font-size:13px">
      <div><span style="color:var(--mu);font-size:11px">Administrador</span><br><strong id="dAdm">—</strong></div>
      <div><span style="color:var(--mu);font-size:11px">Grupo Emergencia</span><br><strong id="dGrp">—</strong></div>
      <div><span style="color:var(--mu);font-size:11px">Red WiFi</span><br><strong id="dSSID">—</strong></div>
      <div><span style="color:var(--mu);font-size:11px">Tiempo activo</span><br><strong id="dUp">—</strong></div>
    </div>
    <div style="margin-top:16px"><button class="btn bp" onclick="doTest()"><svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg>Enviar Mensaje Prueba</button></div>
  </div>
</div>

<!-- WIFI -->
<div class="sec" id="s-wifi">
  <div class="ptit">Red WiFi</div><div class="psub">Configura la red a la que conectará el dispositivo</div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 11-2.12-9.36L23 10"/></svg>Redes disponibles</div>
    <button class="btn bg" onclick="doScan()" id="scanBtn"><svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 11-2.12-9.36L23 10"/></svg>Escanear</button>
    <div id="wlist" style="margin-top:14px;display:flex;flex-direction:column;gap:5px"></div>
  </div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0110 0v4"/></svg>Credenciales</div>
    <form onsubmit="svWifi(event)">
      <div class="fr"><label>SSID (nombre de red)</label><input class="inp" type="text" id="wSSID" maxlength="31"></div>
      <div class="fr"><label>Password</label><input class="inp" type="password" id="wPass" maxlength="63"></div>
      <button class="btn bp" type="submit" id="wBtn"><svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 21H5a2 2 0 01-2-2V5a2 2 0 012-2h11l5 5v11a2 2 0 01-2 2z"/><polyline points="17 21 17 13 7 13 7 21"/></svg>Guardar y Conectar</button>
    </form>
  </div>
</div>

<!-- TELEGRAM -->
<div class="sec" id="s-tg">
  <div class="ptit">Telegram Bot</div><div class="psub">Token del bot y configuración del administrador</div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/></svg>Token del Bot</div>
    <form onsubmit="svTok(event)">
      <div class="fr"><label>Bot Token</label><input class="inp" type="password" id="bTok" placeholder="123456789:ABC..." maxlength="63"><div class="hint">Obtén el token de @BotFather en Telegram</div></div>
      <button class="btn bp" type="submit">Guardar Token</button>
    </form>
  </div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 21v-2a4 4 0 00-4-4H8a4 4 0 00-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>Administrador Principal</div>
    <div class="ibox blue">💡 El administrador recibe: mensajes de arranque, reporte diario a las 18:00 y respuestas a todos los comandos del bot (/status, /ip, /pin, etc.)</div>
    <form onsubmit="svAdm(event)">
      <div class="r2">
        <div class="fr"><label>Nombre</label><input class="inp" type="text" id="aNom" maxlength="31"></div>
        <div class="fr"><label>Chat ID</label><input class="inp" type="text" id="aID" maxlength="19"><div class="hint">Envía /getchatid al bot</div></div>
      </div>
      <button class="btn bp" type="submit">Guardar Administrador</button>
    </form>
  </div>
</div>

<!-- GRUPO -->
<div class="sec" id="s-grp">
  <div class="ptit">Grupo de Emergencia</div><div class="psub">Grupo de Telegram que recibe las alarmas de pánico</div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>Configurar Grupo</div>
    <div class="ibox red">🚨 Al mantener el botón de pánico 6 segundos, se envían <strong>4 mensajes de alerta</strong> a este grupo de Telegram.<br><br>
    <strong>¿Cómo obtener el Chat ID del grupo?</strong><br>
    1. Agrega el bot al grupo y envía un mensaje<br>
    2. Abre: <code style="color:var(--ac)">api.telegram.org/bot&lt;TOKEN&gt;/getUpdates</code><br>
    3. El <code>chat.id</code> del grupo es un número negativo (ej: <code>-1001234567890</code>)</div>
    <form onsubmit="svGrp(event)">
      <div class="fr"><label>Chat ID del Grupo</label><input class="inp" type="text" id="gID" placeholder="-1001234567890" maxlength="19"><div class="hint">Los grupos tienen IDs negativos (empiezan con -100)</div></div>
      <button class="btn bp" type="submit">Guardar Grupo</button>
    </form>
  </div>
</div>

<!-- ACCESS POINT -->
<div class="sec" id="s-ap">
  <div class="ptit">Access Point</div><div class="psub">Punto de acceso WiFi cuando no hay red disponible</div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="2"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg>Credenciales del AP</div>
    <div class="ibox blue">📶 Si el ESP32 no se conecta a la red WiFi configurada, crea automáticamente este punto de acceso. Cualquier dispositivo que se conecte será redirigido a esta página (Captive Portal).</div>
    <form onsubmit="svAP(event)">
      <div class="r2">
        <div class="fr"><label>Nombre (SSID)</label><input class="inp" type="text" id="apN" maxlength="31"></div>
        <div class="fr"><label>Password</label><input class="inp" type="password" id="apP" maxlength="31"><div class="hint">Mínimo 8 caracteres</div></div>
      </div>
      <button class="btn bp" type="submit">Guardar Access Point</button>
    </form>
  </div>
</div>

<!-- SISTEMA -->
<div class="sec" id="s-sys">
  <div class="ptit">Sistema</div><div class="psub">PIN del portal y acciones del dispositivo</div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0110 0v4"/></svg>PIN del Portal Web</div>
    <form onsubmit="svPin(event)" style="display:flex;align-items:flex-end;gap:12px;flex-wrap:wrap">
      <div class="fr" style="margin:0;flex:1;min-width:160px"><label>Nuevo PIN</label><input class="inp" type="password" id="nPin" placeholder="Mínimo 4 caracteres" maxlength="15"></div>
      <button class="btn bp" type="submit" style="margin-bottom:18px">Cambiar PIN</button>
    </form>
  </div>
  <div class="card">
    <div class="ctit"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>Acciones del Dispositivo</div>
    <div class="brow">
      <button class="btn bg" onclick="doReboot()"><svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 11-2.12-9.36L23 10"/></svg>Reiniciar ESP32</button>
      <button class="btn bg" onclick="askSleep()"><svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.79A9 9 0 1111.21 3 7 7 0 0021 12.79z"/></svg>Poner a Dormir Ahora</button>
      <button class="btn bd" onclick="askReset()"><svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6"/></svg>Borrar Configuración</button>
    </div>
    <p style="font-size:11px;color:var(--mu);margin-top:10px">⚠️ Borrar configuración elimina todos los datos de EEPROM y reinicia con valores de fábrica.<br>💤 Deep Sleep: bajo consumo energético; solo el botón de pánico (GPIO4) despierta el dispositivo.</p>
  </div>
</div>

</main>

<!-- MODAL -->
<div class="mbg" id="mbg"><div class="modal"><h3 id="mtit">⚠️ Confirmar Acción</h3><p id="mtxt"></p><div class="brow"><button class="btn bd" id="mok">Confirmar</button><button class="btn bg" onclick="closeMod()">Cancelar</button></div></div></div>

<!-- TOAST AREA -->
<div class="ta" id="ta"></div>

<!-- OVERLAY MOBILE -->
<div id="overlay" onclick="closeSide()"></div>

<script>
// Theme
const H=document.documentElement;
H.setAttribute('data-theme',localStorage.getItem('sh_theme')||'dark');
function tgTheme(){const t=H.getAttribute('data-theme')==='dark'?'light':'dark';H.setAttribute('data-theme',t);localStorage.setItem('sh_theme',t);}

// Sidebar
function tgSide(){document.getElementById('side').classList.toggle('on');const o=document.getElementById('overlay');o.style.display=document.getElementById('side').classList.contains('on')?'block':'none';}
function closeSide(){document.getElementById('side').classList.remove('on');document.getElementById('overlay').style.display='none';}

// Navigation
function go(id,btn){
  document.querySelectorAll('.sec').forEach(s=>s.classList.remove('on'));
  document.querySelectorAll('.sbtn').forEach(b=>b.classList.remove('on'));
  document.getElementById('s-'+id).classList.add('on');
  btn.classList.add('on');
  if(window.innerWidth<768)closeSide();
  if(id==='dash')loadSt();
}

// Toast
function toast(m,t){
  const a=document.getElementById('ta'),e=document.createElement('div');
  e.className='toast t'+t;
  e.innerHTML='<span>'+(t==='ok'?'✓':t==='er'?'✕':'ℹ')+'</span><span>'+m+'</span>';
  a.appendChild(e);
  setTimeout(()=>{e.style.opacity='0';e.style.transform='translateX(36px)';e.style.transition='.3s';setTimeout(()=>e.remove(),300);},3500);
}

// Modal
let mCb=null;
function askReset(){
  document.getElementById('mtit').textContent='⚠️ Confirmar Acción';
  document.getElementById('mtxt').textContent='Se borrarán TODOS los datos guardados. El dispositivo se reiniciará con valores de fábrica.';
  mCb=()=>{post('/reset',{}).then(()=>{toast('Reseteando...','in');setTimeout(()=>location.reload(),4000);});};
  document.getElementById('mbg').classList.add('on');
  document.getElementById('mok').onclick=()=>{closeMod();mCb&&mCb();};
}
function askSleep(){
  document.getElementById('mtit').textContent='💤 Poner en Deep Sleep';
  document.getElementById('mtxt').textContent='El ESP32 entrará en modo de bajo consumo. El portal web y Telegram dejarán de responder. Solo el botón de pánico (GPIO4) lo despertará — al despertar la alarma se activa en 6 segundos.';
  mCb=()=>{post('/force-sleep',{}).then(r=>{r==='OK'?toast('Dispositivo entrando en reposo...','in'):toast('Error: '+r,'er');});};
  document.getElementById('mbg').classList.add('on');
  document.getElementById('mok').onclick=()=>{closeMod();mCb&&mCb();};
}
function closeMod(){document.getElementById('mbg').classList.remove('on');}

// API
async function post(url,data){
  const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});
  return r.text();
}

// Status
async function loadSt(){
  try{
    const d=await(await fetch('/status-json')).json();
    // Navbar
    const dot=document.getElementById('wdot'),lbl=document.getElementById('wlbl');
    if(d.apMode){dot.className='dot w';lbl.textContent='Modo AP';}
    else if(d.rssi>-65){dot.className='dot';lbl.textContent='WiFi '+d.rssi+'dBm';}
    else if(d.rssi>-80){dot.className='dot w';lbl.textContent='WiFi '+d.rssi+'dBm';}
    else{dot.className='dot e';lbl.textContent='Señal débil';}
    document.getElementById('iplbl').textContent=d.ip||'';
    // Cards
    document.getElementById('dRSSI').textContent=d.apMode?'AP Mode':(d.rssi+'dBm');
    document.getElementById('dIP').textContent=d.ip||'—';
    const bi=document.getElementById('dBtnIco'),bv=document.getElementById('dBtn');
    bv.textContent=d.boton?'PRESIONADO':'Libre';bi.className='sico '+(d.boton?'ir':'ig');
    const oi=document.getElementById('dBotIco'),ov=document.getElementById('dBot');
    ov.textContent=d.botOk?'Conectado':'Sin token';oi.className='sico '+(d.botOk?'ic':'ir');
    document.getElementById('dAdm').textContent=d.admin||'—';
    document.getElementById('dGrp').textContent=d.grupoOk?'✓ Configurado':'⚠ No configurado';
    document.getElementById('dSSID').textContent=d.ssid||'—';
    document.getElementById('dUp').textContent=uptime(d.uptime||0);
    // Deep Sleep countdown
    if(d.sleepIn!==undefined){
      const tm=Math.floor(d.sleepIn/60),ts=d.sleepIn%60;
      document.getElementById('dSlp').textContent=d.sleepIn>0?(tm+'m '+ts+'s'):'—';
      document.getElementById('dSlpIco').className='sico '+(d.sleepIn>300?'ig':'iv');
    }
    // Prefill
    if(d.ssid)document.getElementById('wSSID').value=d.ssid;
    if(d.admin)document.getElementById('aNom').value=d.admin;
    if(d.admID)document.getElementById('aID').value=d.admID;
    if(d.grpID)document.getElementById('gID').value=d.grpID;
    if(d.apSSID)document.getElementById('apN').value=d.apSSID;
  }catch(e){}
}
function uptime(ms){const s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60),d=Math.floor(h/24);return d>0?d+'d '+(h%24)+'h':h>0?h+'h '+(m%60)+'m':m+'m '+(s%60)+'s';}

// WiFi Scan
async function doScan(){
  const b=document.getElementById('scanBtn'),l=document.getElementById('wlist');
  b.disabled=true;b.innerHTML='<div class="spin"></div>';
  l.innerHTML='<p style="color:var(--mu);font-size:13px">Buscando redes...</p>';
  try{
    const ns=await(await fetch('/scan-wifi')).json();
    l.innerHTML='';
    if(!ns.length){l.innerHTML='<p style="color:var(--mu);font-size:13px">Sin redes encontradas</p>';return;}
    ns.sort((a,b)=>b.rssi-a.rssi);
    ns.forEach(n=>{
      const lvl=n.rssi>-55?4:n.rssi>-65?3:n.rssi>-75?2:1;
      const bars=[5,8,11,14].map((h,i)=>`<div class="bar" style="height:${h}px;opacity:${i<lvl?1:.25}"></div>`).join('');
      const el=document.createElement('div');el.className='wi';
      el.innerHTML=`<div class="bars">${bars}</div><span class="wss">${n.ssid}</span><span class="wlk">${n.secured?'🔒':'🔓'} ${n.rssi}dBm</span>`;
      el.onclick=()=>{document.getElementById('wSSID').value=n.ssid;document.querySelectorAll('.wi').forEach(x=>x.classList.remove('sel'));el.classList.add('sel');};
      l.appendChild(el);
    });
  }catch(e){l.innerHTML='<p style="color:var(--er);font-size:13px">Error al escanear</p>';}
  b.disabled=false;b.innerHTML='<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 11-2.12-9.36L23 10"/></svg> Escanear';
}

// Save handlers
async function svWifi(e){
  e.preventDefault();const btn=e.target.querySelector('[type=submit]');
  const o=btn.innerHTML;btn.disabled=true;btn.innerHTML='<div class="spin"></div>';
  const r=await post('/save-wifi',{ssid:document.getElementById('wSSID').value,password:document.getElementById('wPass').value});
  btn.disabled=false;btn.innerHTML=o;
  if(r==='OK')toast('WiFi guardado. Reconectando...','ok');
  else if(r==='FALLBACK')toast('No se pudo conectar. Red anterior restaurada.','er');
  else toast('Error al guardar','er');
}
async function svTok(e){
  e.preventDefault();
  const r=await post('/save-token',{token:document.getElementById('bTok').value});
  r==='OK'?toast('Token guardado. Reiniciando...','ok'):toast('Error al guardar token','er');
}
async function svAdm(e){
  e.preventDefault();
  const r=await post('/save-admin',{nombre:document.getElementById('aNom').value,chatID:document.getElementById('aID').value});
  r==='OK'?toast('Administrador guardado','ok'):toast('Error','er');
}
async function svGrp(e){
  e.preventDefault();
  const r=await post('/save-grupo',{chatID:document.getElementById('gID').value});
  r==='OK'?toast('Grupo guardado','ok'):toast('Error','er');
}
async function svAP(e){
  e.preventDefault();
  const p=document.getElementById('apP').value;
  if(p.length>0&&p.length<8){toast('El password debe tener al menos 8 caracteres','er');return;}
  const r=await post('/save-ap',{ssid:document.getElementById('apN').value,password:p});
  r==='OK'?toast('Access Point guardado','ok'):toast('Error: '+r,'er');
}
async function svPin(e){
  e.preventDefault();
  const p=document.getElementById('nPin').value;
  if(p.length<4){toast('El PIN debe tener al menos 4 caracteres','er');return;}
  const r=await post('/save-pin',{pin:p});
  r==='OK'?toast('PIN actualizado','ok'):toast('Error','er');
  document.getElementById('nPin').value='';
}
async function doTest(){
  const r=await post('/test',{});
  r==='OK'?toast('Mensaje enviado al administrador','ok'):toast('Error: '+r,'er');
}
async function doReboot(){
  await post('/reboot',{});toast('Reiniciando ESP32...','in');
}

// Init
loadSt();setInterval(loadSt,8000);
</script>
</body></html>
)rawliteral";
  webSrv.send(200, "text/html", PAGE);
}
