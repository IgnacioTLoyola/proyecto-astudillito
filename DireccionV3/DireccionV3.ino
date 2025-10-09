// ==================================================================================
// CONTROL DE MOTORES L298N CON ESP32-CAM VIA WIFI (VERSIÓN COMPLETA CORREGIDA)
// ==================================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// ==================================================================================
// SECCIÓN 1: CONFIGURACIÓN DE RED Y PINES
// ==================================================================================

// Configuración WiFi
const char* ssid = "Asuncion5";     // ⚠ CAMBIAR por tu red WiFi
const char* password = "44462987";  // ⚠ CAMBIAR por tu contraseña

// Pines de conexión L298N a ESP32-CAM (revisá que estos pines sean compatibles con tu módulo)
#define PIN_MOTOR_A_DIR1 14
#define PIN_MOTOR_A_DIR2 15
#define PIN_MOTOR_B_DIR1 13
#define PIN_MOTOR_B_DIR2 12
#define PIN_LED 4

// Servidor web en puerto 80
WebServer servidor(80);

// ==================================================================================
// SECCIÓN 2: ESTRUCTURAS Y VARIABLES GLOBALES
// ==================================================================================
struct Coordenada {
  double x;
  double y;
};

struct DatosCalibracion {
  float tiempoMs;
  float grados;
};

Coordenada posicionInicial = { 0, 0 };
int gradosPorSegundo = 360;
float velocidadMetrosPorSegundo = 0.67f;

bool giroEnEjecucion = false;
unsigned long tiempoInicioGiro = 0;
const unsigned long DURACION_GIRO_TEMPORAL = 1000;

DatosCalibracion tablaCalibracion[] = {
  { 50, 15 },
  { 75, 19 },
  { 100, 25 },
  { 125, 33 },
  { 150, 40 },
  { 200, 55 },
  { 250, 70 },
  { 300, 92 },
  { 350, 115 },
  { 400, 130 },
};
const int tamanioTablaCalibracion = sizeof(tablaCalibracion) / sizeof(tablaCalibracion[0]);

// ===== Flags / comandos pendentes (para no bloquear en handlers HTTP) =====
bool movimientoEnCurso = false;

bool avanzarFlag = false;
double avanzarDistancia = 0.0;

bool giroFlag = false;
double giroGradosPendientes = 0.0;

bool navegarFlag = false;
Coordenada navegarDestino = { 0, 0 };
double navegarOrientacionInicial = 0;

bool ejecutarRutaFlag = false;
Coordenada rutaP1 = { 0, 0 }, rutaP2 = { 0, 0 };

// ==================================================================================
// SECCIÓN 3: CONTROL DE MOTORES (BAJO NIVEL)
// ==================================================================================
void avanzarAdelante() {
  digitalWrite(PIN_MOTOR_A_DIR1, LOW);
  digitalWrite(PIN_MOTOR_A_DIR2, HIGH);
  digitalWrite(PIN_MOTOR_B_DIR1, LOW);
  digitalWrite(PIN_MOTOR_B_DIR2, HIGH);
}
void retroceder() {
  digitalWrite(PIN_MOTOR_A_DIR1, HIGH);
  digitalWrite(PIN_MOTOR_A_DIR2, LOW);
  digitalWrite(PIN_MOTOR_B_DIR1, HIGH);
  digitalWrite(PIN_MOTOR_B_DIR2, LOW);
}
void girarSentidoHorario() {
  digitalWrite(PIN_MOTOR_A_DIR1, HIGH);
  digitalWrite(PIN_MOTOR_A_DIR2, LOW);
  digitalWrite(PIN_MOTOR_B_DIR1, LOW);
  digitalWrite(PIN_MOTOR_B_DIR2, HIGH);
}
void girarSentidoAntihorario() {
  digitalWrite(PIN_MOTOR_A_DIR1, LOW);
  digitalWrite(PIN_MOTOR_A_DIR2, HIGH);
  digitalWrite(PIN_MOTOR_B_DIR1, HIGH);
  digitalWrite(PIN_MOTOR_B_DIR2, LOW);
}
void detenerMotores() {
  digitalWrite(PIN_MOTOR_A_DIR1, LOW);
  digitalWrite(PIN_MOTOR_A_DIR2, LOW);
  digitalWrite(PIN_MOTOR_B_DIR1, LOW);
  digitalWrite(PIN_MOTOR_B_DIR2, LOW);
}
void alternarEstadoLuces() {
  int estadoActual = digitalRead(PIN_LED);
  digitalWrite(PIN_LED, !estadoActual);
}

// ==================================================================================
// SECCIÓN 4: MOVIMIENTO AVANZADO
// ==================================================================================
void avanzarDistanciaMetros(double metros) {
  if (metros <= 0) return;
  unsigned long duracionMovimientoMs = (metros / velocidadMetrosPorSegundo) * 1000;
  Serial.print("Avanzando ");
  Serial.print(metros);
  Serial.print(" m => ");
  Serial.print(duracionMovimientoMs);
  Serial.println(" ms");
  avanzarAdelante();
  delay(duracionMovimientoMs);
  detenerMotores();
}

void retrocederDistanciaMetros(double metros) {
  if (metros <= 0) return;
  unsigned long duracionMovimientoMs = (metros / velocidadMetrosPorSegundo) * 1000;
  retroceder();
  delay(duracionMovimientoMs);
  detenerMotores();
}

float calcularTiempoParaGrados(float gradosObjetivo) {
  for (int i = 0; i < tamanioTablaCalibracion - 1; i++) {
    if (gradosObjetivo >= tablaCalibracion[i].grados && gradosObjetivo <= tablaCalibracion[i + 1].grados) {
      float x1 = tablaCalibracion[i].grados;
      float y1 = tablaCalibracion[i].tiempoMs;
      float x2 = tablaCalibracion[i + 1].grados;
      float y2 = tablaCalibracion[i + 1].tiempoMs;
      return y1 + (y2 - y1) * ((gradosObjetivo - x1) / (x2 - x1));
    }
  }
  if (gradosObjetivo < tablaCalibracion[0].grados) return tablaCalibracion[0].tiempoMs;
  return tablaCalibracion[tamanioTablaCalibracion - 1].tiempoMs;
}

void girarAnguloAntihorario(double grados) {
  if (grados <= 0) return;
  if (gradosPorSegundo <= 0) {
    Serial.println("⚠️ gradosPorSegundo inválido");
    return;
  }
  double tiempoBaseMs = (grados * 1000.0) / (double)gradosPorSegundo;
  double tiempoGiroMs = (grados <= 130) ? calcularTiempoParaGrados(grados) : tiempoBaseMs;

  Serial.print("👉 Giro de ");
  Serial.print(grados);
  Serial.print("° | tiempo usado = ");
  Serial.print(tiempoGiroMs);
  Serial.println(" ms");

  girarSentidoAntihorario();
  delay((unsigned long)roundf(tiempoGiroMs));
  detenerMotores();
}

// ==================================================================================
// SECCIÓN 5: NAVEGACIÓN POR COORDENADAS
// ==================================================================================
double calcularDistanciaEntrePuntos(Coordenada origen, Coordenada destino) {
  double dx = destino.x - origen.x;
  double dy = destino.y - origen.y;
  return sqrt(dx * dx + dy * dy);
}
double calcularAnguloHaciaDestino(Coordenada origen, Coordenada destino) {
  double dx = destino.x - origen.x;
  double dy = destino.y - origen.y;
  return atan2(dy, dx) * (180.0 / M_PI);
}
double normalizarAngulo(double angulo) {
  while (angulo < 0) angulo += 360;
  while (angulo >= 360) angulo -= 360;
  return angulo;
}

double navegarHaciaCoordenada(Coordenada origen, Coordenada destino, double orientacionActual) {
  double distancia = calcularDistanciaEntrePuntos(origen, destino);
  if (distancia < 0.01) {
    Serial.println("⚠️ Distancia muy pequeña, omitiendo movimiento");
    return orientacionActual;
  }
  double anguloObjetivo = calcularAnguloHaciaDestino(origen, destino);
  double anguloGiro = anguloObjetivo - orientacionActual;
  anguloGiro = normalizarAngulo(anguloGiro);
  if (anguloGiro > 180) anguloGiro = anguloGiro - 360;

  Serial.print("📍 Navegando: distancia=");
  Serial.print(distancia);
  Serial.print(" m, orientación actual=");
  Serial.print(orientacionActual);
  Serial.print("°, ángulo objetivo=");
  Serial.print(anguloObjetivo);
  Serial.print("°, giro necesario=");
  Serial.print(anguloGiro);
  Serial.println("°");

  if (abs(anguloGiro) > 1) {
    girarAnguloAntihorario(abs(anguloGiro));
    delay(500);
  }
  avanzarDistanciaMetros(distancia);
  delay(500);
  return normalizarAngulo(anguloObjetivo);
}

void ejecutarRutaCompleta(Coordenada punto1, Coordenada punto2) {
  double orientacionActual = 0;
  Serial.println("🚀 Iniciando ruta completa");
  orientacionActual = navegarHaciaCoordenada(punto1, posicionInicial, orientacionActual);
  Serial.println("✓ Tramo 1 completado");
  orientacionActual = navegarHaciaCoordenada(posicionInicial, punto2, orientacionActual);
  Serial.println("✓ Tramo 2 completado");
  orientacionActual = navegarHaciaCoordenada(punto2, posicionInicial, orientacionActual);
  Serial.println("✓ Ruta completa finalizada");
}

// ==================================================================================
// SECCIÓN 6: HANDLERS HTTP (ENDPOINTS DE LA API)
// ==================================================================================
void manejarPaginaPrincipal() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Panel de Control - ESP32-CAM</title>
<style>
  body {
    font-family: "Consolas", "Courier New", monospace;
    background: #1a1f24;
    color: #e0e0e0;
    margin: 0;
    padding: 0;
  }
  .panel {
    max-width: 500px;
    margin: 30px auto;
    background: #2a3038;
    border: 2px solid #3f4a55;
    border-radius: 12px;
    box-shadow: 0 0 25px rgba(0,0,0,0.4);
    padding: 25px 30px;
  }
  h1 {
    text-align: center;
    color: #00b4d8;
    margin-bottom: 15px;
    font-size: 1.5em;
  }
  .status {
    text-align: center;
    background: #101417;
    border: 1px solid #3f4a55;
    border-radius: 6px;
    padding: 10px;
    font-weight: bold;
    color: #90ee90;
    margin-bottom: 20px;
  }

  /* Botones */
  button {
    border: none;
    border-radius: 8px;
    color: #fff;
    font-size: 15px;
    font-weight: bold;
    cursor: pointer;
    padding: 14px;
    transition: transform 0.05s, background 0.15s;
  }
  button:active { transform: scale(0.96); }

  .btn-adelante { background: #0077b6; }
  .btn-atras { background: #f8961e; }
  .btn-horario { background: #007f5f; }
  .btn-antihorario { background: #9d0208; }
  .btn-parar { background: #d62828; width: 80px; }
  .btn-luces { background: #4361ee; width: 100%; margin-top: 10px; }
  .btn-config { background: #adb5bd; color: #222; width: 100%; }

  input[type=number] {
    width: 70%;
    padding: 10px;
    border-radius: 6px;
    border: 1px solid #555;
    background: #181c20;
    color: #fff;
    margin: 4px 0;
    text-align: center;
  }

  .section {
    background: #21262b;
    border: 1px solid #3f4a55;
    border-radius: 8px;
    padding: 12px 15px;
    margin-top: 18px;
  }
  .section h3 {
    color: #00b4d8;
    border-bottom: 1px solid #3f4a55;
    padding-bottom: 5px;
    font-size: 1em;
    text-align: center;
  }

  /* Control manual estilo cruceta */
  .control-grid {
    display: grid;
    grid-template-columns: 80px 80px 80px;
    grid-template-rows: 80px 80px 80px;
    justify-content: center;
    align-items: center;
    gap: 5px;
  }
  .control-grid button { width: 80px; height: 80px; }

  .footer {
    text-align: center;
    font-size: 0.8em;
    margin-top: 15px;
    color: #666;
  }
</style>
</head>
<body>
<div class="panel">
  <h1>⚙️ PANEL DE CONTROL - ESP32</h1>
  <div class="status" id="status">Sistema listo</div>

  <!-- Control manual tipo cruceta -->
  <div class="section">
    <h3>🎮 Control Manual</h3>
    <div class="control-grid">
      <div></div>
      <button class="btn-adelante" 
        onmousedown="iniciarMovimiento('adelante')" onmouseup="detener()" 
        ontouchstart="iniciarMovimiento('adelante')" ontouchend="detener()">⬆</button>
      <div></div>

      <button class="btn-antihorario" 
        onmousedown="iniciarMovimiento('antihorario')" onmouseup="detener()" 
        ontouchstart="iniciarMovimiento('antihorario')" ontouchend="detener()">⬅</button>

      <button class="btn-parar" onclick="detener()">⛔</button>

      <button class="btn-horario" 
        onmousedown="iniciarMovimiento('horario')" onmouseup="detener()" 
        ontouchstart="iniciarMovimiento('horario')" ontouchend="detener()">➡</button>

      <div></div>
      <button class="btn-atras" 
        onmousedown="iniciarMovimiento('atras')" onmouseup="detener()" 
        ontouchstart="iniciarMovimiento('atras')" ontouchend="detener()">⬇</button>
      <div></div>
    </div>

    <button class="btn-luces" onclick="enviarComando('luces')">💡 LUCES</button>
  </div>

  <!-- Sección Configuración -->
  <div class="section">
    <h3>⚙️ Configuración</h3>
    <input type="number" id="grados" value="360" placeholder="Grados por segundo">
    <button class="btn-config" onclick="configurarGradosPorSegundo()">Actualizar</button>

    <div style="display:flex; justify-content:space-between; margin-top:10px;">
      <button style="flex:1; margin-right:5px;" class="btn-adelante" onclick="enviarComando('avanzarSegundo')">🚗 Avanzar 1 s</button>
      <button style="flex:1; margin-left:5px;" class="btn-horario" onclick="enviarComando('antihorarioSegundo')">⟲ Girar 1 s</button>
    </div>

    <div style="margin-top:10px;">
      <input type="number" id="gradosAntihorario" placeholder="Grados (ej: 45)">
      <button class="btn-antihorario" onclick="girarGrados()">⟲ Girar Preciso</button>
    </div>
  </div>

  <!-- Sección Navegación -->
  <div class="section">
    <h3>🧭 Navegación</h3>

    <div>
      <h4 style="text-align:center;margin:8px 0;">Navegación por Coordenadas</h4>
      <input type="number" id="coordX" placeholder="X (m)">
      <input type="number" id="coordY" placeholder="Y (m)">
      <button class="btn-adelante" onclick="navegarAPunto()">🚀 IR AL PUNTO</button>
    </div>

    <div>
      <h4 style="text-align:center;margin:8px 0;">Ruta Completa (2 Puntos)</h4>
      <input type="number" id="x1" placeholder="X1 (m)">
      <input type="number" id="y1" placeholder="Y1 (m)">
      <input type="number" id="x2" placeholder="X2 (m)">
      <input type="number" id="y2" placeholder="Y2 (m)">
      <button class="btn-horario" onclick="ejecutarRuta()">🧭 Ejecutar Ruta</button>
    </div>

    <div>
      <h4 style="text-align:center;margin:8px 0;">Movimiento por Distancia</h4>
      <input type="number" id="metros" placeholder="Metros a avanzar">
      <button class="btn-adelante" onclick="avanzarMetros()">🚗 Avanzar</button>
    </div>
  </div>

  <div class="footer">v1.1 - Control Industrial WiFi | ESP32-CAM</div>
</div>

<script>
function enviarComando(accion) {
  fetch('/' + accion)
    .then(r => r.text())
    .then(data => { document.getElementById('status').textContent = data; })
    .catch(_ => { document.getElementById('status').textContent = 'Error de conexión'; });
}
function iniciarMovimiento(a){ enviarComando(a); }
function detener(){ enviarComando('parar'); }
function avanzarMetros(){
  let m=document.getElementById("metros").value;
  if(m>0) fetch('/avanzarMetros?distancia='+m).then(r=>r.text()).then(t=>status.textContent=t);
}
function configurarGradosPorSegundo(){
  let g=document.getElementById("grados").value;
  if(g>0) fetch('/configurarGradosPorSegundo?valor='+g).then(r=>r.text()).then(t=>status.textContent=t);
}
function girarGrados(){
  let g=document.getElementById("gradosAntihorario").value;
  if(g>0) fetch('/girarGrados?angulo='+g).then(r=>r.text()).then(t=>status.textContent=t);
}
function navegarAPunto(){
  let x=document.getElementById("coordX").value, y=document.getElementById("coordY").value;
  if(x&&y) fetch(`/navegarAPunto?x=${x}&y=${y}`).then(r=>r.text()).then(t=>status.textContent=t);
}
function ejecutarRuta(){
  let x1=document.getElementById("x1").value;
  let y1=document.getElementById("y1").value;
  let x2=document.getElementById("x2").value;
  let y2=document.getElementById("y2").value;
  if(x1&&y1&&x2&&y2){
    fetch(`/ejecutarRuta?x1=${x1}&y1=${y1}&x2=${x2}&y2=${y2}`)
      .then(r=>r.text())
      .then(t=>status.textContent=t)
      .catch(_=>status.textContent="⚠️ Error al enviar ruta");
  } else status.textContent="❗ Ingresá todos los valores (X1,Y1,X2,Y2)";
}
</script>
</body>
</html>

)rawliteral";
  servidor.send(200, "text/html", html);
}

// Handlers básicos (hacen set de flags o acciones directas)
void manejarComandoAdelante() {
  avanzarAdelante();
  Serial.println("🚗 Adelante");
  servidor.send(200, "text/plain", "🚗 Adelante");
}
void manejarComandoAtras() {
  retroceder();
  Serial.println("🚙 Atrás");
  servidor.send(200, "text/plain", "🚙 Atrás");
}
void manejarComandoGiroHorario() {
  girarSentidoHorario();
  Serial.println("➡️ Giro horario");
  servidor.send(200, "text/plain", "➡️ Giro horario");
}
void manejarComandoGiroAntihorario() {
  girarSentidoAntihorario();
  Serial.println("⬅️ Giro antihorario");
  servidor.send(200, "text/plain", "⬅️ Giro antihorario");
}
void manejarComandoDetener() {
  detenerMotores();
  Serial.println("🛑 Motores detenidos");
  servidor.send(200, "text/plain", "🛑 Motores detenidos");
}
void manejarComandoLuces() {
  alternarEstadoLuces();
  int estado = digitalRead(PIN_LED);
  String m = estado ? "💡 Luces ENCENDIDAS" : "💡 Luces APAGADAS";
  Serial.println(m);
  servidor.send(200, "text/plain", m);
}

void manejarComandoGiroTemporalAntihorario() {
  girarSentidoAntihorario();

  delay(1000);
  Serial.println("⬅️ Giro antihorario");
  servidor.send(200, "text/plain", "⬅️ Giro antihorario 1 seg");
}

void manejarComandoAdelanteTemporalAdelante() {
  avanzarAdelante();

  delay(1000);
  Serial.println("🚗 Adelante");
  servidor.send(200, "text/plain", "🚗 Adelante 1 seg");
}


// Ruta: Avanzar X metros (no bloqueante: setea flag)
void registrarAvanzar() {
  if (!servidor.hasArg("distancia")) {
    servidor.send(400, "text/plain", "⚠️ Falta parámetro 'distancia'");
    return;
  }
  if (movimientoEnCurso) {
    servidor.send(409, "text/plain", "⚠️ Robot ocupado");
    return;
  }
  avanzarDistancia = servidor.arg("distancia").toDouble();
  avanzarFlag = true;
  servidor.send(200, "text/plain", "🚗 Comando avanzar recibido: " + String(avanzarDistancia) + " m");
}

// Ruta: Configurar grados por segundo (instantáneo)
void registrarConfigGrados() {
  if (!servidor.hasArg("valor")) {
    servidor.send(400, "text/plain", "⚠️ Falta parámetro 'valor'");
    return;
  }
  gradosPorSegundo = servidor.arg("valor").toInt();
  servidor.send(200, "text/plain", "✅ Grados por segundo actualizados a " + String(gradosPorSegundo));
  Serial.println("Nueva velocidad de giro: " + String(gradosPorSegundo) + " grados/segundo");
}

// Ruta: Girar X grados (no bloqueante: setea flag)
void registrarGirarGrados() {
  if (!servidor.hasArg("angulo")) {
    servidor.send(400, "text/plain", "⚠️ Falta parámetro 'angulo'");
    return;
  }
  if (movimientoEnCurso) {
    servidor.send(409, "text/plain", "⚠️ Robot ocupado");
    return;
  }
  giroGradosPendientes = servidor.arg("angulo").toDouble();
  giroFlag = true;
  servidor.send(200, "text/plain", "⟲ Giro solicitado: " + String(giroGradosPendientes) + "°");
}

// Ruta: Navegar a un punto (no bloqueante)
void registrarNavegarAPunto() {
  if (!(servidor.hasArg("x") && servidor.hasArg("y"))) {
    servidor.send(400, "text/plain", "⚠️ Faltan parámetros 'x' o 'y'");
    return;
  }
  if (movimientoEnCurso) {
    servidor.send(409, "text/plain", "⚠️ Robot ocupado");
    return;
  }
  navegarDestino.x = servidor.arg("x").toDouble();
  navegarDestino.y = servidor.arg("y").toDouble();
  navegarFlag = true;
  servidor.send(200, "text/plain", "🚗 Navegación solicitada a (" + String(navegarDestino.x) + "," + String(navegarDestino.y) + ")");
}

// Ruta: Ejecutar ruta completa (no bloqueante)
void registrarEjecutarRuta() {
  if (!(servidor.hasArg("x1") && servidor.hasArg("y1") && servidor.hasArg("x2") && servidor.hasArg("y2"))) {
    servidor.send(400, "text/plain", "⚠️ Faltan parámetros de la ruta");
    return;
  }
  if (movimientoEnCurso) {
    servidor.send(409, "text/plain", "⚠️ Robot ocupado");
    return;
  }
  rutaP1.x = servidor.arg("x1").toDouble();
  rutaP1.y = servidor.arg("y1").toDouble();
  rutaP2.x = servidor.arg("x2").toDouble();
  rutaP2.y = servidor.arg("y2").toDouble();
  ejecutarRutaFlag = true;
  servidor.send(200, "text/plain", "🧭 Ruta recibida: (" + String(rutaP1.x) + "," + String(rutaP1.y) + ") -> (" + String(rutaP2.x) + "," + String(rutaP2.y) + ")");
}

// ==================================================================================
// SECCIÓN 7: CONFIG INICIAL (SETUP)
// ==================================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32-CAM Control de Motores ===");

  // Pines
  pinMode(PIN_MOTOR_A_DIR1, OUTPUT);
  pinMode(PIN_MOTOR_A_DIR2, OUTPUT);
  pinMode(PIN_MOTOR_B_DIR1, OUTPUT);
  pinMode(PIN_MOTOR_B_DIR2, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  detenerMotores();
  digitalWrite(PIN_LED, LOW);

  // WiFi
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi conectado!");
  Serial.print("📱 Dirección IP: ");
  Serial.println(WiFi.localIP());

  // Rutas
  servidor.on("/", manejarPaginaPrincipal);
  servidor.on("/adelante", manejarComandoAdelante);
  servidor.on("/atras", manejarComandoAtras);
  servidor.on("/horario", manejarComandoGiroHorario);
  servidor.on("/antihorario", manejarComandoGiroAntihorario);
  servidor.on("/parar", manejarComandoDetener);
  servidor.on("/luces", manejarComandoLuces);
  servidor.on("/avanzarSegundo", manejarComandoAdelanteTemporalAdelante);
  servidor.on("/antihorarioSegundo", manejarComandoGiroTemporalAntihorario);

  servidor.on("/avanzarMetros", registrarAvanzar);
  servidor.on("/configurarGradosPorSegundo", registrarConfigGrados);
  servidor.on("/girarGrados", registrarGirarGrados);
  servidor.on("/navegarAPunto", registrarNavegarAPunto);
  servidor.on("/ejecutarRuta", registrarEjecutarRuta);

  servidor.begin();
  Serial.println("🌐 Servidor web iniciado correctamente");
  Serial.print("Accedé desde: http://");
  Serial.println(WiFi.localIP());
}

// ==================================================================================
// SECCIÓN 8: BUCLE PRINCIPAL (LOOP)
// ==================================================================================
void loop() {
  servidor.handleClient();

  // Control temporal de giros por 1 segundo (handlers horarioSegundo/antihorarioSegundo usan esta lógica)
  if (giroEnEjecucion && (millis() - tiempoInicioGiro >= DURACION_GIRO_TEMPORAL)) {
    detenerMotores();
    giroEnEjecucion = false;
    Serial.println("⏹ Giro temporal completado automáticamente");
  }

  // Si no hay movimiento en curso, ejecutamos el siguiente comando pendiente (prioridad: giro, avanzar, navegar, ruta)
  if (!movimientoEnCurso) {
    if (giroFlag) {
      movimientoEnCurso = true;
      Serial.println("Ejecutando giro pendiente...");
      girarAnguloAntihorario(giroGradosPendientes);
      giroFlag = false;
      movimientoEnCurso = false;
    } else if (avanzarFlag) {
      movimientoEnCurso = true;
      Serial.println("Ejecutando avance pendiente...");
      avanzarDistanciaMetros(avanzarDistancia);
      avanzarFlag = false;
      movimientoEnCurso = false;
    } else if (navegarFlag) {
      movimientoEnCurso = true;
      Serial.println("Ejecutando navegación hacia punto...");
      navegarHaciaCoordenada(posicionInicial, navegarDestino, navegarOrientacionInicial);
      navegarFlag = false;
      movimientoEnCurso = false;
    } else if (ejecutarRutaFlag) {
      movimientoEnCurso = true;
      Serial.println("Ejecutando ruta completa pendiente...");
      ejecutarRutaCompleta(rutaP1, rutaP2);
      ejecutarRutaFlag = false;
      movimientoEnCurso = false;
    }
  }
}
