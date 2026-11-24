#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <DHT.h>

// CAMBIA SOLO ESTOS DOS DATOS POR LOS DE TU PUNTO DE ACCESO.
// No compartas tu contrasena en capturas de pantalla.
const char* WIFI_NOMBRE = "NOMBRE_DE_TU_RED";
const char* WIFI_CLAVE  = "CLAVE_DE_TU_RED";

// EDITA ESTOS DOS DATOS EN TU COMPUTADOR. NO LOS COMPARTAS.
const char* BOT_TOKEN = "TU_TOKEN_DE_BOTFATHER";
const char* CHAT_ID = "TU_CHAT_ID";

// Telegram usa HTTPS. Esta version de demostracion usa setInsecure(),
// que cifra el trafico pero NO verifica la identidad del servidor.
// Para un sistema definitivo, configurar validacion del certificado TLS.


const int PIN_MQ5 = 34;
const int PIN_DHT = 32;
const float PORCENTAJE_ALERTA = 0.20f;
const float AUMENTO_MINIMO = 0.15f;

hd44780_I2Cexp lcd;
DHT dht(PIN_DHT, DHT11);
WebServer servidor(80);

float gas = NAN;
float temperatura = NAN;
float humedad = NAN;
float referencia = 0;
float umbral = 0;
bool calibrado = false;
bool alerta = false;
int lecturasAltas = 0;
int lecturasBajas = 0;
bool pantallaAmbiente = false;
bool pantallaCorre = false;
bool alertaAnterior = false;
unsigned long ultimoDHT = 0;
unsigned long ultimoCambioLCD = 0;
unsigned long ultimoCambioAlerta = 0;
unsigned long ultimoIntentoWiFi = 0;
bool servidorIniciado = false;

// Notificaciones pendientes: 0 = ninguna; 1 = alerta; 2 = normalidad.
int avisoPendiente = 0;
bool estadoTelegramAnterior = false;
unsigned long ultimoIntentoTelegram = 0;
const unsigned long ESPERA_REINTENTO_TELEGRAM = 15000;


byte signoInvertido[8] = {
  B00100, B00000, B00100, B00100,
  B00100, B00100, B00100, B00000
};

const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="es"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Detector de gas ESP32</title>
<style>
:root { color-scheme: dark; font-family: system-ui, sans-serif; }
body { margin:0; background:#0d1724; color:#edf5ff; }
main { max-width:640px; margin:auto; padding:24px 16px; }
h1 { font-size:1.5rem; margin:0 0 5px; }
small {color:#a4b8ca;}
#estado { margin:22px 0; border-radius:14px; padding:17px;
         background:#18372d; color:#9eebcb; font-weight:750; text-align:center; }
#estado.alerta { background:#5b2227; color:#ffe1e1; }
.tarjetas {display:grid; grid-template-columns:repeat(2,1fr); gap:12px;}
.tarjeta {border-radius:14px; padding:17px; background:#182738;}
.etiqueta {color:#a4b8ca;font-size:.85rem;}
.valor {font-size:1.75rem;font-weight:750;margin-top:8px;}
.grafico {background:#182738;border-radius:14px;margin-top:14px;padding:16px;}
canvas {display:block;width:100%;height:190px;}
.pie {margin-top:16px;color:#a4b8ca;font-size:.8rem;}
</style></head><body><main>
<h1>Detector de cocina</h1><small>ESP32 · MQ-5 · DHT11 · Datos en tiempo real</small>
<div id="estado">CONECTANDO...</div>
<div class="tarjetas">
  <div class="tarjeta"><div class="etiqueta">Señal MQ-5</div><div class="valor" id="gas">-- V</div></div>
  <div class="tarjeta"><div class="etiqueta">Umbral experimental</div><div class="valor" id="umbral">-- V</div></div>
  <div class="tarjeta"><div class="etiqueta">Temperatura</div><div class="valor" id="temperatura">-- °C</div></div>
  <div class="tarjeta"><div class="etiqueta">Humedad relativa</div><div class="valor" id="humedad">-- %</div></div>
</div>
<div class="grafico"><div class="etiqueta">Historial reciente de la señal (V)</div>
<canvas id="grafico" width="600" height="240"></canvas></div>
<div class="pie">Prototipo educativo. Las lecturas no son ppm y el umbral no es un límite certificado de seguridad.</div>
</main><script>
const g=document.getElementById('grafico');const ctx=g.getContext('2d');
const serie=[];let umbral=0;
function dibujar(){
 const w=g.width,h=g.height;ctx.clearRect(0,0,w,h);
 const datos=serie.length?serie:[0];
 let min=Math.min(...datos,umbral||Infinity),max=Math.max(...datos,umbral||-Infinity);
 if(!isFinite(min))min=0;if(!isFinite(max))max=1;
 let margen=Math.max(.06,(max-min)*.2);min-=margen;max+=margen;
 ctx.strokeStyle='#415266';ctx.lineWidth=1;
 for(let i=0;i<4;i++){let y=15+i*(h-35)/3;
 ctx.beginPath();ctx.moveTo(45,y);ctx.lineTo(w-8,y);ctx.stroke();
 ctx.fillStyle='#b0c1d1';ctx.font='13px sans-serif';
 ctx.fillText((max-(max-min)*i/3).toFixed(2),3,y+4);}
 const yv=v=>15+(max-v)/(max-min)*(h-35);
 if(umbral>0){ctx.setLineDash([7,5]);ctx.strokeStyle='#fb9f66';ctx.beginPath();
 ctx.moveTo(45,yv(umbral));ctx.lineTo(w-8,yv(umbral));ctx.stroke();ctx.setLineDash([]);}
 ctx.strokeStyle='#59c8ef';ctx.lineWidth=3;ctx.beginPath();
 datos.forEach((v,i)=>{let x=45+i*(w-54)/Math.max(1,59);
 if(i===0)ctx.moveTo(x,yv(v));else ctx.lineTo(x,yv(v));});ctx.stroke();
}
async function actualizar(){
 try{const r=await fetch('/datos',{cache:'no-store'});if(!r.ok)throw Error('sin respuesta');
 const d=await r.json();
 document.getElementById('gas').textContent=d.gas===null?'-- V':d.gas.toFixed(2)+' V';
 document.getElementById('umbral').textContent=d.calibrado?d.umbral.toFixed(2)+' V':'Calculando';
 document.getElementById('temperatura').textContent=d.temperatura===null?'-- °C':d.temperatura.toFixed(1)+' °C';
 document.getElementById('humedad').textContent=d.humedad===null?'-- %':d.humedad.toFixed(0)+' %';
 const s=document.getElementById('estado');
 s.className=d.alerta?'alerta':'';
 s.textContent=!d.calibrado?'PREPARANDO REFERENCIA':d.alerta?'ALERTA: GAS · ¡COREEEE!':'ESTADO: NORMAL';
 umbral=d.calibrado?d.umbral:0;
 if(d.gas!==null){serie.push(d.gas);if(serie.length>60)serie.shift();}dibujar();
 }catch(e){const s=document.getElementById('estado');s.className='alerta';s.textContent='SIN CONEXIÓN CON LA ESP32';}
}
actualizar();setInterval(actualizar,1500);
</script></body></html>
)HTML";

float leerGas() {
  float suma = 0;
  for (int i=0;i<10;i++) {
    suma += analogReadMilliVolts(PIN_MQ5);
    delay(10);
  }
  // AO llega por un divisor de dos resistencias iguales de 10 kohm.
  return (suma / 10.0 / 1000.0) * 2.0;
}

void mostrarLinea(int fila, const char* texto) {
  char buffer[17];
  snprintf(buffer, sizeof(buffer), "%-16.16s", texto);
  lcd.setCursor(0, fila);
  lcd.print(buffer);
}

void actualizarDHT() {
  if (millis()-ultimoDHT >= 2000) {
    ultimoDHT=millis();
    float t=dht.readTemperature();
    float h=dht.readHumidity();
    if (!isnan(t)) temperatura=t;
    if (!isnan(h)) humedad=h;
  }
}

void paginaPrincipal() {
  servidor.send(200, "text/html; charset=utf-8", PAGINA);
}

void datosJSON() {
  String respuesta = "{";
  respuesta += "\"gas\":";
  respuesta += isnan(gas) ? String("null") : String(gas, 3);
  respuesta += ",\"temperatura\":";
  respuesta += isnan(temperatura) ? String("null") : String(temperatura, 1);
  respuesta += ",\"humedad\":";
  respuesta += isnan(humedad) ? String("null") : String(humedad, 1);
  respuesta += ",\"umbral\":";
  respuesta += String(umbral, 3);
  respuesta += ",\"calibrado\":";
  respuesta += calibrado ? "true" : "false";
  respuesta += ",\"alerta\":";
  respuesta += alerta ? "true" : "false";
  respuesta += "}";
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json", respuesta);
}

void atenderRed() {
  if (WiFi.status()==WL_CONNECTED) {
    if (!servidorIniciado) {
      servidor.begin();
      servidorIniciado=true;
      Serial.print("PANEL WEB: http://");
      Serial.println(WiFi.localIP());
    }
    servidor.handleClient();
  } else if (millis()-ultimoIntentoWiFi>15000) {
    ultimoIntentoWiFi=millis();
    WiFi.reconnect();
  }
}

// Codificar texto para enviarlo como formulario HTTP POST a Telegram.
String codificarURL(const String &texto) {
  const char *hex = "0123456789ABCDEF";
  String salida;
  salida.reserve(texto.length() * 3);
  for (size_t i = 0; i < texto.length(); ++i) {
    unsigned char c = (unsigned char) texto[i];
    bool permitido = (c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') ||
                     c == '-' || c == '_' || c == '.' || c == '~';
    if (permitido) {
      salida += (char)c;
    } else {
      salida += '%';
      salida += hex[(c >> 4) & 0x0F];
      salida += hex[c & 0x0F];
    }
  }
  return salida;
}

bool telegramConfigurado() {
  return strlen(BOT_TOKEN) > 0 && strlen(CHAT_ID) > 0 &&
         String(BOT_TOKEN) != "TU_TOKEN_DE_BOTFATHER" &&
         String(CHAT_ID) != "TU_CHAT_ID";
}

bool enviarTelegram(const String &texto) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram: sin conexion WiFi");
    return false;
  }
  if (!telegramConfigurado()) {
    Serial.println("Telegram: debes completar BOT_TOKEN y CHAT_ID");
    return false;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure(); // SOLO DEMOSTRACION: omite verificacion TLS.
  cliente.setTimeout(5000);

  HTTPClient http;
  http.setTimeout(5000);
  String url = String("https://api.telegram.org/bot") + BOT_TOKEN + "/sendMessage";
  if (!http.begin(cliente, url)) {
    Serial.println("Telegram: no se pudo iniciar HTTPS");
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String cuerpo = "chat_id=" + codificarURL(String(CHAT_ID)) +
                  "&text=" + codificarURL(texto);
  int codigo = http.POST(cuerpo);
  String respuesta = http.getString();
  http.end();

  bool exito = codigo == 200 && respuesta.indexOf("\"ok\":true") >= 0;
  if (exito) {
    Serial.println("Telegram: mensaje enviado correctamente");
  } else {
    Serial.printf("Telegram: error HTTP %d\n", codigo);
    // Mostrar respuesta de error sin revelar el token en pantalla.
    if (respuesta.length() > 0) Serial.println(respuesta.substring(0, 300));
  }
  return exito;
}

// Se ejecuta despues de comparar el MQ-5 con el umbral.
// Un cambio de estado genera como maximo un aviso pendiente.
void actualizarNotificaciones() {
  if (alerta != estadoTelegramAnterior) {
    estadoTelegramAnterior = alerta;
    avisoPendiente = alerta ? 1 : 2;
    ultimoIntentoTelegram = millis() - ESPERA_REINTENTO_TELEGRAM;
  }
  if (avisoPendiente == 0 || WiFi.status() != WL_CONNECTED) return;
  if (millis() - ultimoIntentoTelegram < ESPERA_REINTENTO_TELEGRAM) return;
  ultimoIntentoTelegram = millis();

  String texto;
  if (avisoPendiente == 1) {
    texto = "ALERTA: AUMENTO DE GAS (prototipo)\n";
    texto += "¡COREEEE!\n";
    texto += "MQ-5: " + String(gas, 2) + " V\n";
    texto += "Umbral: " + String(umbral, 2) + " V\n";
  } else {
    texto = "SENAL NORMAL (prototipo)\n";
    texto += "El MQ-5 volvio bajo el umbral experimental.\n";
    texto += "MQ-5: " + String(gas, 2) + " V\n";
  }
  texto += "Temperatura: " + (isnan(temperatura) ? String("--") : String(temperatura, 1)) + " C\n";
  texto += "Humedad: " + (isnan(humedad) ? String("--") : String(humedad, 0)) + " %\n";
  texto += "Aviso experimental: no sustituye una alarma certificada.";
  if (enviarTelegram(texto)) avisoPendiente = 0;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21,22);
  dht.begin();
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ5, ADC_11db);

  if (lcd.begin(16,2)!=0) {
    Serial.println("Error al iniciar LCD");
    while(true) delay(1000);
  }
  lcd.backlight();
  lcd.createChar(0,signoInvertido);
  mostrarLinea(0,"DETECTOR MQ-5");
  mostrarLinea(1,"CONECTANDO WIFI");

  servidor.on("/",HTTP_GET,paginaPrincipal);
  servidor.on("/datos",HTTP_GET,datosJSON);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_NOMBRE,WIFI_CLAVE);

  unsigned long inicio=millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-inicio<18000) delay(250);
  atenderRed();
  if (WiFi.status()==WL_CONNECTED) {
    String direccionIP = "IP:" + WiFi.localIP().toString();
    mostrarLinea(0,"PANEL WIFI LISTO");
    mostrarLinea(1,direccionIP.c_str());
    delay(5000);
    if (telegramConfigurado()) {
      enviarTelegram(String("PRUEBA OK: detector MQ-5 conectado.\n") +
                     "Panel web: http://" + WiFi.localIP().toString() +
                     "\nEsperando referencia ambiental.");
    } else {
      Serial.println("Configura BOT_TOKEN y CHAT_ID para recibir avisos.");
    }
  } else {
    Serial.println("Sin WiFi: el detector local continuara funcionando.");
  }

  mostrarLinea(0,"ESTABILIZANDO");
  for (int s=10;s>0;s--) {
    char texto[17];
    snprintf(texto,sizeof(texto),"Espere: %d s",s);
    mostrarLinea(1,texto);
    gas=leerGas();
    actualizarDHT();
    atenderRed();
    delay(900);
  }

  mostrarLinea(0,"MIDIENDO AIRE");
  mostrarLinea(1,"CALIBRANDO...");
  float suma=0;
  for (int i=0;i<60;i++) {
    gas=leerGas();
    suma+=gas;
    actualizarDHT();
    atenderRed();
    delay(400);
  }
  referencia=suma/60.0f;
  umbral=referencia+max(referencia*PORCENTAJE_ALERTA,AUMENTO_MINIMO);
  calibrado=true;
  Serial.printf("Referencia: %.3f V; Umbral experimental: %.3f V\n",referencia,umbral);
  mostrarLinea(0,"SISTEMA LISTO");
  mostrarLinea(1,"MONITOREANDO");
  delay(1000);
  ultimoCambioLCD=millis();
}

void loop() {
  atenderRed();
  gas=leerGas();
  actualizarDHT();

  if (gas>=umbral) {
    if (lecturasAltas<3) lecturasAltas++;
    lecturasBajas=0;
  } else if (gas<=umbral-0.05f) {
    if (lecturasBajas<6) lecturasBajas++;
    lecturasAltas=0;
  } else {
    lecturasAltas=0;
    lecturasBajas=0;
  }
  if (lecturasAltas>=3) alerta=true;
  if (lecturasBajas>=6) alerta=false;

  // Enviar avisos solo si se activo o desactivo la alerta.
  // Si falla la red, reintenta cada 15 segundos.
  actualizarNotificaciones();

  if (millis()-ultimoCambioLCD>=3000) {
    ultimoCambioLCD=millis();
    pantallaAmbiente=!pantallaAmbiente;
  }
  if (alerta && !alertaAnterior) {
    pantallaCorre=false;
    ultimoCambioAlerta=millis();
  }
  alertaAnterior=alerta;

  char linea[17];
  if (alerta) {
    if (millis()-ultimoCambioAlerta>=2000) {
      ultimoCambioAlerta=millis();
      pantallaCorre=!pantallaCorre;
    }
    if (pantallaCorre) {
      mostrarLinea(0,"ALERTA: GAS");
      mostrarLinea(1,"");
      lcd.setCursor(3,1);
      lcd.write((uint8_t)0);
      lcd.print("COREEEE!");
    } else {
      snprintf(linea,sizeof(linea),"MQ5: %.2f V",gas);
      mostrarLinea(0,linea);
      mostrarLinea(1,"ALERTA: GAS");
    }
  } else if (pantallaAmbiente) {
    if (!isnan(temperatura)) snprintf(linea,sizeof(linea),"TEMP: %.1f C",temperatura);
    else snprintf(linea,sizeof(linea),"TEMP: --");
    mostrarLinea(0,linea);
    if (!isnan(humedad)) snprintf(linea,sizeof(linea),"HUMEDAD: %.0f %%",humedad);
    else snprintf(linea,sizeof(linea),"HUMEDAD: --");
    mostrarLinea(1,linea);
  } else {
    snprintf(linea,sizeof(linea),"MQ5: %.2f V",gas);
    mostrarLinea(0,linea);
    mostrarLinea(1,"ESTADO: NORMAL");
  }

  Serial.print("Gas_V:"); Serial.print(gas,3);
  Serial.print("\tUmbral_V:"); Serial.print(umbral,3);
  Serial.print("\tTemperatura_C:"); Serial.print(temperatura,1);
  Serial.print("\tHumedad_pct:"); Serial.println(humedad,1);
  atenderRed();
  delay(400);
}
