#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include "time.h"
#include <ESPmDNS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

// ============================================================
// CONFIG
// ============================================================
const char* WIFI_SSID     = "PA5-2";
const char* WIFI_PASSWORD = "ipetplantaalta5";
const char* WEB_USER      = "admin";
const char* WEB_PASS      = "1p3t.2026";

#define MATRIX_PIN   4
#define MATRIX_W     32
#define MATRIX_H     8
#define BRIGHTNESS   40
#define SCROLL_DELAY 40

const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = -10800;
const int   DST_OFFSET = 0;

// ============================================================
// TOKEN AUTH CONFIG
// ============================================================
#define TOKEN_TIMEOUT_MS (15 * 60 * 1000UL)  // 15 minutos de inactividad

String authToken = "";
unsigned long tokenLastActivity = 0;

// ============================================================
// HORARIO ACTIVO  (LEDs encendidos)
// Fuera de este rango: LEDs apagados, web sigue activa.
// ============================================================
#define ACTIVE_HOUR_START 6  // desde las 08:00
#define ACTIVE_HOUR_END   20  // hasta las 22:59 (antes de las 23)

// ============================================================
// WIFI
// ============================================================
unsigned long lastAttempt   = 0;
const unsigned long retryInterval = 10000;

// ============================================================
// EVENTOS
// ============================================================
#define MAX_EVENTS 10
#define NAME_LEN   31

struct Event {
  char    name[NAME_LEN];
  int     day;
  int     month;
  int     year;
  uint8_t r, g, b;
  bool    isEvent;      // true = tiene fecha, false = solo texto fijo
  bool    dynamicColor; // true = color aleatorio al renderizar
};

Event events[MAX_EVENTS];
int   totalEvents = 0;

#define EEPROM_SIZE (1 + MAX_EVENTS * sizeof(Event))

// ============================================================
// MATRIX
// ============================================================
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
  MATRIX_W, MATRIX_H, MATRIX_PIN,
  NEO_MATRIX_TOP  + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800
);

WebServer server(80);

// ============================================================
// EEPROM
// ============================================================
void saveEvents() {
  EEPROM.write(0, totalEvents);
  int addr = 1;
  for (int i = 0; i < MAX_EVENTS; i++) {
    EEPROM.put(addr, events[i]);
    addr += sizeof(Event);
  }
  EEPROM.commit();
}

void loadEvents() {
  totalEvents = EEPROM.read(0);
  if (totalEvents < 0 || totalEvents > MAX_EVENTS) totalEvents = 0;
  int addr = 1;
  for (int i = 0; i < MAX_EVENTS; i++) {
    EEPROM.get(addr, events[i]);
    events[i].name[NAME_LEN - 1] = '\0';
    // Inicializar dynamicColor si viene de versión anterior
    if (events[i].dynamicColor != true && events[i].dynamicColor != false) {
      events[i].dynamicColor = false;
    }
    addr += sizeof(Event);
  }
}

// ============================================================
// HORARIO ACTIVO
// ============================================================
bool isActiveHour() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return true; // si no hay hora, mostrar igual
  int h = timeinfo.tm_hour;
  return (h >= ACTIVE_HOUR_START && h < ACTIVE_HOUR_END);
}

// ============================================================
// TIEMPO / DÍAS RESTANTES
// ============================================================
int daysRemaining(const Event& e) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return -9999;
  struct tm target = {0};
  target.tm_mday = e.day;
  target.tm_mon  = e.month - 1;
  target.tm_year = e.year  - 1900;
  time_t now_t    = mktime(&timeinfo);
  time_t target_t = mktime(&target);
  return (int)(difftime(target_t, now_t) / 86400.0);
}

// ============================================================
// ORDEN  (los que ya pasaron van al final)
// ============================================================
void sortEvents() {
  for (int i = 0; i < totalEvents - 1; i++) {
    for (int j = i + 1; j < totalEvents; j++) {
      // Los "no-evento" (solo texto) siempre van al final
      bool iIsEvent = events[i].isEvent;
      bool jIsEvent = events[j].isEvent;
      if (!iIsEvent && jIsEvent) {
        Event t = events[i]; events[i] = events[j]; events[j] = t;
      } else if (iIsEvent && jIsEvent) {
        if (daysRemaining(events[j]) < daysRemaining(events[i])) {
          Event t = events[i]; events[i] = events[j]; events[j] = t;
        }
      }
    }
  }
}

// ============================================================
// COLOR
// ============================================================
void hexToRGB(const String& hex, uint8_t &r, uint8_t &g, uint8_t &b) {
  String h = hex;
  if (h.startsWith("#")) h = h.substring(1);
  if (h.length() < 6) { r = 0; g = 200; b = 255; return; }
  r = strtol(h.substring(0, 2).c_str(), NULL, 16);
  g = strtol(h.substring(2, 4).c_str(), NULL, 16);
  b = strtol(h.substring(4, 6).c_str(), NULL, 16);
}

// Generar color aleatorio para dynamicColor
uint16_t getRandomColor() {
  uint8_t r = random(50, 256);
  uint8_t g = random(50, 256);
  uint8_t b = random(50, 256);
  return matrix.Color(r, g, b);
}

// ============================================================
// MATRIX — mostrar mensaje scrolling
// ============================================================
void mostrarMensaje(const char* texto, uint16_t color) {
  int px = strlen(texto) * 6;
  for (int x = matrix.width(); x > -px; x--) {
    matrix.fillScreen(0);
    matrix.setCursor(x, 0);
    matrix.setTextColor(color);
    matrix.print(texto);
    matrix.show();
    server.handleClient();
    delay(SCROLL_DELAY);
  }
}

void displayEvent(int i) {
  uint16_t color;
  
  // Si dynamicColor está activo, generar color aleatorio
  if (events[i].dynamicColor) {
    color = getRandomColor();
  } else {
    color = matrix.Color(events[i].r, events[i].g, events[i].b);
  }

  if (!events[i].isEvent) {
    // Solo texto fijo, sin fecha
    mostrarMensaje(events[i].name, color);
    return;
  }

  int d = daysRemaining(events[i]);
  char buf[64];
  if      (d < 0)  sprintf(buf, "%s PASO",      events[i].name);
  else if (d == 0) sprintf(buf, "%s HOY!",      events[i].name);
  else             sprintf(buf, "%s %d DIAS",   events[i].name, d);

  mostrarMensaje(buf, color);
}

// ============================================================
// TOKEN AUTH — verificar token
// ============================================================
bool checkToken() {
  // Si no hay token activo en el servidor, no hay sesión válida
  if (authToken.length() == 0) {
    return false;
  }
  
  // Verificar si el token fue proporcionado en la request
  String providedToken = server.arg("token");
  if (providedToken.length() == 0) {
    providedToken = server.header("X-Auth-Token");
  }
  
  // Comparar tokens
  if (providedToken != authToken) {
    return false;
  }
  
  // Verificar timeout por inactividad
  if (millis() - tokenLastActivity > TOKEN_TIMEOUT_MS) {
    authToken = "";
    return false;
  }
  
  // Actualizar última actividad
  tokenLastActivity = millis();
  return true;
}

// Generar token aleatorio
String generateToken() {
  String token = "";
  const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  for (int i = 0; i < 32; i++) {
    token += chars[random(0, strlen(chars))];
  }
  return token;
}

// ============================================================
// WEB — login
// ============================================================
void handleLogin() {
  String user = server.arg("user");
  String pass = server.arg("pass");
  
  if (user == WEB_USER && pass == WEB_PASS) {
    authToken = generateToken();
    tokenLastActivity = millis();
    server.send(200, "application/json", "{\"token\":\"" + authToken + "\",\"success\":true}");
  } else {
    server.send(401, "application/json", "{\"error\":\"invalid credentials\"}");
  }
}

// ============================================================
// WEB — logout
// ============================================================
void handleLogout() {
  authToken = "";
  server.send(200, "application/json", "{\"success\":true}");
}

// ============================================================
// WEB — página principal
// ============================================================
void handleRoot() {
  // Si NO hay token activo en el servidor, mostrar login
  if (authToken.length() == 0) {
    String html = R"rawhtml(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Matrix Eventos - Login</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Exo+2:wght@300;600;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg:      #080c10;
      --surface: #0d1520;
      --border:  #1a2d45;
      --accent:  #00c8ff;
      --accent2: #ff4060;
      --text:    #c8dde8;
      --muted:   #4a6070;
      --mono:    'Share Tech Mono', monospace;
      --sans:    'Exo 2', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: var(--sans);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 24px 16px;
    }
    body::before {
      content: '';
      position: fixed; inset: 0;
      background: repeating-linear-gradient(0deg,
        transparent, transparent 2px,
        rgba(0,200,255,.03) 2px, rgba(0,200,255,.03) 4px);
      pointer-events: none; z-index: 999;
    }
    .login-card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 24px;
      width: 100%;
      max-width: 320px;
      text-align: center;
    }
    .logo {
      font-family: var(--mono);
      font-size: 1.45rem;
      color: var(--accent);
      letter-spacing: .06em;
      text-shadow: 0 0 18px var(--accent);
      margin-bottom: 24px;
    }
    .logo span { color: var(--accent2); }
    label {
      display: block;
      font-size: .75rem;
      color: var(--muted);
      letter-spacing: .06em;
      text-transform: uppercase;
      margin-bottom: 5px;
      text-align: left;
    }
    input[type=text], input[type=password] {
      width: 100%; padding: 9px 11px;
      background: #0a1118; border: 1px solid var(--border);
      color: #eef; border-radius: 4px;
      font-family: var(--mono); font-size: .9rem;
      outline: none; transition: border-color .2s;
      margin-bottom: 14px;
    }
    input[type=text]:focus, input[type=password]:focus { border-color: var(--accent); }
    .submit-btn {
      width: 100%; padding: 11px;
      background: var(--accent); color: #000;
      border: none; border-radius: 4px;
      font-family: var(--mono); font-size: .95rem;
      font-weight: 700; letter-spacing: .04em;
      cursor: pointer; transition: background .2s, box-shadow .2s;
      margin-top: 8px;
    }
    .submit-btn:hover {
      background: #40d8ff;
      box-shadow: 0 0 20px rgba(0,200,255,.4);
    }
    .error-msg {
      color: var(--accent2);
      font-family: var(--mono);
      font-size: .8rem;
      margin-top: 12px;
      min-height: 1.2em;
    }
  </style>
</head>
<body>
  <div class="login-card">
    <div class="logo">&#9632; <span>MATRIX</span> CTRL</div>
    <form id="loginForm">
      <label>Usuario</label>
      <input type="text" id="user" name="user" required autocomplete="username">
      <label>Contraseña</label>
      <input type="password" id="pass" name="pass" required autocomplete="current-password">
      <button class="submit-btn" type="submit">INGRESAR</button>
      <div class="error-msg" id="errorMsg"></div>
    </form>
  </div>
  <script>
    document.getElementById('loginForm').addEventListener('submit', async function(e) {
      e.preventDefault();
      const user = document.getElementById('user').value;
      const pass = document.getElementById('pass').value;
      const errorMsg = document.getElementById('errorMsg');
      
      try {
        const res = await fetch('/login?user=' + encodeURIComponent(user) + '&pass=' + encodeURIComponent(pass));
        const data = await res.json();
        if (data.success) {
          sessionStorage.setItem('matrixToken', data.token);
          window.location.href = '/?token=' + encodeURIComponent(data.token);
        } else {
          errorMsg.textContent = 'Credenciales inválidas';
        }
      } catch (err) {
        errorMsg.textContent = 'Error de conexión';
      }
    });
  </script>
</body>
</html>
)rawhtml";
    server.send(200, "text/html", html);
    return;
  }
  
  // Si hay token activo, verificar validez de la sesión
  if (!checkToken()) {
    // Token inválido o expirado: limpiar y redirigir al login
    authToken = "";
    server.sendHeader("Location", "/");
    server.send(302);
    return;
  }

  String html = R"rawhtml(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Matrix Eventos</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Exo+2:wght@300;600;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg:      #080c10;
      --surface: #0d1520;
      --border:  #1a2d45;
      --accent:  #00c8ff;
      --accent2: #ff4060;
      --text:    #c8dde8;
      --muted:   #4a6070;
      --mono:    'Share Tech Mono', monospace;
      --sans:    'Exo 2', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: var(--sans);
      min-height: 100vh;
      padding: 24px 16px 48px;
    }
    /* scanline overlay */
    body::before {
      content: '';
      position: fixed; inset: 0;
      background: repeating-linear-gradient(0deg,
        transparent, transparent 2px,
        rgba(0,200,255,.03) 2px, rgba(0,200,255,.03) 4px);
      pointer-events: none; z-index: 999;
    }

    .wrap { max-width: 480px; margin: 0 auto; }

    header {
      display: flex; justify-content: space-between; align-items: flex-end;
      border-bottom: 1px solid var(--border);
      padding-bottom: 14px; margin-bottom: 22px;
    }
    .logo {
      font-family: var(--mono);
      font-size: 1.45rem;
      color: var(--accent);
      letter-spacing: .06em;
      text-shadow: 0 0 18px var(--accent);
    }
    .logo span { color: var(--accent2); }
    .logout-btn {
      font-family: var(--mono);
      font-size: .78rem;
      color: var(--accent2);
      text-decoration: none;
      border: 1px solid var(--accent2);
      padding: 4px 10px; border-radius: 3px;
      transition: all .2s;
      cursor: pointer;
    }
    .logout-btn:hover { background: var(--accent2); color: #000; }

    /* Horario activo badge */
    .schedule-badge {
      font-family: var(--mono);
      font-size: .72rem;
      color: var(--muted);
      text-align: center;
      margin-bottom: 18px;
      letter-spacing: .04em;
    }
    .schedule-badge .on  { color: #40ff80; }
    .schedule-badge .off { color: var(--accent2); }

    /* Lista de eventos */
    .event-list { margin-bottom: 24px; }
    .event-card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-left: 3px solid var(--accent);
      border-radius: 4px;
      padding: 10px 14px;
      margin-bottom: 8px;
      display: flex; justify-content: space-between; align-items: center;
      transition: border-color .2s;
    }
    .event-card:hover { border-left-color: var(--accent2); }
    .event-card.text-only { border-left-style: dashed; }
    .ev-left { display: flex; align-items: center; gap: 10px; }
    .dot { width: 12px; height: 12px; border-radius: 50%; flex-shrink: 0;
           box-shadow: 0 0 6px currentColor; }
    .ev-name {
      font-family: var(--mono);
      font-size: .95rem;
      color: #eef;
    }
    .ev-meta {
      font-size: .78rem;
      color: var(--muted);
      margin-top: 2px;
      font-family: var(--mono);
    }
    .ev-tag {
      font-size: .65rem;
      padding: 1px 6px;
      border-radius: 2px;
      margin-left: 6px;
      vertical-align: middle;
    }
    .tag-event { background: rgba(0,200,255,.15); color: var(--accent); }
    .tag-text  { background: rgba(255,64,96,.12);  color: var(--accent2); }
    .action-btns { display: flex; gap: 6px; }
    .del-btn, .edit-btn {
      color: var(--muted); text-decoration: none; font-size: 1.1rem;
      transition: color .2s; padding: 4px; cursor: pointer;
    }
    .del-btn:hover { color: var(--accent2); }
    .edit-btn:hover { color: var(--accent); }

    /* Formulario */
    .form-card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 20px;
    }
    .form-card h2 {
      font-family: var(--mono);
      font-size: .9rem;
      color: var(--accent);
      letter-spacing: .08em;
      margin-bottom: 18px;
    }
    label {
      display: block;
      font-size: .75rem;
      color: var(--muted);
      letter-spacing: .06em;
      text-transform: uppercase;
      margin-bottom: 5px;
    }
    input[type=text] {
      width: 100%; padding: 9px 11px;
      background: #0a1118; border: 1px solid var(--border);
      color: #eef; border-radius: 4px;
      font-family: var(--mono); font-size: .9rem;
      outline: none; transition: border-color .2s;
      margin-bottom: 4px;
    }
    input[type=text]:focus { border-color: var(--accent); }

    .char-count {
      text-align: right; font-family: var(--mono);
      font-size: .72rem; color: var(--accent);
      margin-bottom: 14px;
    }

    /* Checkbox "es evento" */
    .check-row {
      display: flex; align-items: center; gap: 10px;
      margin-bottom: 14px;
      padding: 10px 12px;
      background: rgba(0,200,255,.05);
      border: 1px solid var(--border);
      border-radius: 4px;
      cursor: pointer;
    }
    .check-row input[type=checkbox] {
      width: 17px; height: 17px; accent-color: var(--accent);
      cursor: pointer; flex-shrink: 0;
    }
    .check-label {
      font-family: var(--mono); font-size: .85rem; color: var(--text);
      user-select: none;
    }
    .check-sub {
      font-size: .72rem; color: var(--muted); margin-top: 1px;
    }

    /* Date picker + wrapper para gris */
    .date-wrapper {
      margin-bottom: 16px;
      transition: opacity .3s;
    }
    .date-wrapper.disabled {
      opacity: .35;
      pointer-events: none;
    }
    input[type=date] {
      width: 100%; padding: 9px 11px;
      background: #0a1118; border: 1px solid var(--border);
      color: #eef; border-radius: 4px;
      font-family: var(--mono); font-size: .9rem;
      outline: none; transition: border-color .2s;
    }
    input[type=date]:focus { border-color: var(--accent); }
    /* Estilo nativo del calendario en navegadores webkit */
    input[type=date]::-webkit-calendar-picker-indicator {
      filter: invert(.6) sepia(1) hue-rotate(170deg);
      cursor: pointer;
    }

    /* Color */
    .color-row {
      display: flex; align-items: center; gap: 12px; margin-bottom: 16px;
    }
    input[type=color] {
      width: 44px; height: 34px;
      border: 1px solid var(--border); background: #0a1118;
      border-radius: 4px; cursor: pointer; padding: 2px;
    }
    .presets { display: flex; gap: 7px; flex-wrap: wrap; }
    .preset {
      width: 24px; height: 24px; border-radius: 50%;
      cursor: pointer; border: 2px solid transparent;
      transition: border .15s, transform .15s;
      box-shadow: 0 0 6px rgba(0,0,0,.5);
    }
    .preset:hover { border-color: #fff; transform: scale(1.18); }

    /* Dynamic color checkbox */
    .dynamic-check {
      display: flex; align-items: center; gap: 10px;
      margin-bottom: 16px;
      padding: 10px 12px;
      background: rgba(255,64,96,.08);
      border: 1px solid var(--border);
      border-radius: 4px;
      cursor: pointer;
    }
    .dynamic-check input[type=checkbox] {
      width: 17px; height: 17px; accent-color: var(--accent2);
      cursor: pointer; flex-shrink: 0;
    }
    .dynamic-label {
      font-family: var(--mono); font-size: .85rem; color: var(--text);
      user-select: none;
    }
    .dynamic-sub {
      font-size: .72rem; color: var(--muted); margin-top: 1px;
    }

    /* Submit */
    .submit-btn {
      width: 100%; padding: 11px;
      background: var(--accent); color: #000;
      border: none; border-radius: 4px;
      font-family: var(--mono); font-size: .95rem;
      font-weight: 700; letter-spacing: .04em;
      cursor: pointer; transition: background .2s, box-shadow .2s;
    }
    .submit-btn:hover {
      background: #40d8ff;
      box-shadow: 0 0 20px rgba(0,200,255,.4);
    }

    .empty {
      text-align: center; color: var(--muted);
      font-family: var(--mono); font-size: .85rem;
      padding: 22px 0; letter-spacing: .06em;
    }
    
    /* Modal para editar */
    .modal {
      display: none;
      position: fixed;
      top: 0; left: 0;
      width: 100%; height: 100%;
      background: rgba(0,0,0,.7);
      z-index: 1000;
      align-items: center;
      justify-content: center;
    }
    .modal.active { display: flex; }
    .modal-content {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 20px;
      width: 90%;
      max-width: 400px;
      max-height: 90vh;
      overflow-y: auto;
    }
    .modal-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 18px;
      padding-bottom: 12px;
      border-bottom: 1px solid var(--border);
    }
    .modal-title {
      font-family: var(--mono);
      font-size: 1rem;
      color: var(--accent);
    }
    .modal-close {
      background: none;
      border: none;
      color: var(--muted);
      font-size: 1.4rem;
      cursor: pointer;
      padding: 0;
      line-height: 1;
    }
    .modal-close:hover { color: var(--accent2); }
    .modal-form label {
      display: block;
      font-size: .75rem;
      color: var(--muted);
      letter-spacing: .06em;
      text-transform: uppercase;
      margin-bottom: 5px;
    }
    .modal-form input[type=text],
    .modal-form input[type=date],
    .modal-form input[type=color] {
      width: 100%; padding: 9px 11px;
      background: #0a1118; border: 1px solid var(--border);
      color: #eef; border-radius: 4px;
      font-family: var(--mono); font-size: .9rem;
      outline: none; transition: border-color .2s;
      margin-bottom: 14px;
    }
    .modal-form input[type=text]:focus,
    .modal-form input[type=date]:focus { border-color: var(--accent); }
    .modal-actions {
      display: flex;
      gap: 10px;
      margin-top: 16px;
    }
    .modal-btn {
      flex: 1;
      padding: 10px;
      border: none;
      border-radius: 4px;
      font-family: var(--mono);
      font-size: .9rem;
      font-weight: 600;
      cursor: pointer;
      transition: background .2s;
    }
    .modal-save {
      background: var(--accent);
      color: #000;
    }
    .modal-save:hover { background: #40d8ff; }
    .modal-cancel {
      background: var(--border);
      color: var(--text);
    }
    .modal-cancel:hover { background: #2a3d55; }
    
    /* Checkbox styles for modal */
    .modal-check-row {
      display: flex; align-items: center; gap: 10px;
      margin-bottom: 14px;
      padding: 10px 12px;
      background: rgba(0,200,255,.05);
      border: 1px solid var(--border);
      border-radius: 4px;
      cursor: pointer;
    }
    .modal-check-row input[type=checkbox] {
      width: 17px; height: 17px; accent-color: var(--accent);
      cursor: pointer; flex-shrink: 0;
    }
    .modal-check-label {
      font-family: var(--mono); font-size: .85rem; color: var(--text);
      user-select: none;
    }
    .modal-check-sub {
      font-size: .72rem; color: var(--muted); margin-top: 1px;
    }
    
    .modal-dynamic-check {
      display: flex; align-items: center; gap: 10px;
      margin-bottom: 16px;
      padding: 10px 12px;
      background: rgba(255,64,96,.08);
      border: 1px solid var(--border);
      border-radius: 4px;
      cursor: pointer;
    }
    .modal-dynamic-check input[type=checkbox] {
      width: 17px; height: 17px; accent-color: var(--accent2);
      cursor: pointer; flex-shrink: 0;
    }
    .modal-dynamic-label {
      font-family: var(--mono); font-size: .85rem; color: var(--text);
      user-select: none;
    }
    .modal-dynamic-sub {
      font-size: .72rem; color: var(--muted); margin-top: 1px;
    }
  </style>
</head>
<body>
<div class="wrap">

  <header>
    <div class="logo">&#9632; <span>MATRIX</span> CTRL</div>
    <a class="logout-btn" id="logoutBtn">&#128274; SALIR</a>
  </header>

  <div class="schedule-badge">
    LEDs activos: )rawhtml";

  html += String(ACTIVE_HOUR_START) + ":00 &ndash; " + String(ACTIVE_HOUR_END) + ":00 &nbsp;|&nbsp; ";
  if (isActiveHour()) {
    html += "<span class='on'>&#9679; ENCENDIDO</span>";
  } else {
    html += "<span class='off'>&#9679; APAGADO (LEDs off)</span>";
  }

  html += R"rawhtml(
  </div>

  <div class="event-list">
)rawhtml";

  if (totalEvents == 0) {
    html += "<div class='empty'>// sin eventos cargados</div>";
  }

  for (int i = 0; i < totalEvents; i++) {
    int d = daysRemaining(events[i]);
    char hexColor[8];
    sprintf(hexColor, "#%02X%02X%02X", events[i].r, events[i].g, events[i].b);

    String diasStr;
    String tag;
    if (!events[i].isEvent) {
      diasStr = "texto fijo";
      tag = "<span class='ev-tag tag-text'>TEXTO</span>";
    } else {
      tag = "<span class='ev-tag tag-event'>EVENTO</span>";
      if      (d < 0)  diasStr = "Ya pasó";
      else if (d == 0) diasStr = "¡HOY!";
      else             diasStr = String(d) + " días &mdash; " + String(events[i].day) + "/" + String(events[i].month) + "/" + String(events[i].year);
    }

    html += "<div class='event-card" + String(!events[i].isEvent ? " text-only" : "") + "' data-index='" + String(i) + "'>";
    html += "<div class='ev-left'>";
    html += "<div class='dot' style='background:" + String(hexColor) + "; color:" + String(hexColor) + "'></div>";
    html += "<div><div class='ev-name'>" + String(events[i].name) + tag + "</div>";
    html += "<div class='ev-meta'>" + diasStr + "</div></div></div>";
    html += "<div class='action-btns'>";
    html += "<a class='edit-btn' href='#' onclick='openEditModal(" + String(i) + "); return false;' title='Editar'>&#9998;</a>";
    html += "<a class='del-btn' href='#' onclick='confirmDelete(" + String(i) + "); return false;' title='Eliminar'>&#128465;</a>";
    html += "</div></div>";
  }

  html += R"rawhtml(
  </div><!-- /event-list -->

  <div class="form-card">
    <h2>// AGREGAR ENTRADA</h2>

    <form action="/add" method="GET" onsubmit="return validateForm()">
      <input type="hidden" name="token" id="formToken">

      <label>Nombre</label>
      <input type="text" id="nombre" name="nombre" maxlength="30"
             placeholder="Ej: CUMPLE JUAN" required oninput="updateCount()">
      <div class="char-count"><span id="count">0</span>/30</div>

      <!-- CHECKBOX ES EVENTO -->
      <div class="check-row" onclick="toggleCheck()">
        <input type="checkbox" id="isEvent" name="isEvent" value="1" checked>
        <div>
          <div class="check-label">&#128197; Es un evento con fecha</div>
          <div class="check-sub">Desmarcá para mostrar solo el texto fijo</div>
        </div>
      </div>

      <!-- DATE PICKER (se deshabilita si no es evento) -->
      <div class="date-wrapper" id="dateWrapper">
        <label>Fecha del evento</label>
        <input type="date" id="fecha" name="fecha">
      </div>

      <label>Color</label>
      <div class="color-row">
        <input type="color" name="color" id="colorPicker" value="#00C8FF">
        <div class="presets">
          <div class="preset" style="background:#00C8FF" onclick="setColor('#00C8FF')" title="Celeste"></div>
          <div class="preset" style="background:#FF4040" onclick="setColor('#FF4040')" title="Rojo"></div>
          <div class="preset" style="background:#40FF40" onclick="setColor('#40FF40')" title="Verde"></div>
          <div class="preset" style="background:#FFD700" onclick="setColor('#FFD700')" title="Dorado"></div>
          <div class="preset" style="background:#FF69B4" onclick="setColor('#FF69B4')" title="Rosa"></div>
          <div class="preset" style="background:#FF8C00" onclick="setColor('#FF8C00')" title="Naranja"></div>
          <div class="preset" style="background:#FFFFFF" onclick="setColor('#FFFFFF')" title="Blanco"></div>
        </div>
      </div>

      <!-- CHECKBOX COLOR DINÁMICO -->
      <div class="dynamic-check" onclick="toggleDynamic()">
        <input type="checkbox" id="dynamicColor" name="dynamicColor" value="1">
        <div>
          <div class="dynamic-label">&#127912; Color dinámico</div>
          <div class="dynamic-sub">Cambia de color aleatorio al mostrar</div>
        </div>
      </div>

      <button class="submit-btn" type="submit">&#43; AGREGAR</button>
    </form>
  </div>

</div><!-- /wrap -->

<!-- MODAL EDITAR -->
<div class="modal" id="editModal">
  <div class="modal-content">
    <div class="modal-header">
      <div class="modal-title">&#9998; EDITAR EVENTO</div>
      <button class="modal-close" onclick="closeEditModal()">&times;</button>
    </div>
    <form class="modal-form" id="editForm">
      <input type="hidden" id="editIndex">
      
      <label>Nombre</label>
      <input type="text" id="editNombre" maxlength="30" required oninput="updateEditCount()">
      <div class="char-count"><span id="editCount">0</span>/30</div>

      <!-- CHECKBOX ES EVENTO -->
      <div class="modal-check-row" onclick="toggleEditCheck()">
        <input type="checkbox" id="editIsEvent" value="1">
        <div>
          <div class="modal-check-label">&#128197; Es un evento con fecha</div>
          <div class="modal-check-sub">Desmarcá para mostrar solo el texto fijo</div>
        </div>
      </div>

      <!-- DATE PICKER -->
      <div class="date-wrapper" id="editDateWrapper">
        <label>Fecha del evento</label>
        <input type="date" id="editFecha">
      </div>

      <label>Color</label>
      <div class="color-row">
        <input type="color" id="editColor" value="#00C8FF">
        <div class="presets">
          <div class="preset" style="background:#00C8FF" onclick="setEditColor('#00C8FF')" title="Celeste"></div>
          <div class="preset" style="background:#FF4040" onclick="setEditColor('#FF4040')" title="Rojo"></div>
          <div class="preset" style="background:#40FF40" onclick="setEditColor('#40FF40')" title="Verde"></div>
          <div class="preset" style="background:#FFD700" onclick="setEditColor('#FFD700')" title="Dorado"></div>
          <div class="preset" style="background:#FF69B4" onclick="setEditColor('#FF69B4')" title="Rosa"></div>
          <div class="preset" style="background:#FF8C00" onclick="setEditColor('#FF8C00')" title="Naranja"></div>
          <div class="preset" style="background:#FFFFFF" onclick="setEditColor('#FFFFFF')" title="Blanco"></div>
        </div>
      </div>

      <!-- CHECKBOX COLOR DINÁMICO -->
      <div class="modal-dynamic-check" onclick="toggleEditDynamic()">
        <input type="checkbox" id="editDynamicColor" value="1">
        <div>
          <div class="modal-dynamic-label">&#127912; Color dinámico</div>
          <div class="modal-dynamic-sub">Cambia de color aleatorio al mostrar</div>
        </div>
      </div>

      <div class="modal-actions">
        <button type="button" class="modal-btn modal-cancel" onclick="closeEditModal()">CANCELAR</button>
        <button type="submit" class="modal-btn modal-save">GUARDAR</button>
      </div>
    </form>
  </div>
</div>

<script>
  // Sincronización robusta de token entre URL y sessionStorage
  function getToken() {
    let token = sessionStorage.getItem('matrixToken');
    if (!token) {
      const urlParams = new URLSearchParams(window.location.search);
      token = urlParams.get('token');
      if (token) sessionStorage.setItem('matrixToken', token);
    }
    return token || '';
  }

  // Inyectar token en formulario al cargar
  document.addEventListener('DOMContentLoaded', function() {
    const tokenField = document.getElementById('formToken');
    if (tokenField) {
      tokenField.value = getToken();
    }
  });
  
  function logout() {
    fetch('/logout?token=' + encodeURIComponent(getToken()))
      .then(() => {
        sessionStorage.removeItem('matrixToken');
        window.location.href = '/';
      });
  }
  
  document.getElementById('logoutBtn').addEventListener('click', function(e) {
    e.preventDefault();
    logout();
  });
  
  // Auto-logout on token expiry check
  setInterval(function() {
    const token = getToken();
    if (token) {
      fetch('/?token=' + encodeURIComponent(token), { method: 'HEAD' })
        .then(function(res) {
          if (res.status === 401) {
            sessionStorage.removeItem('matrixToken');
            window.location.href = '/';
          }
        })
        .catch(function() {
          sessionStorage.removeItem('matrixToken');
          window.location.href = '/';
        });
    }
  }, 60000);
  
  // Función para confirmar y ejecutar eliminación
  function confirmDelete(index) {
    if (confirm('¿Eliminar este evento?')) {
      const token = getToken();
      window.location.href = '/delete?id=' + index + '&token=' + encodeURIComponent(token);
    }
  }
  
  // Form functions
  function updateCount() {
    document.getElementById('count').textContent =
      document.getElementById('nombre').value.length;
  }
  function setColor(hex) {
    document.getElementById('colorPicker').value = hex;
  }

  // Checkbox: habilitar / deshabilitar date picker
  function toggleCheck() {
    const cb      = document.getElementById('isEvent');
    cb.checked = !cb.checked;
    applyDateState();
  }
  document.getElementById('isEvent').addEventListener('click', function(e) {
    e.stopPropagation();
    applyDateState();
  });

  function applyDateState() {
    const cb      = document.getElementById('isEvent');
    const wrapper = document.getElementById('dateWrapper');
    const input   = document.getElementById('fecha');
    if (cb.checked) {
      wrapper.classList.remove('disabled');
      input.required = true;
    } else {
      wrapper.classList.add('disabled');
      input.required = false;
      input.value = '';
    }
  }
  
  // Dynamic color toggle
  function toggleDynamic() {
    const cb = document.getElementById('dynamicColor');
    cb.checked = !cb.checked;
  }

  function validateForm() {
    const cb = document.getElementById('isEvent');
    if (cb.checked && !document.getElementById('fecha').value) {
      alert('Ingresá una fecha para el evento.');
      return false;
    }
    return true;
  }

  // Estado inicial al cargar
  applyDateState();
  updateCount();
  
  // Modal functions
  function openEditModal(index) {
    fetch('/api/events?token=' + encodeURIComponent(getToken()))
      .then(res => res.json())
      .then(data => {
        const event = data.events[index];
        document.getElementById('editIndex').value = index;
        document.getElementById('editNombre').value = event.name;
        document.getElementById('editIsEvent').checked = event.isEvent;
        document.getElementById('editFecha').value = event.isEvent ? 
          (event.year + '-' + String(event.month).padStart(2,'0') + '-' + String(event.day).padStart(2,'0')) : '';
        document.getElementById('editColor').value = '#' + 
          event.r.toString(16).padStart(2,'0') + 
          event.g.toString(16).padStart(2,'0') + 
          event.b.toString(16).padStart(2,'0');
        document.getElementById('editDynamicColor').checked = event.dynamicColor || false;
        
        applyEditDateState();
        updateEditCount();
        document.getElementById('editModal').classList.add('active');
      });
  }
  
  function closeEditModal() {
    document.getElementById('editModal').classList.remove('active');
  }
  
  function updateEditCount() {
    document.getElementById('editCount').textContent =
      document.getElementById('editNombre').value.length;
  }
  
  function setEditColor(hex) {
    document.getElementById('editColor').value = hex;
  }
  
  function toggleEditCheck() {
    const cb = document.getElementById('editIsEvent');
    cb.checked = !cb.checked;
    applyEditDateState();
  }
  document.getElementById('editIsEvent').addEventListener('click', function(e) {
    e.stopPropagation();
    applyEditDateState();
  });
  
  function applyEditDateState() {
    const cb = document.getElementById('editIsEvent');
    const wrapper = document.getElementById('editDateWrapper');
    const input = document.getElementById('editFecha');
    if (cb.checked) {
      wrapper.classList.remove('disabled');
      input.required = true;
    } else {
      wrapper.classList.add('disabled');
      input.required = false;
      input.value = '';
    }
  }
  
  function toggleEditDynamic() {
    const cb = document.getElementById('editDynamicColor');
    cb.checked = !cb.checked;
  }
  
  // Handle edit form submit
  document.getElementById('editForm').addEventListener('submit', function(e) {
    e.preventDefault();
    
    const index = document.getElementById('editIndex').value;
    const nombre = document.getElementById('editNombre').value;
    const isEvent = document.getElementById('editIsEvent').checked;
    const fecha = document.getElementById('editFecha').value;
    const color = document.getElementById('editColor').value;
    const dynamicColor = document.getElementById('editDynamicColor').checked;
    
    let url = '/edit?index=' + index + 
              '&nombre=' + encodeURIComponent(nombre) +
              '&isEvent=' + (isEvent ? '1' : '0') +
              '&color=' + encodeURIComponent(color) +
              '&dynamicColor=' + (dynamicColor ? '1' : '0') +
              '&token=' + encodeURIComponent(getToken());
    
    if (isEvent && fecha) {
      url += '&fecha=' + encodeURIComponent(fecha);
    }
    
    fetch(url)
      .then(res => {
        if (res.ok) {
          closeEditModal();
          window.location.href = '/?token=' + encodeURIComponent(getToken());
        } else {
          alert('Error al guardar');
        }
      })
      .catch(err => {
        alert('Error de conexión');
      });
  });
  
  // Close modal on outside click
  document.getElementById('editModal').addEventListener('click', function(e) {
    if (e.target === this) {
      closeEditModal();
    }
  });
</script>
</body>
</html>
)rawhtml";

  server.send(200, "text/html", html);
}

// ============================================================
// API — obtener eventos para modal
// ============================================================
void handleApiEvents() {
  if (!checkToken()) {
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  
  String json = "{\"events\":[";
  for (int i = 0; i < totalEvents; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"name\":\"" + String(events[i].name) + "\",";
    json += "\"day\":" + String(events[i].day) + ",";
    json += "\"month\":" + String(events[i].month) + ",";
    json += "\"year\":" + String(events[i].year) + ",";
    json += "\"r\":" + String(events[i].r) + ",";
    json += "\"g\":" + String(events[i].g) + ",";
    json += "\"b\":" + String(events[i].b) + ",";
    json += "\"isEvent\":" + String(events[i].isEvent ? "true" : "false") + ",";
    json += "\"dynamicColor\":" + String(events[i].dynamicColor ? "true" : "false");
    json += "}";
  }
  json += "]}";
  
  server.send(200, "application/json", json);
}

// ============================================================
// WEB — agregar
// ============================================================
void handleAdd() {
  if (!checkToken()) {
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  
  if (totalEvents >= MAX_EVENTS) {
    server.sendHeader("Location", "/?token=" + authToken);
    server.send(302);
    return;
  }

  Event &e = events[totalEvents];
  strncpy(e.name, server.arg("nombre").c_str(), NAME_LEN);
  e.name[NAME_LEN - 1] = '\0';

  // ¿Es evento con fecha?
  e.isEvent = (server.arg("isEvent") == "1");

  if (e.isEvent) {
    String fecha = server.arg("fecha");
    if (fecha.length() == 10) {
      e.year  = fecha.substring(0, 4).toInt();
      e.month = fecha.substring(5, 7).toInt();
      e.day   = fecha.substring(8, 10).toInt();
    } else {
      e.day = e.month = e.year = 0;
    }
  } else {
    e.day = e.month = e.year = 0;
  }

  hexToRGB(server.arg("color"), e.r, e.g, e.b);
  
  // Dynamic color
  e.dynamicColor = (server.arg("dynamicColor") == "1");
  
  totalEvents++;
  saveEvents();

  // CORRECCIÓN CRÍTICA: Redirigir manteniendo el token en la URL
  server.sendHeader("Location", "/?token=" + authToken);
  server.send(302);
}

// ============================================================
// WEB — editar
// ============================================================
void handleEdit() {
  if (!checkToken()) {
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  
  int index = server.arg("index").toInt();
  if (index < 0 || index >= totalEvents) {
    server.send(400, "text/plain", "Invalid index");
    return;
  }
  
  Event &e = events[index];
  
  // Nombre
  if (server.hasArg("nombre")) {
    strncpy(e.name, server.arg("nombre").c_str(), NAME_LEN);
    e.name[NAME_LEN - 1] = '\0';
  }
  
  // IsEvent
  if (server.hasArg("isEvent")) {
    e.isEvent = (server.arg("isEvent") == "1");
  }
  
  // Fecha
  if (e.isEvent && server.hasArg("fecha")) {
    String fecha = server.arg("fecha");
    if (fecha.length() == 10) {
      e.year  = fecha.substring(0, 4).toInt();
      e.month = fecha.substring(5, 7).toInt();
      e.day   = fecha.substring(8, 10).toInt();
    }
  } else if (!e.isEvent) {
    e.day = e.month = e.year = 0;
  }
  
  // Color
  if (server.hasArg("color")) {
    hexToRGB(server.arg("color"), e.r, e.g, e.b);
  }
  
  // Dynamic color
  if (server.hasArg("dynamicColor")) {
    e.dynamicColor = (server.arg("dynamicColor") == "1");
  }
  
  saveEvents();
  server.send(200, "text/plain", "OK");
}

// ============================================================
// WEB — eliminar
// ============================================================
void handleDelete() {
  if (!checkToken()) {
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }
  
  int id = server.arg("id").toInt();
  if (id >= 0 && id < totalEvents) {
    for (int i = id; i < totalEvents - 1; i++) events[i] = events[i + 1];
    totalEvents--;
    saveEvents();
  }
  
  // CORRECCIÓN CRÍTICA: Redirigir manteniendo el token en la URL
  server.sendHeader("Location", "/?token=" + authToken);
  server.send(302);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  matrix.begin();
  matrix.setBrightness(BRIGHTNESS);
  matrix.setTextWrap(false);

  EEPROM.begin(EEPROM_SIZE);
  loadEvents();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32")) Serial.println("mDNS: http://esp32.local");

  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);

  // Random seed for token and dynamic colors
  randomSeed(analogRead(0));

  server.on("/",              handleRoot);
  server.on("/login",         handleLogin);
  server.on("/logout",        handleLogout);
  server.on("/add",           handleAdd);
  server.on("/edit",          handleEdit);
  server.on("/delete",        handleDelete);
  server.on("/api/events",    handleApiEvents);
  server.begin();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // Reconexión WiFi
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttempt > retryInterval) {
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      lastAttempt = millis();
    }
    server.handleClient();
    // Fuera de horario o sin WiFi: igual apagar LEDs
    if (!isActiveHour()) { matrix.fillScreen(0); matrix.show(); delay(500); return; }
    mostrarMensaje("WIFI...", matrix.Color(255, 0, 0));
    return;
  }

  server.handleClient();

  // ── FUERA DE HORARIO ACTIVO ──────────────────────────────
  // LEDs apagados, web sigue corriendo.
  if (!isActiveHour()) {
    matrix.fillScreen(0);
    matrix.show();
    delay(500);          // cede tiempo al servidor sin busy-loop
    return;
  }

  // ── HORARIO ACTIVO ───────────────────────────────────────
  if (totalEvents == 0) {
    mostrarMensaje("SIN EVENTOS", matrix.Color(50, 50, 50));
    return;
  }

  sortEvents();
  for (int i = 0; i < totalEvents; i++) {
    // Chequeamos horario entre eventos también
    if (!isActiveHour()) { matrix.fillScreen(0); matrix.show(); return; }
    displayEvent(i);
  }
}