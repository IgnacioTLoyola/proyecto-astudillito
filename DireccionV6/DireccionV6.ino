// ================================================================
// 🚗 Control de motores L298N con ESP32-CAM via WiFi
// Autor: (Tu nombre)
// Descripción: Control de movimiento, giros y navegación por coordenadas
//              + Mateada con pasos y pausa controlada desde la UI
// ================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ========== CONFIGURACIÓN WIFI ==========
const char* ssid     = "HUAWEI-2.4G-Q5ug";  // ⚠ CAMBIAR
const char* password = "Astkw8Vt";          // ⚠ CAMBIAR

// Pines de conexión L298N a ESP32-CAM
#define IN1 14  // Motor A - Dirección 1
#define IN2 15  // Motor A - Dirección 2
#define IN3 13  // Motor B - Dirección 1
#define IN4 12  // Motor B - Dirección 2
#define LED 4

// --------- Estructuras y estado de navegación ----------
struct Punto {
  double x;
  double y;
};

struct viajeAnterior {
  double anguloAnterior;
  double distancia;
};

viajeAnterior viajeActual;

Punto inicial = {0, 0};          // origen
Punto posicionActual = {0, 0};   // se va actualizando tras cada movimiento       
int gradosXseg = 360;            // calibración giro
float velocidadRobot = 0.67f;    // m/s de avance lineal (calibración)

double anguloActual = 0;
double distanciaAVolver = 0;



WebServer server(80);

// ==================================================================================
// SECCIÓN: COORDENADAS GUARDADAS EN PLACA
// ==================================================================================
#define MAX_COORDENADAS_GUARDADAS 20

struct CoordenadaGuardada {
  double x;
  double y;
};

CoordenadaGuardada coordenadasGuardadas[MAX_COORDENADAS_GUARDADAS];
int totalCoordenadasGuardadas = 0;

void guardarCoordenada(double x, double y) {
  if (totalCoordenadasGuardadas >= MAX_COORDENADAS_GUARDADAS) {
    // modo circular: sobrescribe
    totalCoordenadasGuardadas = 0;
  }
  coordenadasGuardadas[totalCoordenadasGuardadas++] = { x, y };
  Serial.printf("💾 Coordenada guardada #%d: (%.3f, %.3f)\n", totalCoordenadasGuardadas, x, y);
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
// PROTOS
// ==================================================================================
void handleRoot();
void handleAdelante();
void handleAtras();
void handleHorario();
void handleAntihorario();
void handleParar();
void handleLuces();

// Movimiento “bajo nivel”
void irAtras();
void irAdelante();
void girarHorario();
void girarAntihorario();
void pararMotores();

// Movimiento “alto nivel”
void moverAdelanteMetros(double m);
void moverAtrasMetros(double m);
float tiempoPorGrados(float g);
void girarAntiHorarioGrado(double g);


// Navegación por punto
viajeAnterior handleViajarPunto(Punto puntoFinal, Punto puntoInicial, double anguloAnt);
double viajar(Punto destino);


// Mateada
void mateadaStep();

// ==================================================================================
// ESTADO DE MATEADA (no bloqueante por pasos)
// ==================================================================================
volatile bool pausa = false;
volatile bool sesionMate = false;
int idxTomador = 0;   // índice del tomador actual (usa coordenadasGuardadas)
int etapa = 0;        // 0 mover → 1 pausa tomar → 2 retroceder → 3 pausa cebar → 4 avanzar índice → 5 reorientar

// ==================================================================================
// SETUP
// ==================================================================================
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

  // Control básico
  server.on("/adelante", handleAdelante);
  server.on("/atras", handleAtras);
  server.on("/horario", handleHorario);
  server.on("/antihorario", handleAntihorario);
  server.on("/parar", handleParar);
  server.on("/luces", handleLuces);

  // Parámetros de giro/calibración
  server.on("/setGrados", []() {
    if (server.hasArg("valor")) {
      gradosXseg = server.arg("valor").toInt();
      server.send(200, "text/plain", "✅ gradosXseg actualizado a " + String(gradosXseg));
    } else {
      server.send(400, "text/plain", "⚠️ Falta parámetro valor (ej: /setGrados?valor=360)");
    }
  });

  // Avance por metros
  server.on("/adelanteMetros", []() {
    if (server.hasArg("dist")) {
      float metros = server.arg("dist").toFloat();
      moverAdelanteMetros(metros);
      server.send(200, "text/plain", "🚗 Avanzando " + String(metros) + " m");
    } else {
      server.send(400, "text/plain", "Falta parametro dist (ej: /adelanteMetros?dist=2.5)");
    }
  });

  // Giro por grados (antihorario)
  server.on("/girarGrados", []() {
    if (server.hasArg("g")) {
      float grados = server.arg("g").toFloat();
      girarAntiHorarioGrado(grados);
      server.send(200, "text/plain", "⟳ Girando " + String(grados) + "° antihorario");
    } else {
      server.send(400, "text/plain", "⚠️ Falta parámetro g (ej: /girarGrados?g=45)");
    }
  });

  // Viajar a un punto puntual (actualiza posicionActual/anguloActual)
  server.on("/viajarPunto", []() {
    if (server.hasArg("x") && server.hasArg("y")) {
      Punto destino = { server.arg("x").toDouble(), server.arg("y").toDouble() };
      viajar(destino);
      server.send(200, "text/plain", "🚗 Moviéndose al punto (" + String(destino.x, 3) + ", " + String(destino.y, 3) + ")");
    } else {
      server.send(400, "text/plain", "⚠️ Falta parámetro x o y");
    }
  });

  // --- Coordenadas guardadas (para botones de la UI) ---
  server.on("/agregarCoordenada", []() {
    if (server.hasArg("x") && server.hasArg("y")) {
      double x = server.arg("x").toDouble();
      double y = server.arg("y").toDouble();
      guardarCoordenada(x, y);
      server.send(200, "text/plain", "✅ Guardada (" + String(x, 3) + ", " + String(y, 3) + ")");
    } else {
      server.send(400, "text/plain", "⚠️ Faltan parámetros x,y");
    }
  });

  server.on("/listarCoordenadas", []() {
    server.send(200, "application/json", listarCoordenadasJSON());
  });

  // --- Mateada ---
  server.on("/mateada/start", []() {
    if (totalCoordenadasGuardadas <= 0) {
      server.send(400, "text/plain", "⚠️ No hay tomadores cargados. Usá 'Guardar puntos seleccionados'.");
      return;
    }
    sesionMate = true;
    pausa = false;
    idxTomador = 0;
    etapa = 0;
    Serial.println("🧉 Mateada iniciada");
    server.send(200, "text/plain", "🧉 Mateada iniciada con " + String(totalCoordenadasGuardadas) + " tomadores.");
  });

  server.on("/mateada/stop", []() {
    sesionMate = false;
    pausa = false;
    Serial.println("⏹ Mateada detenida");
    server.send(200, "text/plain", "⏹ Mateada detenida");
  });

  server.on("/mateada/siguiente", []() {
    // habilita siguiente acción: baja la pausa
    pausa = false;
    server.send(200, "text/plain", "⏭ Siguiente acción");
  });

  server.begin();
  Serial.println("🌐 Servidor HTTP iniciado");
}

// ==================================================================================
// INTERFAZ WEB
// ==================================================================================
void handleRoot() {
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
  .btn.mate{background:#ff7f11}
  .btn.stop{background:#ef476f}
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
      <button class="btn mate" onclick="fetchText('/mateada/start')">🧉 Iniciar mateada</button>
      <button class="btn mate" onclick="fetchText('/mateada/siguiente')">⏭ Siguiente acción</button>
      <button class="btn stop" onclick="fetchText('/mateada/stop')">⏹ Detener mateada</button>
    </div>
  </div>

  <div class="muted" style="text-align:center;margin-top:10px">v1.5 · Mateada por pasos + guardado en placa</div>
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

// Guardar puntos seleccionados en la placa (sin mover)
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
  server.send(200, "text/html", html);
}

// ==================================================================================
// FUNCIONES MOTORES (bajo nivel)
// ==================================================================================
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

// ==================================================================================
// MOVIMIENTO (alto nivel)
// ==================================================================================
void moverAdelanteMetros(double metros) {
  if (metros <= 0) return;
  unsigned long duracionMovimiento = (unsigned long)((metros / velocidadRobot) * 1000.0);
  irAdelante();
  delay(duracionMovimiento);
  pararMotores();
}
void moverAtrasMetros(double metros) {
  if (metros <= 0) return;
  unsigned long duracionMovimiento = (unsigned long)((metros / velocidadRobot) * 1000.0);
  irAtras();
  delay(duracionMovimiento);
  pararMotores();
}

// Tabla de calibración tiempo vs grados
struct PuntoVector { float t; float grados; };
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
double viajar(Punto destino) {
  // Usa los estados globales actuales
  viajeAnterior v = handleViajarPunto(destino, inicial, anguloActual);

  // Actualiza estado global del robot
  posicionActual = destino;
  anguloActual = v.anguloAnterior;
  distanciaAVolver = v.distancia;

  return v.distancia; // por si querés loguear/usar la distancia recorrida
}

// ==================================================================================
// NAVEGACIÓN POR COORDENADAS
// ==================================================================================
viajeAnterior handleViajarPunto(Punto puntoFinal, Punto puntoInicial, double anguloAnt) {
  double dx = puntoFinal.x - puntoInicial.x;
  double dy = puntoFinal.y - puntoInicial.y;
  double anguloObjetivo = atan2(dy, dx) * (180.0 / M_PI);

  // Mantener consistencia: giro antihorario por la magnitud de la diferencia
  double delta = fabs(anguloObjetivo - anguloAnt);
  girarAntiHorarioGrado(delta);
  delay(300);

  double distancia = sqrt(dx * dx + dy * dy);
  moverAdelanteMetros(distancia);
  delay(200);

  viajeAnterior viaje;
  viaje.anguloAnterior = anguloObjetivo;
  viaje.distancia = distancia;

  return viaje;
}



// ==================================================================================
// MATEADA: lógica por pasos (no while bloqueante)
// ==================================================================================
void mateadaStep() {
  if (!sesionMate) return;
  if (totalCoordenadasGuardadas <= 0) {
    Serial.println("⚠️ No hay tomadores, se detiene mateada.");
    sesionMate = false;
    return;
  }
  
  
  switch (etapa) {
    case 0: { // mover hacia tomador actual
      CoordenadaGuardada c = coordenadasGuardadas[idxTomador];
      Punto destino = { c.x, c.y };
      Serial.printf("➡️ Ir al tomador #%d (%.3f, %.3f)\n", idxTomador + 1, c.x, c.y);
      viajar(destino);
      
      pausa = true;   // esperar que tome
      etapa = 1;
      break;
    }
    case 1: { // esperando “está tomando”
      if (!pausa) {
        etapa = 2;
      }
      break;
    }
    case 2: { // retroceder un poco (retirar mate)
      Serial.println("↩️ Retroceder para retirar el mate");
      moverAtrasMetros(distanciaAVolver);
      pausa = true;   // esperar “se está cebando”
      etapa = 3;
      break;
    }
    case 3: { // esperando “se está cebando”
      if (!pausa) {
        etapa = 4;
      }
      break;
    }
    case 4: { // siguiente tomador
      idxTomador++;
      if (idxTomador >= totalCoordenadasGuardadas) {
        etapa = 5; // reorientar y volver a empezar (ciclo)
      } else {
        etapa = 0;
      }
      break;
    }
    case 5: { // reorientar a 0 (aprox) y reiniciar ciclo
      double giroRestante = fmod(360.0 - anguloActual + 360.0, 360.0);
      Serial.printf("🔄 Reorientar a 0°, giro restante %.2f°\n", giroRestante);
      girarAntiHorarioGrado(giroRestante);
      idxTomador = 0;
      etapa = 0;
      break;
    }
  }
}

// ==================================================================================
// HANDLERS BÁSICOS
// ==================================================================================
void handleAdelante()     { irAdelante();     server.send(200, "text/plain", "⬆️ Adelante"); }
void handleAtras()        { irAtras();        server.send(200, "text/plain", "⬇️ Atrás"); }
void handleHorario()      { girarHorario();   server.send(200, "text/plain", "➡️ Giro horario"); }
void handleAntihorario()  { girarAntihorario(); server.send(200, "text/plain", "⬅️ Giro antihorario"); }
void handleParar()        { pararMotores();   server.send(200, "text/plain", "⛔ Motores detenidos"); }

void handleLuces() {
  digitalWrite(LED, !digitalRead(LED));
  String mensaje = (digitalRead(LED)) ? "💡 Luces encendidas" : "🌑 Luces apagadas";
  Serial.println(mensaje);
  server.send(200, "text/plain", mensaje);
}

// ==================================================================================
// LOOP
// ==================================================================================
void loop() {
  server.handleClient();
  mateadaStep();  // avanza la mateada por pasos si está activa
}
