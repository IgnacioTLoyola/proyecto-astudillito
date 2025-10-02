// Control de motores L298N con ESP32-CAM via WiFi
#include <WiFi.h>
#include <WebServer.h>

// ========== CONFIGURACIÓN WIFI ==========
const char* ssid = "Asuncion5";           // ⚠ CAMBIAR
const char* password = "44462987";        // ⚠ CAMBIAR

// Pines de conexión L298N a ESP32-CAM
#define IN1 14  // Motor A - Dirección 1
#define IN2 15  // Motor A - Dirección 2
#define IN3 13  // Motor B - Dirección 1
#define IN4 12  // Motor B - Dirección 2
#define LED 4
int gradosXseg = 360;
float  velocidadRobot = 0.67f;

WebServer server(80);

// Variables para giro de 1 segundo
bool giroActivo = false;
unsigned long tiempoInicio = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32-CAM Control de Motores ===");
  
  // Configurar pines
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(LED, OUTPUT);
  
  pararMotores();
  digitalWrite(LED, LOW);
  
  // Conectar a WiFi
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
  server.on("/", handleRoot);
  server.on("/adelante", handleAdelante);
  server.on("/atras", handleAtras);
  server.on("/horario", handleHorario);
  server.on("/antihorario", handleAntihorario);
  server.on("/parar", handleParar);
  server.on("/luces", handleLuces);

  // Ruta para el giro 1 segundo
  server.on("/horarioSegundo", handleHorarioSegundo);

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
      Serial.println("Nuevo valor gradosXseg: " + String(gradosXseg));
    } else {
      server.send(400, "text/plain", "⚠️ Falta parametro valor (ej: /setGrados?valor=720)");
    }
  });
  server.on("/girarGrados", []() {
    if (server.hasArg("g")) {
      float grados = server.arg("g").toFloat();
      girarAntiHorarioGrado(grados);
      server.send(200, "text/plain", "⟲ Girando " + String(grados) + "° antihorario");
    } else {
      server.send(400, "text/plain", "⚠️ Falta parámetro g (ej: /girarGrados?g=45)");
    }
  });



  server.begin();
  Serial.println("🚀 Servidor web iniciado");
}

void loop() {
  server.handleClient();

  // Si hay un giro activo, controlar el tiempo
  if (giroActivo && millis() - tiempoInicio >= 1000) {
    pararMotores();
    giroActivo = false;
    Serial.println("🛑 Giro horario terminado (1s)");
  }
}

// ========== FUNCIONES DE CONTROL ==========
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

void pararMotores() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void toggleLuces() {
  int estado = digitalRead(LED);
  digitalWrite(LED, !estado);
}

// ========== HANDLERS ==========
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Control ESP32-CAM</title>
<style>
    body {
        font-family: Arial, sans-serif;
        background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
        min-height: 100vh;
        display: flex;
        justify-content: center;
        align-items: center;
    }
    .container {
        background: white;
        border-radius: 20px;
        padding: 30px;
        box-shadow: 0 20px 60px rgba(0,0,0,0.3);
        max-width: 400px;
        width: 100%;
        text-align: center;
    }
    .btn {
        width: 100%;
        padding: 20px;
        margin: 10px 0;
        border: none;
        border-radius: 12px;
        font-size: 18px;
        font-weight: bold;
        color: white;
        cursor: pointer;
        transition: transform 0.1s;
        text-transform: uppercase;
    }
    .btn:active { transform: scale(0.95); }
    .btn-adelante { background: linear-gradient(135deg, #00c6ff, #0072ff); }
    .btn-atras { background: linear-gradient(135deg, #f7971e, #ffd200); }
    .btn-horario { background: linear-gradient(135deg, #667eea, #764ba2); }
    .btn-antihorario { background: linear-gradient(135deg, #f093fb, #f5576c); }
    .btn-parar { background: linear-gradient(135deg, #fa709a, #fee140); }
    .btn-luces { background: linear-gradient(135deg, #4facfe, #00f2fe); }
    .status {
        background: #f0f0f0;
        padding: 15px;
        border-radius: 10px;
        margin-bottom: 20px;
        font-weight: bold;
        color: #555;
    }
</style>
</head>
<body>
<div class="container">
    <h1>🤖 Control de Motores</h1>
    <div class="status" id="status">Listo</div>
    
    <button class="btn btn-adelante" 
        onmousedown="start('adelante')" onmouseup="stop()" 
        ontouchstart="start('adelante')" ontouchend="stop()">⬆️ ADELANTE</button>
    
    <button class="btn btn-atras" 
        onmousedown="start('atras')" onmouseup="stop()" 
        ontouchstart="start('atras')" ontouchend="stop()">⬇️ ATRÁS</button>
    
    <button class="btn btn-horario" 
        onmousedown="start('horario')" onmouseup="stop()" 
        ontouchstart="start('horario')" ontouchend="stop()">➡️ GIRAR HORARIO</button>

    <button class="btn btn-horario" onclick="enviar('horarioSegundo')">
        ⏱️ ANDAR 1 SEGUNDO
    </button>
    
    <button class="btn btn-antihorario" 
        onmousedown="start('antihorario')" onmouseup="stop()" 
        ontouchstart="start('antihorario')" ontouchend="stop()">⬅️ GIRAR ANTIHORARIO</button>

        
    
    <button class="btn btn-parar" onclick="enviar('parar')">⏹ PARAR</button>
    <button class="btn btn-luces" onclick="enviar('luces')">💡 LUCES</button>

    <!-- 🚀 NUEVO CONTROL: METROS -->
    <div style="margin-top:20px;">
        <input type="number" id="metros" placeholder="Metros a avanzar" 
               style="padding:10px; width:70%; border-radius:10px; border:1px solid #ccc;">
        <button class="btn btn-adelante" onclick="adelanteMetros()">🚗 AVANZAR METROS</button>
    </div>
    <!-- 🚀 NUEVO CONTROL: GRADOS POR SEGUNDO -->
    <div style="margin-top:20px;">
        <input type="number" id="grados" value="360"
              style="padding:10px; width:70%; border-radius:10px; border:1px solid #ccc;">
        <button class="btn btn-horario" onclick="setGrados()">⚙️ ACTUALIZAR GRADOS</button>
    </div>
    <!-- 🚀 NUEVO CONTROL: GIRAR X GRADOS (ANTIHORARIO) -->
    <div style="margin-top:20px;">
      <input type="number" id="gradosAntihorario" placeholder="Grados (ej: 45)"
             style="padding:10px; width:70%; border-radius:10px; border:1px solid #ccc;">
      <button class="btn btn-antihorario" onclick="girarGrados()">⟲ GIRAR GRADOS</button>
    </div>
</div>

<script>
function enviar(accion) {
    fetch('/' + accion)
        .then(r => r.text())
        .then(data => { document.getElementById('status').textContent = data; })
        .catch(_ => { document.getElementById('status').textContent = 'Error'; });
}

function start(accion) { enviar(accion); }
function stop() { enviar('parar'); }

// 🚀 FUNCIÓN PARA AVANZAR METROS
function adelanteMetros() {
    let m = document.getElementById("metros").value;
    if(m && m > 0) {
        fetch('/adelanteMetros?dist=' + m)
            .then(r => r.text())
            .then(data => { document.getElementById('status').textContent = data; })
            .catch(_ => { document.getElementById('status').textContent = 'Error'; });
    } else {
        document.getElementById('status').textContent = "⚠️ Ingresá un valor válido";
    }
}

function setGrados() {
    let g = document.getElementById("grados").value;
    if(g && g > 0) {
        fetch('/setGrados?valor=' + g)
            .then(r => r.text())
            .then(data => { document.getElementById('status').textContent = data; })
            .catch(_ => { document.getElementById('status').textContent = 'Error'; });
    } else {
        document.getElementById('status').textContent = "⚠️ Ingresá un valor válido";
    }
}
function girarGrados() {
  let g = parseFloat(document.getElementById("gradosAntihorario").value);
  if (!isNaN(g) && g > 0) {
    fetch('/girarGrados?g=' + g)
      .then(r => r.text())
      .then(data => { document.getElementById('status').textContent = data; })
      .catch(_ => { document.getElementById('status').textContent = 'Error'; });
  } else {
    document.getElementById('status').textContent = "⚠️ Ingresá un valor de grados válido (> 0)";
  }
}

</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleAdelante() {
  irAdelante();
  Serial.println("🚗 Adelante");
  server.send(200, "text/plain", "🚗 Adelante");
}

void handleAtras() {
  irAtras();
  Serial.println("🚙 Atrás");
  server.send(200, "text/plain", "🚙 Atrás");
}

void handleHorario() {
  girarHorario();
  Serial.println("➡️ Giro horario");
  server.send(200, "text/plain", "➡️ Giro horario");
}

void handleHorarioSegundo() {
  irAdelante();
  tiempoInicio = millis();
  giroActivo = true;
  Serial.println("➡️ Giro horario (1s)");
  server.send(200, "text/plain", "➡️ Giro horario (1s)");
}



void handleirAdelanteTresMetros() {
  irAdelante();
  tiempoInicio = millis();
  giroActivo = true;
  Serial.println("➡️ Giro horario (1s)");
  server.send(200, "text/plain", "➡️ Giro horario (1s)");
}

void moverAdelanteMetros(float metros) {
  unsigned long duracionMovimiento = (metros / velocidadRobot) * 1000; // tiempo en ms
  irAdelante();
  delay(duracionMovimiento);  // 🚫 Bloquea todo
  pararMotores();
}

void girarAntiHorarioGrado(float grados) {
  if (grados <= 0) return;
  if (gradosXseg <= 0) { Serial.println("⚠️ gradosXseg inválido"); return; }

  const float base = (grados * 1000.0f) / (float)gradosXseg;
  unsigned long msGiro = (unsigned long)roundf(base);

  if (grados < 100.0f) {
    msGiro += 20UL;
  } else if (grados <= 250.0f) {
    msGiro += 40UL;
  } else {
    // sin corrección extra
  }

  girarAntihorario();
  delay(msGiro);   // bloquea
  pararMotores();
}


void handleAntihorario() {
  girarAntihorario();
  Serial.println("⬅️ Giro antihorario");
  server.send(200, "text/plain", "⬅️ Giro antihorario");
}

void handleParar() {
  pararMotores();
  Serial.println("🛑 Motores parados");
  server.send(200, "text/plain", "🛑 Motores parados");
}

void handleLuces() {
  toggleLuces();
  int estado = digitalRead(LED);
  String mensaje = estado ? "💡 Luces ENCENDIDAS" : "💡 Luces APAGADAS";
  Serial.println(mensaje);
  server.send(200, "text/plain", mensaje);
}
