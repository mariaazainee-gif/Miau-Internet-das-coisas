/* ── INCLUSÃO DAS BIBLIOTECAS ─────────────────────────── */
#include <WiFi.h> /* comunicação Wi-Fi do ESP32 */
#include <PubSubClient.h> /* protocolo MQTT */
#include <ESP32Servo.h> /* controle do servo motor via PWM */
/* ── CONFIGURAÇÕES DE REDE ────────────────────────────── */
/* ⚠ Altere aqui com o nome e senha da sua rede Wi-Fi */
const char* WIFI_SSID = "G96201IOT";
const char* WIFI_SENHA = "12345678";
/* ── CONFIGURAÇÕES DO BROKER MQTT ─────────────────────── */
/* ⚠ Altere aqui com as credenciais do seu broker HiveMQ */
const char* MQTT_HOST = "broker.hivemq.com";
const int MQTT_PORTA = 1883; /* porta padrão MQTT (sem
SSL) */
const char* MQTT_CLIENT = "esp32-miau"; /* identificador único do
ESP32 */
const char* MQTT_USUARIO = ""; /* deixe vazio se não usar
senha */
const char* MQTT_SENHA_ = "";
/* ── TÓPICOS MQTT ─────────────────────────────────────── */
/* Esses nomes devem ser iguais aos usados no app (mqtt.js) */
const char* TOPICO_ALIMENTAR = "pet/alimentar"; /* ESP32 assina (ouve)
*/
const char* TOPICO_AGENDAR = "pet/agendar"; /* ESP32 assina (ouve)
*/
const char* TOPICO_STATUS = "pet/status"; /* ESP32 publica (fala)
*/
const char* TOPICO_NIVEL = "pet/nivel"; /* ESP32 publica (fala)
*/
const char* TOPICO_PRESENCA = "pet/presenca"; /* ESP32 publica (fala)
*/
/* ── PINOS DO ESP32 ───────────────────────────────────── */
const int PINO_SERVO = 18; /* sinal PWM do servo motor SG90
*/
const int PINO_TRIG = 17; /* pino TRIG do sensor ultrassônico
(teste isolado) */
const int PINO_ECHO = 25; /* pino ECHO do sensor ultrassônico
(teste isolado) */
const int PINO_MH_SENSOR = 26; /* Saída digital D0 do sensor MH
(Flying Fish) - teste isolado */
/* ── PARÂMETROS DO SERVO ──────────────────────────────── */
const int SERVO_FECHADO = 0; /* ângulo = pote fechado (graus)
*/
const int SERVO_ABERTO = 90; /* ângulo = pote aberto (graus)
*/
const int TEMPO_ABERTO_MS = 3000; /* tempo em milissegundos aberto
*/
/* ── PARÂMETROS DO SENSOR ULTRASSÔNICO ───────────────── */
/* Distância máxima do sensor até o fundo (reservatório vazio) */
const float DIST_VAZIO_CM = 20.0; /* calibre com o seu comedouro
*/
const float DIST_CHEIO_CM = 2.0; /* distância quando cheio
*/
/* ── OBJETOS GLOBAIS ──────────────────────────────────── */
WiFiClient clienteWifi; /* conexão TCP/IP
*/
PubSubClient clienteMqtt(clienteWifi); /* cliente MQTT
*/
Servo servoMotor; /* objeto do servo
*/
/* ── VARIÁVEIS DE CONTROLE ────────────────────────────── */
unsigned long ultimoHeartbeat = 0; /* tempo do último sinal enviado
*/
unsigned long ultimaLeituraNivel= 0; /* tempo da última leitura do
pote*/
int totalRefeicoes = 0; /* contador de refeições do dia
*/
bool presencaAnterior = false; /* estado anterior do sensor
MH */
/* ── INTERVALO DOS TIMERS (em milissegundos) ──────────── */
const unsigned long INTERVALO_HEARTBEAT = 30000; /* 30 segundos
*/
const unsigned long INTERVALO_NIVEL = 30000; /* 30 segundos
*/
/* ============================================================
SETUP — roda uma única vez ao ligar o ESP32
============================================================ */
void setup() {
Serial.begin(115200);
Serial.println("\n[MiAU] Iniciando comedouro inteligente...");
/* Configura os pinos como entrada ou saída */
pinMode(PINO_TRIG, OUTPUT);
pinMode(PINO_ECHO, INPUT);
pinMode(PINO_MH_SENSOR, INPUT); /* Configura o pino do sensor MH como
entrada */
/* Associa o servo ao pino e o posiciona fechado */
servoMotor.attach(PINO_SERVO);
servoMotor.write(SERVO_FECHADO);
Serial.println("[MiAU] Servo inicializado na posição fechada.");
/* Conecta ao Wi-Fi */
conectarWifi();
/* Configura o cliente MQTT */
clienteMqtt.setServer(MQTT_HOST, MQTT_PORTA);
clienteMqtt.setCallback(aoReceberMensagem); /* função chamada ao
receber msg */
clienteMqtt.setKeepAlive(30); /* heartbeat TCP a cada
30s */
/* Conecta ao broker MQTT */
conectarMqtt();
Serial.println("[MiAU] Setup concluído! Aguardando comandos...");
}
/* ============================================================
LOOP — roda continuamente enquanto o ESP32 estiver ligado
============================================================ */
void loop() {
/* Garante que o MQTT permanece conectado */
if (!clienteMqtt.connected()) {
Serial.println("[MiAU] MQTT desconectado. Reconectando...");
conectarMqtt();
}
/* Processa as mensagens MQTT recebidas */
clienteMqtt.loop();
/* Obtém o tempo atual em milissegundos desde que o ESP32 ligou */
unsigned long agora = millis();
/* Envia heartbeat periodicamente para indicar que está online */
if (agora - ultimoHeartbeat >= INTERVALO_HEARTBEAT) {
ultimoHeartbeat = agora;
publicarStatus("online");
Serial.println("[MiAU] Heartbeat enviado.");
}
/* Lê o nível de ração periodicamente */
if (agora - ultimaLeituraNivel >= INTERVALO_NIVEL) {
ultimaLeituraNivel = agora;
lerEPublicarNivel();
}
/* Verifica a presença do pet continuamente */
verificarPresenca();
}
/* ============================================================
CALLBACK MQTT — chamado quando uma mensagem chega
============================================================ */
void aoReceberMensagem(char* topico, byte* payload, unsigned int
tamanho) {
String mensagem = "";
for (unsigned int i = 0; i < tamanho; i++) {
mensagem += (char)payload[i];
}
mensagem.trim();
Serial.print("[MQTT] Mensagem recebida em '");
Serial.print(topico);
Serial.print("': ");
Serial.println(mensagem);
if (String(topico) == TOPICO_ALIMENTAR) {
if (mensagem == "alimentar") {
abrirComedouro();
}
} else if (String(topico) == TOPICO_AGENDAR) {
Serial.print("[MiAU] Agendamento recebido: ");
Serial.println(mensagem);
}
}
/* ============================================================
ABRIR O COMEDOURO (acionar o servo)
============================================================ */
void abrirComedouro() {
Serial.println("[MiAU] Iniciando abertura do comedouro...");
publicarStatus("alimentando");
servoMotor.write(SERVO_ABERTO);
Serial.println("[MiAU] Servo ABERTO.");
delay(TEMPO_ABERTO_MS);
servoMotor.write(SERVO_FECHADO);
Serial.println("[MiAU] Servo FECHADO.");
totalRefeicoes++;
String confirmacao = "concluido|refeicoes:" + String(totalRefeicoes);
publicarStatus(confirmacao.c_str());
Serial.print("[MiAU] Refeição concluída. Total hoje: ");
Serial.println(totalRefeicoes);
}
/* ============================================================
SENSOR ULTRASSÔNICO — medir nível de ração
============================================================ */
void lerEPublicarNivel() {
digitalWrite(PINO_TRIG, LOW);
delayMicroseconds(2);
digitalWrite(PINO_TRIG, HIGH);
delayMicroseconds(10);
digitalWrite(PINO_TRIG, LOW);
long duracao = pulseIn(PINO_ECHO, HIGH, 25000);
if (duracao == 0) {
Serial.println("[Sensor] Timeout na leitura ultrassônica.");
return;
}
float distanciaCm = duracao / 58.0;
Serial.print("[Sensor] Distância medida: ");
Serial.print(distanciaCm);
Serial.println(" cm");
float porcentagem = map(
distanciaCm * 10,
(int)(DIST_VAZIO_CM * 10),
(int)(DIST_CHEIO_CM * 10),
0, 100
);
porcentagem = constrain(porcentagem, 0, 100);
String nivelStr = String((int)porcentagem);
clienteMqtt.publish(TOPICO_NIVEL, nivelStr.c_str());
Serial.print("[MiAU] Nível de ração publicado: ");
Serial.print(nivelStr);
Serial.println("%");
}
/* ============================================================
SENSOR MH (Flying Fish) — detectar presença do pet
============================================================ */
/**
* verificarPresenca()
* Lê o sensor MH. Como ele usa lógica invertida:
* LOW = Obstáculo Detectado (Pet presente)
* HIGH = Sem obstáculo
*/
void verificarPresenca() {
// Invertemos a leitura com o operador '!' para que presencaAtual
// seja 'true' quando o pet estiver lá (sensor em LOW)
bool presencaAtual = (digitalRead(PINO_MH_SENSOR) == LOW);
/* Só publica quando muda de "sem presença" para "com presença" */
if (presencaAtual && !presencaAnterior) {
Serial.println("[Sensor] Pet detectado pelo sensor MH!");
/* Publica "detectado" no broker MQTT */
clienteMqtt.publish(TOPICO_PRESENCA, "detectado|--:--");
}
/* Atualiza o estado anterior para a próxima verificação */
presencaAnterior = presencaAtual;
delay(100); // Pequeno atraso para estabilização (debounce)
}
/* ============================================================
PUBLICAR STATUS, CONECTAR WI-FI E CONECTAR MQTT
============================================================ */
void publicarStatus(const char* mensagem) {
clienteMqtt.publish(TOPICO_STATUS, mensagem);
Serial.print("[MQTT] Status publicado: ");
Serial.println(mensagem);
}
void conectarWifi() {
Serial.print("[Wi-Fi] Conectando à rede '");
Serial.print(WIFI_SSID);
Serial.print("'");
WiFi.begin(WIFI_SSID, WIFI_SENHA);
while (WiFi.status() != WL_CONNECTED) {
delay(500);
Serial.print(".");
}
Serial.println("\n[Wi-Fi] Conectado!");
Serial.print("[Wi-Fi] Endereço IP: ");
Serial.println(WiFi.localIP());
}
void conectarMqtt() {
int tentativas = 0;
while (!clienteMqtt.connected() && tentativas < 5) {
tentativas++;
Serial.print("[MQTT] Tentativa ");
Serial.print(tentativas);
Serial.print(" de conexão ao broker '");
Serial.print(MQTT_HOST);
Serial.print("'...");
bool conectou;
if (strlen(MQTT_USUARIO) > 0) {
conectou = clienteMqtt.connect(MQTT_CLIENT, MQTT_USUARIO,
MQTT_SENHA_);
} else {
conectou = clienteMqtt.connect(MQTT_CLIENT);
}
if (conectou) {
Serial.println(" Conectado!");
clienteMqtt.subscribe(TOPICO_ALIMENTAR);
clienteMqtt.subscribe(TOPICO_AGENDAR);
Serial.println("[MQTT] Inscrito nos tópicos:");
Serial.println(" → pet/alimentar");
Serial.println(" → pet/agendar");
publicarStatus("online");
} else {
Serial.print(" Falhou (código: ");
Serial.print(clienteMqtt.state());
Serial.println("). Tentando novamente em 3s...");
delay(3000);
}
}
if (!clienteMqtt.connected()) {
Serial.println("[MQTT] Não foi possível conectar após 5
tentativas.");
}
}