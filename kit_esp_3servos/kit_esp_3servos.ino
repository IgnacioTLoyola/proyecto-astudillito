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
const char* ssid = "HUAWEI-2.4G-Q5ug";     // ⚠ CAMBIAR por tu red WiFi
const char* password = "Astkw8Vt";         // ⚠ CAMBIAR por tu contraseña

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
// SECCIÓN 2B: VARIABLES GLOBALES DE COORDENADAS GUARDADAS (sin timestamp)
// ==================================================================================
#define MAX_COORDENADAS_GUARDADAS 20

struct CoordenadaGuardada {
  double x;
  double y;
};

CoordenadaGuardada coordenadasGuardadas[MAX_COORDENADAS_GUARDADAS];
int totalCoordenadasGuardadas = 0;

void guardarCoordenada(double x, double y) {
  if (totalCoordenadasGuardadas >= MAX_COORDENADAS_GUARDADAS)
    totalCoordenadasGuardadas = 0; // sobrescribe las viejas (modo circular)

  coordenadasGuardadas[totalCoordenadasGuardadas++] = { x, y };
  Serial.printf("💾 Coordenada guardada #%d: (%.2f, %.2f)\n", totalCoordenadasGuardadas, x, y);
}

String listarCoordenadasJSON() {
  String json = "[";
  for (int i = 0; i < totalCoordenadasGuardadas; i++) {
    json += "{";
    json += "\"x\":" + String(coordenadasGuardadas[i].x, 3) + ",";
    json += "\"y\":" + String(coordenadasGuardadas[i].y, 3);
    json += "}";
    if (i < totalCoordenadasGuardadas - 1) json += ",";
  }
  json += "]";
  return json;
}

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
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Panel de Control - ESP32-CAM</title>
<style>
  body{font-family:ui-monospace,Consolas,"Courier New",monospace;background:#1a1f24;color:#e0e0e0;margin:0}
  .panel{max-width:960px;margin:24px auto;background:#2a3038;border:2px solid #3f4a55;border-radius:12px;box-shadow:0 0 25px rgba(0,0,0,.4);padding:20px}
  h1{color:#00b4d8;margin:0 0 10px;text-align:center}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}
  .section{background:#21262b;border:1px solid #3f4a55;border-radius:10px;padding:12px}
  .section h3{color:#00b4d8;border-bottom:1px solid #3f4a55;padding-bottom:6px;margin:0 0 10px;text-align:center;font-size:1.05rem}
  .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
  label{font-size:.9rem;opacity:.9}
  input[type=number]{width:120px;padding:8px;border-radius:8px;border:1px solid #555;background:#181c20;color:#fff;text-align:center}
  button{border:none;border-radius:10px;color:#fff;font-weight:700;padding:10px 12px;cursor:pointer}
  .btn{background:#3a86ff}
  .btn.clear{background:#d62828}
  .btn.copy{background:#38b000}
  .btn.save{background:#6a4c93}
  .btn:active{transform:scale(.98)}
  .status{background:#101417;border:1px solid #3f4a55;border-radius:8px;padding:10px;margin:10px 0;color:#90ee90;text-align:center}
  canvas{width:100%;height:auto;background:#0f1317;border:1px solid #3f4a55;border-radius:8px;touch-action:none}
  table{width:100%;border-collapse:collapse;font-size:.92rem}
  th,td{border-bottom:1px solid #3f4a55;padding:6px 8px;text-align:right}
  th{text-align:center;color:#9bd0ff}
  .muted{opacity:.8;font-size:.85rem}
  @media(max-width:900px){.grid{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="panel">
  <h1>⚙️ PANEL DE CONTROL - ESP32</h1>

  <div class="grid">
    <!-- ========= Sección: Mesa + Canvas ========= -->
    <div class="section">
      <h3>📐 Mesa (metros) → Marcado de puntos</h3>
      <div class="row" style="justify-content:space-between">
        <div class="row">
          <label>Ancho (X):</label>
          <input type="number" id="mesaAncho" step="0.01" min="0.01" value="2.00">
          <label>Alto (Y):</label>
          <input type="number" id="mesaAlto" step="0.01" min="0.01" value="1.20">
          <button class="btn" id="btnAplicar">Aplicar</button>
        </div>
        <div class="muted">Origen: <b>esquina inferior izquierda</b></div>
      </div>

      <div class="status" id="statusMesa">Mesa: 2.00 m × 1.20 m</div>
      <canvas id="canvas" width="900" height="540"></canvas>
      <div class="muted" style="margin-top:6px">Click/tap para agregar · Shift+click para borrar · Cebador fijo en (0, 0)</div>
    </div>

    <!-- ========= Sección: Puntos ========= -->
    <div class="section">
      <h3>📍 Puntos (m)</h3>
      <div class="row" style="gap:6px;margin-bottom:8px">
        <button class="btn copy" id="btnCopy">📋 Copiar JSON</button>
        <button class="btn clear" id="btnClear">🗑 Limpiar</button>
        <button class="btn save" id="btnGuardarSel">➕ Guardar puntos seleccionados</button>
      </div>

      <table><thead><tr><th>#</th><th>X</th><th>Y</th></tr></thead><tbody id="tbody"></tbody></table>
      <div id="statusSend" class="status" style="display:none"></div>
    </div>
  </div>

  <div class="section" style="margin-top:16px">
    <h3>🎮 Control rápido</h3>
    <div class="row" style="gap:8px;flex-wrap:wrap">
      <button class="btn" onclick="fetchText('/adelante')">⬆ Adelante</button>
      <button class="btn" onclick="fetchText('/atras')">⬇ Atrás</button>
      <button class="btn" onclick="fetchText('/antihorario')">⬅ Giro Antihorario</button>
      <button class="btn" onclick="fetchText('/horario')">➡ Giro Horario</button>
      <button class="btn clear" onclick="fetchText('/parar')">⛔ Parar</button>
      <button class="btn" onclick="fetchText('/luces')">💡 Luces</button>
    </div>
  </div>

  <div class="muted" style="text-align:center;margin-top:10px">v1.4 · Guardado de puntos seleccionados en placa</div>
</div>

<!-- ============ SCRIPT COMPLETO ============ -->
<script>
function fetchText(path){
  fetch(path).then(r=>r.text()).then(t=>{
    const s=document.getElementById('statusSend');
    if(!s)return;
    s.style.display='block';s.textContent=t;
    setTimeout(()=>s.style.display='none',2000);
  }).catch(_=>alert('Error de conexión'));
}

const canvas=document.getElementById('canvas');
const ctx=canvas.getContext('2d');
const tbody=document.getElementById('tbody');
const statusMesa=document.getElementById('statusMesa');
let mesa={ancho:2.00,alto:1.20};
let puntos=[];
let seleccion=new Set();
const cebador={x:0,y:0}; // fijo

function pxToM(xpx,ypx){
  const xm=(xpx/canvas.width)*mesa.ancho;
  const ym=((canvas.height-ypx)/canvas.height)*mesa.alto;
  return {x:+xm.toFixed(3),y:+ym.toFixed(3)};
}
function mToPx(xm,ym){
  const xpx=(xm/mesa.ancho)*canvas.width;
  const ypx=canvas.height-(ym/mesa.alto)*canvas.height;
  return {x:xpx,y:ypx};
}

function draw(){
  ctx.fillStyle='#0f1317';ctx.fillRect(0,0,canvas.width,canvas.height);
  const paso=0.1;
  ctx.lineWidth=1;ctx.strokeStyle='#1f2630';ctx.beginPath();
  for(let x=0;x<=mesa.ancho+1e-9;x+=paso){const px=(x/mesa.ancho)*canvas.width;ctx.moveTo(px,0);ctx.lineTo(px,canvas.height);}
  for(let y=0;y<=mesa.alto+1e-9;y+=paso){const py=canvas.height-(y/mesa.alto)*canvas.height;ctx.moveTo(0,py);ctx.lineTo(canvas.width,py);}
  ctx.stroke();
  ctx.strokeStyle='#324055';ctx.lineWidth=2;ctx.beginPath();
  ctx.moveTo(0,canvas.height);ctx.lineTo(canvas.width,canvas.height);
  ctx.moveTo(0,0);ctx.lineTo(0,canvas.height);ctx.stroke();
  // cebador
  {const {x,y}=mToPx(cebador.x,cebador.y);
   ctx.beginPath();ctx.arc(x,y,7,0,Math.PI*2);
   ctx.fillStyle='#ffd166';ctx.fill();
   ctx.fillStyle='#ffe8a1';ctx.font='12px monospace';
   ctx.fillText('Cebador (0,0)',x+10,y-10);}
  // puntos
  puntos.forEach((p,i)=>{
    const {x,y}=mToPx(p.x,p.y);
    ctx.beginPath();ctx.arc(x,y,6,0,Math.PI*2);
    ctx.fillStyle=seleccion.has(i)?'#00b4d8':'#38b000';ctx.fill();
    ctx.fillStyle='#cbd5e1';ctx.font='12px monospace';
    ctx.fillText(`${i+1} (${p.x},${p.y})`,x+8,y-8);
  });
}

function renderTable(){
  tbody.innerHTML='';
  puntos.forEach((p,i)=>{
    const tr=document.createElement('tr');
    if(seleccion.has(i))tr.style.background='#193445';
    tr.onclick=()=>{seleccion.has(i)?seleccion.delete(i):seleccion.add(i);renderTable();draw();};
    tr.innerHTML=`<td style="text-align:center">${i+1}</td><td>${p.x.toFixed(3)}</td><td>${p.y.toFixed(3)}</td>`;
    tbody.appendChild(tr);
  });
}

function canvasPos(evt){
  const rect=canvas.getBoundingClientRect();
  const scaleX=canvas.width/rect.width,scaleY=canvas.height/rect.height;
  const cx=evt.clientX??(evt.touches&&evt.touches[0].clientX);
  const cy=evt.clientY??(evt.touches&&evt.touches[0].clientY);
  return {x:(cx-rect.left)*scaleX,y:(cy-rect.top)*scaleY};
}
function findNearestIndex(px,py){
  let best=-1,bestD=1e9;
  puntos.forEach((p,i)=>{const pt=mToPx(p.x,p.y);const dx=pt.x-px,dy=pt.y-py,d=dx*dx+dy*dy;if(d<bestD){bestD=d;best=i;}});
  return best;
}
canvas.addEventListener('pointerdown',e=>{
  canvas.setPointerCapture(e.pointerId);
  const {x,y}=canvasPos(e);
  if(e.shiftKey&&puntos.length){const i=findNearestIndex(x,y);if(i>-1){puntos.splice(i,1);seleccion.delete(i);seleccion=new Set([...seleccion].map(j=>j>i?j-1:j));}}
  else{const m=pxToM(x,y);m.x=Math.max(0,Math.min(mesa.ancho,m.x));m.y=Math.max(0,Math.min(mesa.alto,m.y));puntos.push(m);}
  renderTable();draw();
},{passive:false});

document.getElementById('btnAplicar').onclick=()=>{
  const a=parseFloat(document.getElementById('mesaAncho').value),h=parseFloat(document.getElementById('mesaAlto').value);
  if(!(a>0&&h>0))return alert('Medidas inválidas');
  mesa.ancho=a;mesa.alto=h;statusMesa.textContent=`Mesa: ${a.toFixed(2)} m × ${h.toFixed(2)} m`;fitCanvas();
};
document.getElementById('btnClear').onclick=()=>{puntos=[];seleccion.clear();renderTable();draw();};
document.getElementById('btnCopy').onclick=()=>{
  const json=JSON.stringify([{label:'Cebador',x:0,y:0},...puntos],null,2);
  navigator.clipboard.writeText(json).then(()=>alert('Puntos copiados'));
};

// === Nuevo: Guardar puntos seleccionados en la placa (sin mover el robot) ===
document.getElementById('btnGuardarSel').onclick=async ()=>{
  if(seleccion.size<1) return alert('Seleccioná al menos 1 punto.');
  const idxs=[...seleccion].sort((a,b)=>a-b);
  const reqs = idxs.map(i=>{
    const p=puntos[i];
    return fetch(`/agregarCoordenada?x=${p.x}&y=${p.y}`).then(r=>r.text());
  });
  try{
    const results = await Promise.all(reqs);
    const s=document.getElementById('statusSend');
    s.style.display='block';
    s.textContent=`✅ Guardados ${results.length} punto(s) en la placa`;
    setTimeout(()=>s.style.display='none',2500);
  }catch(e){
    alert('Error al guardar puntos');
  }
};

function fitCanvas(){
  const panel=document.querySelector('.panel');
  const w=(panel?panel.clientWidth:window.innerWidth)-48;
  const maxW=Math.min(900,w);
  const aspect=mesa.ancho/mesa.alto;
  const targetW=Math.max(300,Math.round(maxW));
  const targetH=Math.max(200,Math.round(targetW/aspect));
  canvas.width=targetW;canvas.height=targetH;draw();
}
window.addEventListener('resize',fitCanvas);

fitCanvas();renderTable();draw();
</script>
</body>
</html>
)rawliteral";
  servidor.send(200, "text/html", html);
}

// Handlers básicos (acciones directas)
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

// Ruta: Navegar a un punto (no bloqueante) + GUARDAR coordenada (opcional, puedes seguir usándola si querés)
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
  guardarCoordenada(navegarDestino.x, navegarDestino.y); // 💾 guarda
  navegarFlag = true;
  servidor.send(200, "text/plain", "🚗 Navegación a (" + String(navegarDestino.x) + "," + String(navegarDestino.y) + ") guardada");
}

// Ruta: Ejecutar ruta completa (no bloqueante) + GUARDAR ambos puntos (opcional)
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
  guardarCoordenada(rutaP1.x, rutaP1.y);
  guardarCoordenada(rutaP2.x, rutaP2.y);
  ejecutarRutaFlag = true;
  servidor.send(200, "text/plain", "🧭 Ruta registrada y guardada");
}

// === Nuevo: agregar coordenada sin mover el robot ===
void manejarAgregarCoordenada() {
  if (!(servidor.hasArg("x") && servidor.hasArg("y"))) {
    servidor.send(400, "text/plain", "⚠️ Faltan parámetros 'x' o 'y'");
    return;
  }
  double x = servidor.arg("x").toDouble();
  double y = servidor.arg("y").toDouble();
  guardarCoordenada(x, y);
  servidor.send(200, "text/plain", "✅ Coordenada (" + String(x,3) + "," + String(y,3) + ") guardada en la placa");
}

// Endpoint: devolver coordenadas guardadas en JSON
void manejarCoordenadasGuardadas() {
  servidor.send(200, "application/json", listarCoordenadasJSON());
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

  // Nuevos: guardar y consultar coordenadas
  servidor.on("/agregarCoordenada", manejarAgregarCoordenada);
  servidor.on("/coordenadas", manejarCoordenadasGuardadas);

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

volatile bool pausa = false;
volatile bool sesionMate = false;

void SeguienterAccion (bool &bandera) {
  if (bandera) {
    bandera = false;
  } else {
    bandera = true;
  }
}

void Mateada() {
  if (coordenadasGuardadas.empty()) {
  return;
  }
  sesionMate = true;
  double anguloActual = 0;
  while (sesionMate) {
    for (auto tomador : coordenadasGuardadas) {
      anguloActual = handleViajarPunto(tomador, inicial, anguloActual);
      
      pausa = true;
      while (pausa == true) {
        Serial.println("Espera un cachito que está tomando el verde");
        delay(500);
      }

      moverAtrasMetros(distancia);

      pausa = true;
      while (pausa == true) {
        Serial.println("Se está cebando");
        delay(500);
      }

      delay(200);
    }
    girarAntiHorarioGrado(360 - anguloActual);
  }
}



