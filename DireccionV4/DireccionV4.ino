// ================================================================
// 🚗 Control de motores L298N con ESP32-CAM via WiFi
// Autor: (Tu nombre)
// Descripción: Control de movimiento, giros y navegación por coordenadas
// ================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// ========== CONFIGURACIÓN WIFI ==========
const char* ssid = "HUAWEI-2.4G-Q5ug";     // ⚠ CAMBIAR
const char* password = "Astkw8Vt";  // ⚠ CAMBIAR

// Pines de conexión L298N a ESP32-CAM
#define IN1 14  // Motor A - Dirección 1
#define IN2 15  // Motor A - Dirección 2
#define IN3 13  // Motor B - Dirección 1
#define IN4 12  // Motor B - Dirección 2
#define LED 4

struct Punto {
  public:
    double x;
    double y;
};

Punto inicial = {0, 0};  // punto inicial en (0,0)

int gradosXseg = 360;
float velocidadRobot = 0.67f;

WebServer server(80);
unsigned long tiempoInicio = 0;

// ================== CONFIGURACIÓN ==================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32-CAM Control de Motores ===");

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(LED, OUTPUT);

  pararMotores();
  digitalWrite(LED, LOW);

  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi conectado!");
  Serial.print("📱 Dirección IP: ");
  Serial.println(WiFi.localIP());

  // ======== RUTAS ========
  server.on("/", handleRoot);
  server.on("/adelante", handleAdelante);
  server.on("/atras", handleAtras);
  server.on("/horario", handleHorario);
  server.on("/antihorario", handleAntihorario);
  server.on("/parar", handleParar);
  server.on("/luces", handleLuces);
  server.on("/adelanteMetros", []() {
    if (server.hasArg("dist")) {
      float metros = server.arg("dist").toFloat();
      moverAdelanteMetros(metros);
      server.send(200, "text/plain", "🚗 Avanzando " + String(metros) + " m");
    } else {
      server.send(400, "text/plain", "Falta parametro dist (ej: /adelanteMetros?dist=2.5)");
    }
  });

  server.on("/setGrados", []() {
    if (server.hasArg("valor")) {
      gradosXseg = server.arg("valor").toInt();
      server.send(200, "text/plain", "✅ gradosXseg actualizado a " + String(gradosXseg));
    } else {
      server.send(400, "text/plain", "⚠️ Falta parámetro valor (ej: /setGrados?valor=360)");
    }
  });

  server.on("/girarGrados", []() {
    if (server.hasArg("g")) {
      float grados = server.arg("g").toFloat();
      girarAntiHorarioGrado(grados);
      server.send(200, "text/plain", "⟳ Girando " + String(grados) + " grados antihorario");
    } else {
      server.send(400, "text/plain", "⚠️ Falta parámetro g (ej: /girarGrados?g=45)");
    }
  });

  server.on("/viajarPunto", []() {
    if (server.hasArg("x") && server.hasArg("y")) {
      double x = server.arg("x").toDouble();
      double y = server.arg("y").toDouble();
      Punto destino = {x, y};
      handleViajarPunto(destino, inicial, 0);
      server.send(200, "text/plain", "🚗 Moviéndose al punto (" + String(x) + "," + String(y) + ")");
    } else {
      server.send(400, "text/plain", "⚠️ Falta parámetro x o y");
    }
  });

  server.on("/cebar", []() {
    if (server.hasArg("x1") && server.hasArg("y1") && server.hasArg("x2") && server.hasArg("y2")) {
      Punto p1 = {server.arg("x1").toDouble(), server.arg("y1").toDouble()};
      Punto p2 = {server.arg("x2").toDouble(), server.arg("y2").toDouble()};
      cebarMate2p(p1, p2);
      server.send(200, "text/plain", "🚗 Viaje completado: (" +
                  String(p1.x) + "," + String(p1.y) + ") -> (" +
                  String(p2.x) + "," + String(p2.y) + ")");
    } else {
      server.send(400, "text/plain", "⚠️ Faltan parámetros: x1,y1,x2,y2");
    }
  });

  server.begin();
  Serial.println("🌐 Servidor HTTP iniciado");
}

// ================== INTERFAZ WEB ==================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<title>ESP32-CAM Control de Motores</title>
<style>
  body { font-family: Arial; background:#f9f9f9; text-align:center; padding:20px; }
  h1 { color:#333; }
  button {
    padding:15px 25px; margin:10px; font-size:16px; border:none;
    border-radius:10px; cursor:pointer; color:#fff;
  }
  .btn-adelante { background:#4CAF50; }
  .btn-atras { background:#f44336; }
  .btn-horario { background:#2196F3; }
  .btn-antihorario { background:#9C27B0; }
  .btn-parar { background:#555; }
  .btn-luces { background:#FF9800; }
  #status { margin-top:20px; font-weight:bold; color:#333; }
  input { padding:10px; border-radius:10px; border:1px solid #ccc; margin:5px; }
</style>
</head>
<body>

<h1>🚗 Control ESP32-CAM</h1>

<div>
  <button class="btn-adelante" onclick="accion('adelante')">⬆️ Adelante</button><br>
  <button class="btn-horario" onclick="accion('horario')">↻ Horario</button>
  <button class="btn-parar" onclick="accion('parar')">⛔ Parar</button>
  <button class="btn-antihorario" onclick="accion('antihorario')">↺ Antihorario</button><br>
  <button class="btn-atras" onclick="accion('atras')">⬇️ Atrás</button><br>
  <button class="btn-luces" onclick="accion('luces')">💡 Luces</button>
</div>

<hr>

<h3>🧭 Control avanzado</h3>

<div>
  <input type="number" id="dist" placeholder="Distancia (m)">
  <button class="btn-adelante" onclick="adelanteMetros()">➡️ Avanzar</button>
</div>

<div style="margin-top:20px;">
  <input type="number" id="grados" placeholder="Grados (°)">
  <button class="btn-antihorario" onclick="girarGrados()">⟲ Girar</button>
</div>

<!-- 🚀 NUEVO CONTROL: VIAJE POR COORDENADAS -->
<div style="margin-top:30px;">
  <h3>📍 Viaje a un punto</h3>
  <input type="number" id="coordX" placeholder="X (m)">
  <input type="number" id="coordY" placeholder="Y (m)">
  <button class="btn-adelante" onclick="viajarPunto()">🚀 Ir al punto</button>
</div>

<div style="margin-top:30px;">
  <h3>📍 Cebar</h3>
  <input type="number" id="x1" placeholder="X1 (m)">
  <input type="number" id="y1" placeholder="Y1 (m)"><br>
  <input type="number" id="x2" placeholder="X2 (m)">
  <input type="number" id="y2" placeholder="Y2 (m)"><br>
  <button class="btn-adelante" onclick="cebar()">🚀 Viajar</button>
</div>

<p id="status">Estado: Esperando orden...</p>

<script>
function accion(cmd) {
  fetch('/' + cmd)
    .then(r => r.text())
    .then(t => document.getElementById('status').textContent = t);
}

function adelanteMetros() {
  let d = document.getElementById("dist").value;
  fetch(`/adelanteMetros?dist=${d}`)
    .then(r => r.text())
    .then(t => document.getElementById('status').textContent = t);
}

function girarGrados() {
  let g = document.getElementById("grados").value;
  fetch(`/girarGrados?g=${g}`)
    .then(r => r.text())
    .then(t => document.getElementById('status').textContent = t);
}

function viajarPunto() {
  let x = parseFloat(document.getElementById("coordX").value);
  let y = parseFloat(document.getElementById("coordY").value);
  if (!isNaN(x) && !isNaN(y)) {
    fetch(`/viajarPunto?x=${x}&y=${y}`)
      .then(r => r.text())
      .then(data => document.getElementById('status').textContent = data);
  } else {
    document.getElementById('status').textContent = "⚠️ Ingresá valores válidos";
  }
}

function cebar() {
  let x1 = parseFloat(document.getElementById("x1").value);
  let y1 = parseFloat(document.getElementById("y1").value);
  let x2 = parseFloat(document.getElementById("x2").value);
  let y2 = parseFloat(document.getElementById("y2").value);
  if (!isNaN(x1) && !isNaN(y1) && !isNaN(x2) && !isNaN(y2)) {
    fetch(`/cebar?x1=${x1}&y1=${y1}&x2=${x2}&y2=${y2}`)
      .then(r => r.text())
      .then(data => document.getElementById('status').textContent = data);
  } else {
    document.getElementById('status').textContent = "⚠️ Ingresá valores válidos";
  }
}
</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

// ================== FUNCIONES MOTORES ==================
void irAtras() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void irAdelante() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void girarHorario() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void girarAntihorario() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void pararMotores() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// ================== FUNCIONES DE MOVIMIENTO ==================
void moverAdelanteMetros(double metros) {
  unsigned long duracionMovimiento = (metros / velocidadRobot) * 1000;
  irAdelante();
  delay(duracionMovimiento);
  pararMotores();
}

void moverAtrasMetros(double metros) {
  unsigned long duracionMovimiento = (metros / velocidadRobot) * 1000;
  irAtras();
  delay(duracionMovimiento);
  pararMotores();
}

// Tabla de calibración tiempo vs grados
struct PuntoVector {
  float t;
  float grados;
};

PuntoVector tabla[] = {
  {50, 15}, {75, 19}, {100, 25}, {125, 33}, {150, 40},
  {200, 55}, {250, 70}, {300, 92}, {350, 115}, {400, 130}
};
const int nTabla = sizeof(tabla) / sizeof(tabla[0]);

float tiempoPorGrados(float gradosDeseados) {
  for (int i = 0; i < nTabla - 1; i++) {
    if (gradosDeseados >= tabla[i].grados && gradosDeseados <= tabla[i + 1].grados) {
      float x1 = tabla[i].grados, y1 = tabla[i].t;
      float x2 = tabla[i + 1].grados, y2 = tabla[i + 1].t;
      return y1 + (y2 - y1) * ((gradosDeseados - x1) / (x2 - x1));
    }
  }
  if (gradosDeseados < tabla[0].grados) return tabla[0].t;
  return tabla[nTabla - 1].t;
}

void girarAntiHorarioGrado(double grados) {
  if (grados <= 0 || gradosXseg <= 0) return;

  double base = (grados * 1000.0) / gradosXseg;
  double msGiro = (grados <= 130) ? tiempoPorGrados(grados) : base;

  Serial.print("👉 Giro de ");
  Serial.print(grados);
  Serial.print("° | tiempo = ");
  Serial.println(msGiro);

  girarAntihorario();
  delay((unsigned long)roundf(msGiro));
  pararMotores();
}

// ================== FUNCIONES DE COORDENADAS ==================
double handleViajarPunto(Punto puntoFinal, Punto puntoInicial, double anguloAnt) {
  double dx = puntoFinal.x - puntoInicial.x;
  double dy = puntoFinal.y - puntoInicial.y;
  double anguloGiro = atan2(dy, dx) * (180.0 / M_PI);

  //if (anguloGiro < 0) anguloGiro += 360;

  girarAntiHorarioGrado(fabs(anguloGiro - anguloAnt));
  delay(500);

  double distancia = sqrt(dx * dx + dy * dy);
  moverAdelanteMetros(distancia);
  delay(5000);
  moverAtrasMetros(distancia);

  return anguloGiro;
}

void cebarMate2p(Punto punto1, Punto punto2) {
  double anguloActual = 0;
  anguloActual = handleViajarPunto(punto1, inicial, anguloActual);
  delay(200);
  anguloActual = handleViajarPunto(punto2, inicial, anguloActual);
  delay(200);
}

// ================== HANDLERS ==================
void handleAdelante() { irAdelante(); server.send(200, "text/plain", "⬆️ Adelante"); }
void handleAtras() { irAtras(); server.send(200, "text/plain", "⬇️ Atrás"); }
void handleHorario() { girarHorario(); server.send(200, "text/plain", "➡️ Giro horario"); }
void handleAntihorario() { girarAntihorario(); server.send(200, "text/plain", "⬅️ Giro antihorario"); }
void handleParar() { pararMotores(); server.send(200, "text/plain", "⛔ Motores detenidos"); }

void handleLuces() {
  digitalWrite(LED, !digitalRead(LED));
  String mensaje = (digitalRead(LED)) ? "💡 Luces encendidas" : "🌑 Luces apagadas";
  Serial.println(mensaje);
  server.send(200, "text/plain", mensaje);
}

void loop() {
  server.handleClient();
}
