#include <WiFiS3.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ==================================================
// SENTINEL ROBOT - ARDUINO UNO R4 WIFI
// ==================================================

// Wi-Fi
char ssid[] = "SENTINEL-ROBOT";
char pass[] = "<WIFI-PASSWORD-REDACTED>";
WiFiServer server(80);

// L298N motor pins
const int IN1 = 2;
const int IN2 = 3;
const int IN3 = 4;
const int IN4 = 5;

// Servo pins
const int CAMERA_SERVO_PIN = 6;
const int ULTRA_SERVO_PIN = 7;

// HC-SR04
const int TRIG_PIN = 8;
const int ECHO_PIN = 9;

// Ultrasonic servo positions
const int ULTRA_LEFT = 1400;
const int ULTRA_CENTER = 1500;
const int ULTRA_RIGHT = 1600;

// Settings
const int OBSTACLE_DISTANCE = 35;
const unsigned long MOTOR_TIMEOUT = 1000;
const unsigned long SCAN_INTERVAL = 650;
const unsigned long LCD_INTERVAL = 500;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Servos
Servo cameraServo;
Servo ultraServo;

// Robot modes
enum RobotMode {
  STANDBY_MODE,
  MANUAL_MODE,
  AUTO_MODE
};

RobotMode robotMode = STANDBY_MODE;

// Security mode is independent of STANDBY / MANUAL / AUTO.
enum SecurityMode { NORMAL_SECURITY, LOCKDOWN_SECURITY };
SecurityMode securityMode = NORMAL_SECURITY;
String serialCommandBuffer = "";

// Owner token for control requests in LOCKDOWN; this is not MAC verification.
const char OWNER_CONTROL_TOKEN[] = "<OWNER-TOKEN-REDACTED>";
bool ownerAuthorizedSession = false;

// Scan positions
enum ScanPosition {
  SCAN_CENTER,
  SCAN_LEFT,
  SCAN_RIGHT
};

ScanPosition scanPosition = SCAN_CENTER;

// Variables
bool websiteStarted = false;
int cameraAngle = 45;
int trustScore = 100;

String currentMovement = "STOP";
String securityState = "SECURE";

long distanceLeft = -1;
long distanceCenter = -1;
long distanceRight = -1;
long distanceCM = -1;

unsigned long lastMotorCommand = 0;
unsigned long lastScan = 0;
unsigned long lastLCDUpdate = 0;

// ==================================================
// WEB DASHBOARD
// ==================================================

const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>SENTINEL</title>

<style>
*{box-sizing:border-box;user-select:none}
body{
 margin:0;background:radial-gradient(circle at top,#172554,#020617 70%);
 color:white;font-family:Arial;text-align:center
}
.card{
 width:92%;max-width:520px;margin:12px auto;padding:18px;
 background:#0f172a;border:1px solid #334155;border-radius:18px
}
.header{padding:20px}
h1{letter-spacing:4px;margin:0}
.muted{color:#94a3b8;font-size:11px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.box{background:#020617;padding:12px;border-radius:10px}
.val{font-size:18px;font-weight:bold;margin-top:5px}
.row{display:flex;gap:8px;margin-top:10px}
button{
 border:0;border-radius:12px;padding:14px;color:white;
 background:#334155;font-weight:bold;font-size:16px
}
.row button{flex:1}
.start{width:100%;background:#16a34a}
.active{background:#2563eb}
.autoactive{background:#16a34a}
.direction{font-size:24px;color:#38bdf8;margin:8px}
.scan{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-top:12px}
.scan div{background:#020617;padding:10px 2px;border-radius:8px}
.joy{
 position:relative;width:260px;height:260px;max-width:78vw;max-height:78vw;
 margin:14px auto;border-radius:50%;border:4px solid #475569;
 background:radial-gradient(circle,#334155,#0f172a);touch-action:none
}
.key{
 position:absolute;width:75px;height:75px;border-radius:50%;
 background:#1e40af;border:3px solid #60a5fa;
 display:flex;align-items:center;justify-content:center;
 font-size:27px;touch-action:none
}
.up{top:10px;left:50%;transform:translateX(-50%)}
.down{bottom:10px;left:50%;transform:translateX(-50%)}
.left{left:10px;top:50%;transform:translateY(-50%)}
.right{right:10px;top:50%;transform:translateY(-50%)}
.center{
 position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);
 border:2px solid #475569;border-radius:50%;width:70px;height:70px;
 display:grid;place-items:center;background:#020617;color:#94a3b8;font-size:12px
}
.stop{background:#991b1b;border:2px solid #ef4444;width:92%}
input[type=range]{width:100%}
.trust{font-size:28px;color:#38bdf8}
.footer{padding:20px;color:#64748b;font-size:11px}
</style>
</head>

<body>

<div class="header">
<h1>SENTINEL</h1>
<div class="muted">CYBER-SECURE MOBILE ROBOT</div>
</div>

<div class="card grid">
 <div class="box">
  <div class="muted">MODE</div>
  <div class="val" id="mode">STANDBY</div>
 </div>
 <div class="box">
  <div class="muted">MOVEMENT</div>
  <div class="val" id="movement">STOP</div>
 </div>
 <div class="box">
  <div class="muted">DISTANCE</div>
  <div class="val"><span id="distance">--</span> cm</div>
 </div>
 <div class="box">
  <div class="muted">SECURITY</div>
  <div class="val" id="security">SECURE</div>
 </div>
</div>

<div class="card">
 <div class="muted">ROBOT CONTROL</div><br>
 <button id="start" class="start" onclick="startSystem()">START SYSTEM</button>
 <div class="row">
  <button id="auto" onclick="setMode('auto')">AUTO</button>
  <button id="manual" onclick="setMode('manual')">MANUAL</button>
 </div>

 <div class="box" style="margin-top:12px">
  <div class="muted">CURRENT DIRECTION</div>
  <div id="direction" class="direction">STOP</div>
  <div class="scan">
   <div>LEFT<br><span id="left">--</span> cm</div>
   <div>CENTER<br><span id="center">--</span> cm</div>
   <div>RIGHT<br><span id="right">--</span> cm</div>
  </div>
 </div>
</div>

<div class="card">
 <div class="muted">MANUAL JOYSTICK - HOLD TO MOVE</div>
 <div class="joy">
  <div class="key up" data-dir="forward">▲</div>
  <div class="key down" data-dir="backward">▼</div>
  <div class="key left" data-dir="left">◀</div>
  <div class="key right" data-dir="right">▶</div>
  <div class="center">HOLD</div>
 </div>
 <button class="stop" onclick="stopRobot()">STOP ROBOT</button>
</div>

<div class="card">
 <div class="muted">CAMERA SERVO</div>
 <p>Angle: <span id="angle">45</span>°</p>
 <input id="slider" type="range" min="0" max="90" value="45">
</div>

<div class="card">
 <div class="muted">SECURITY STATUS</div>
 <div class="val" id="security2">SECURE</div><br>
 <div class="muted">TRUST SCORE</div>
 <div class="trust" id="trust">100</div>
</div>

<div class="footer">SENTINEL - LOCAL ROBOT CONTROL</div>

<script>
let moveTimer = null;
function ownerToken(){
 let token=localStorage.getItem('sentinel_owner_token');
 if(!token){token=prompt('Owner control token (needed in LOCKDOWN):');if(token===null)return '';token=token.trim();if(token)localStorage.setItem('sentinel_owner_token',token);}
 return token||'';
}
function controlFetch(path){
 let token=ownerToken();if(!token)return fetch(path); // NORMAL remains open; LOCKDOWN rejects requests without the token.
 return fetch(path+(path.includes('?')?'&':'?')+'token='+encodeURIComponent(token));
}

function startSystem(){
 controlFetch('/start')
 .then(()=>{
  document.getElementById('start').innerText='SYSTEM RUNNING';
 }).catch(()=>{});
}

function setMode(m){
 stopRobot();
 controlFetch('/mode?set='+m).catch(()=>{});
}

function sendMove(d){
 controlFetch('/move?dir='+d).catch(()=>{});
}

function startMove(d){
 sendMove(d);
 if(moveTimer) clearInterval(moveTimer);
 moveTimer=setInterval(()=>sendMove(d),250);
}

function stopRobot(){
 if(moveTimer){clearInterval(moveTimer);moveTimer=null;}
 sendMove('stop');
}

document.querySelectorAll('.key').forEach(b=>{
 b.addEventListener('pointerdown',e=>{
  e.preventDefault();
  try{b.setPointerCapture(e.pointerId)}catch(_){}
  startMove(b.dataset.dir);
 });
 b.addEventListener('pointerup',e=>{
  e.preventDefault();
  stopRobot();
 });
 b.addEventListener('pointercancel',stopRobot);
 b.addEventListener('lostpointercapture',stopRobot);
});

document.getElementById('slider').addEventListener('input',()=>{
 let a=document.getElementById('slider').value;
 document.getElementById('angle').innerText=a;
 controlFetch('/camera?angle='+a).catch(()=>{});
});

function updateStatus(){
 fetch('/status')
 .then(r=>r.json())
 .then(d=>{
  document.getElementById('mode').innerText=d.mode;
  document.getElementById('movement').innerText=d.movement;
  document.getElementById('direction').innerText=d.direction;
  document.getElementById('distance').innerText=d.distance<0?'--':d.distance;
  document.getElementById('left').innerText=d.left<0?'--':d.left;
  document.getElementById('center').innerText=d.center<0?'--':d.center;
  document.getElementById('right').innerText=d.right<0?'--':d.right;
  document.getElementById('security').innerText=d.security;
  document.getElementById('security2').innerText=d.security;
  document.getElementById('trust').innerText=d.trust;
  document.getElementById('security').style.color=
   d.security==='SECURE'?'#22c55e':d.security==='ALERT'?'#facc15':'#ef4444';
  document.getElementById('security2').style.color=
   document.getElementById('security').style.color;
  document.getElementById('auto').classList.toggle('autoactive',d.mode==='AUTO');
  document.getElementById('manual').classList.toggle('active',d.mode==='MANUAL');
 })
 .catch(()=>{});
}

setInterval(updateStatus,500);
updateStatus();

document.addEventListener('visibilitychange',()=>{
 if(document.hidden) stopRobot();
});
window.addEventListener('pagehide',stopRobot);
</script>
</body>
</html>
)rawliteral";

// ==================================================
// MOTOR FUNCTIONS - FORWARD/BACKWARD POLARITY UPDATED
// ==================================================

void stopMotors(){
 digitalWrite(IN1,LOW);
 digitalWrite(IN2,LOW);
 digitalWrite(IN3,LOW);
 digitalWrite(IN4,LOW);
 currentMovement="STOP";
}

void moveForward(){
 // Forward polarity: both motor sides use the forward direction.
 digitalWrite(IN1,HIGH);
 digitalWrite(IN2,LOW);
 digitalWrite(IN3,HIGH);
 digitalWrite(IN4,LOW);
 currentMovement="FORWARD";
 lastMotorCommand=millis();
}

void moveBackward(){
 // Backward polarity: reverse the polarity used for forward.
 digitalWrite(IN1,LOW);
 digitalWrite(IN2,HIGH);
 digitalWrite(IN3,LOW);
 digitalWrite(IN4,HIGH);
 currentMovement="BACKWARD";
 lastMotorCommand=millis();
}

void moveLeft(){
 // Updated turning polarity
 digitalWrite(IN1,LOW);
 digitalWrite(IN2,HIGH);
 digitalWrite(IN3,HIGH);
 digitalWrite(IN4,LOW);
 currentMovement="LEFT";
 lastMotorCommand=millis();
}

void moveRight(){
 // Updated turning polarity
 digitalWrite(IN1,HIGH);
 digitalWrite(IN2,LOW);
 digitalWrite(IN3,LOW);
 digitalWrite(IN4,HIGH);
 currentMovement="RIGHT";
 lastMotorCommand=millis();
}

// ==================================================
// ULTRASONIC SENSOR
// ==================================================

long readDistance(){
 digitalWrite(TRIG_PIN,LOW);	
 delayMicroseconds(2);
 digitalWrite(TRIG_PIN,HIGH);
 delayMicroseconds(10);
 digitalWrite(TRIG_PIN,LOW);

 unsigned long duration=pulseIn(ECHO_PIN,HIGH,30000UL);
 if(duration==0) return -1;

 long d=duration/58;
 if(d<2 || d>400) return -1;
 return d;
}

bool isClear(long d){
 return d>OBSTACLE_DISTANCE;
}

// ==================================================
// AUTO OBSTACLE AVOIDANCE
// ==================================================

void decideAuto(){
 if(isClear(distanceCenter)){
  moveForward();
  return;
 }

 bool leftClear=isClear(distanceLeft);
 bool rightClear=isClear(distanceRight);

 if(leftClear && rightClear){
  if(distanceLeft>distanceRight) moveLeft();
  else moveRight();
 }
 else if(leftClear){
  moveLeft();
 }
 else if(rightClear){
  moveRight();
 }
 else{
  stopMotors();
 }
}

void updateAutoMode(){
 if(securityState=="ATTACK" || securityState=="ALERT"){
  stopMotors();
  return;
 }

 if(millis()-lastScan<SCAN_INTERVAL) return;
 lastScan=millis();

 if(scanPosition==SCAN_CENTER){
  ultraServo.writeMicroseconds(ULTRA_CENTER);
  delay(100);
  distanceCenter=readDistance();
  distanceCM=distanceCenter;
  scanPosition=SCAN_LEFT;
 }
 else if(scanPosition==SCAN_LEFT){
  ultraServo.writeMicroseconds(ULTRA_LEFT);
  delay(100);
  distanceLeft=readDistance();
  scanPosition=SCAN_RIGHT;
 }
 else{
  ultraServo.writeMicroseconds(ULTRA_RIGHT);
  delay(100);
  distanceRight=readDistance();

  ultraServo.writeMicroseconds(ULTRA_CENTER);
  decideAuto();
  scanPosition=SCAN_CENTER;
 }
}

// ==================================================
// LCD
// ==================================================

void updateLCD(){
 lcd.setCursor(0,0);

 if(robotMode==STANDBY_MODE) lcd.print("STANDBY         ");
 else if(robotMode==MANUAL_MODE) lcd.print("MANUAL          ");
 else lcd.print("AUTO            ");

 lcd.setCursor(0,1);
 lcd.print("                ");
 lcd.setCursor(0,1);
 lcd.print(currentMovement);
 lcd.print(" D:");

 if(distanceCM>=0) lcd.print(distanceCM);
 else lcd.print("--");

 lcd.print("cm");
}

// ==================================================
// HTTP HELPERS
// ==================================================

String queryValue(const String &request,const char *key){
 String marker=String(key)+"=";
 int p=request.indexOf(marker);
 if(p<0) return "";

 int start=p+marker.length();
 int end=request.indexOf('&',start);
 if(end<0) end=request.indexOf(' ',start);
 if(end<0) end=request.length();

 return request.substring(start,end);
}

bool ownerTokenValid(const String &request){
 String supplied = queryValue(request, "token");
 return supplied == String(OWNER_CONTROL_TOKEN);
}

void sendHeaders(WiFiClient &client,const char *type){
 client.println("HTTP/1.1 200 OK");
 client.print("Content-Type: ");
 client.println(type);
 client.println("Connection: close");
 client.println("Cache-Control: no-store");
 client.println();
}

void sendOK(WiFiClient &client){
 sendHeaders(client,"text/plain");
 client.println("OK");
}

void sendLocked(WiFiClient &client){
 client.println("HTTP/1.1 423 Locked");
 client.println("Content-Type: text/plain");
 client.println("Connection: close");
 client.println("Cache-Control: no-store");
 client.println();
 client.println("SECURITY LOCKDOWN");
}


/*
  Audit event line sent to the Raspberry Pi over USB serial:
  EVENT,millis,source_ip,method,path,security_mode,result,reason
*/
void logSecurityEvent(WiFiClient &client, const String &requestLine,
                      const String &result, const String &reason) {
 IPAddress remote = client.remoteIP();
 int firstSpace = requestLine.indexOf(' ');
 int secondSpace = firstSpace >= 0 ? requestLine.indexOf(' ', firstSpace + 1) : -1;
 String method = firstSpace > 0 ? requestLine.substring(0, firstSpace) : "UNKNOWN";
 String path = (firstSpace >= 0 && secondSpace > firstSpace)
                 ? requestLine.substring(firstSpace + 1, secondSpace) : "UNKNOWN";
 // Redact owner token from serial audit logs.
 int tokenPos = path.indexOf("token=");
 if (tokenPos >= 0) {
   int tokenEnd = path.indexOf('&', tokenPos);
   if (tokenEnd < 0) tokenEnd = path.length();
   path = path.substring(0, tokenPos) + "token=REDACTED" + path.substring(tokenEnd);
 }
 method.replace(',', '_');
 path.replace(',', '_');
 String safeReason = reason;
 safeReason.replace(',', '_');
 safeReason.replace('\r', ' ');
 safeReason.replace('\n', ' ');
 Serial.print("EVENT,"); Serial.print(millis()); Serial.print(",");
 Serial.print(remote); Serial.print(","); Serial.print(method); Serial.print(",");
 Serial.print(path); Serial.print(",");
 Serial.print(securityMode == LOCKDOWN_SECURITY ? "LOCKDOWN" : "NORMAL");
 Serial.print(","); Serial.print(result); Serial.print(",");
 Serial.println(safeReason);
}

void sendStatus(WiFiClient &client){
 sendHeaders(client,"application/json");

 client.print("{\"mode\":\"");
 if(robotMode==STANDBY_MODE) client.print("STANDBY");
 else if(robotMode==MANUAL_MODE) client.print("MANUAL");
 else client.print("AUTO");

 client.print("\",\"movement\":\"");
 client.print(currentMovement);
 client.print("\",\"direction\":\"");
 client.print(currentMovement);

 client.print("\",\"distance\":");
 client.print(distanceCM);
 client.print(",\"left\":");
 client.print(distanceLeft);
 client.print(",\"center\":");
 client.print(distanceCenter);
 client.print(",\"right\":");
 client.print(distanceRight);

 client.print(",\"security\":\"");
 client.print(securityState);
 client.print("\",\"securityMode\":\"");
 client.print(securityMode == LOCKDOWN_SECURITY ? "LOCKDOWN" : "NORMAL");
 client.print("\",\"trust\":");
 client.print(trustScore);
 client.print(",\"camera\":");
 client.print(cameraAngle);
 client.println("}");
}

// ==================================================
// WEB REQUEST HANDLER
// ==================================================

void handleWebClient(){
 WiFiClient client=server.available();
 if(!client) return;

 unsigned long started=millis();
 while(!client.available() && millis()-started<1000) delay(1);

 if(!client.available()){
  client.stop();
  return;
 }

 String request=client.readStringUntil('\r');
 logSecurityEvent(client, request, "RECEIVED", "HTTP_REQUEST");

 // In LOCKDOWN, only requests carrying the owner token may change robot state.
 // This is token-based authorization, not MAC-address verification.
 bool isControlRequest =
     request.indexOf("GET /start") >= 0 ||
     request.indexOf("GET /mode?") >= 0 ||
     request.indexOf("GET /move?") >= 0 ||
     request.indexOf("GET /camera?") >= 0 ||
     request.indexOf("GET /security?") >= 0;
 bool trustedOwner = ownerTokenValid(request);
 if (securityMode == LOCKDOWN_SECURITY && isControlRequest && trustedOwner) {
   ownerAuthorizedSession = true;
 }

 if (securityMode == LOCKDOWN_SECURITY && isControlRequest && !trustedOwner) {
   // Reject without allowing an untrusted device to stop the owner's robot.
   logSecurityEvent(client, request, "REJECTED", "LOCKDOWN_OWNER_TOKEN_REQUIRED");
   sendLocked(client);
   delay(1);
   client.stop();
   return;
 }

 if(request.indexOf("GET / ")>=0){
  websiteStarted=true;
  stopMotors();
  sendHeaders(client,"text/html");
  client.print(MAIN_PAGE);
 }
 else if(request.indexOf("GET /start")>=0){
  if(securityMode == LOCKDOWN_SECURITY && !trustedOwner){
   stopMotors(); sendLocked(client);
  } else {
   websiteStarted=true; robotMode=STANDBY_MODE; stopMotors(); sendOK(client);
  }
 }
 else if(request.indexOf("GET /mode?")>=0){
  String mode=queryValue(request,"set");

  if(securityMode == LOCKDOWN_SECURITY && !trustedOwner){ stopMotors(); }
  else if(websiteStarted && mode=="auto"){
   robotMode=AUTO_MODE;
   stopMotors();
   ultraServo.writeMicroseconds(ULTRA_CENTER);
   scanPosition=SCAN_CENTER;
   distanceLeft=distanceCenter=distanceRight=-1;
   lastScan=millis()-SCAN_INTERVAL;
  }
  else if(websiteStarted && mode=="manual"){
   robotMode=MANUAL_MODE;
   stopMotors();
   ultraServo.writeMicroseconds(ULTRA_CENTER);
  }

  sendOK(client);
 }
 else if(request.indexOf("GET /move?")>=0){
  String dir=queryValue(request,"dir");

  if((securityMode == LOCKDOWN_SECURITY && !trustedOwner) || robotMode!=MANUAL_MODE || !websiteStarted ||
      securityState=="ATTACK" || securityState=="ALERT"){
    stopMotors();
    logSecurityEvent(client, request, "REJECTED",
      securityMode == LOCKDOWN_SECURITY ? "LOCKDOWN" :
      (robotMode!=MANUAL_MODE ? "NOT_MANUAL" : "SECURITY_STATE"));
   }
   else if(dir=="forward") { moveForward(); logSecurityEvent(client, request, "ACCEPTED", "MOVE_FORWARD"); }
   else if(dir=="backward") { moveBackward(); logSecurityEvent(client, request, "ACCEPTED", "MOVE_BACKWARD"); }
   else if(dir=="left") { moveLeft(); logSecurityEvent(client, request, "ACCEPTED", "MOVE_LEFT"); }
   else if(dir=="right") { moveRight(); logSecurityEvent(client, request, "ACCEPTED", "MOVE_RIGHT"); }
   else { stopMotors(); logSecurityEvent(client, request, "ACCEPTED", "MOVE_STOP"); }

  sendOK(client);
 }
 else if(request.indexOf("GET /camera?")>=0){
  String angle=queryValue(request,"angle");

  if(angle.length()>0){
   cameraAngle=constrain(angle.toInt(),0,90);
   cameraServo.write(cameraAngle);
    logSecurityEvent(client, request, "ACCEPTED", "CAMERA_ANGLE");
  }

  sendOK(client);
 }
 else if(request.indexOf("GET /security?")>=0){
  String state=queryValue(request,"state");
  state.toUpperCase();

  if(state=="SECURE" || state=="ALERT" || state=="ATTACK")
   securityState=state;

  String trust=queryValue(request,"trust");
  if(trust.length()>0)
   trustScore=constrain(trust.toInt(),0,100);

  if(securityState=="ATTACK" || securityState=="ALERT")
   stopMotors();

  logSecurityEvent(client, request, "ACCEPTED", "SECURITY_STATE_CHANGE");
   sendOK(client);
 }
 else if(request.indexOf("GET /status")>=0){
  sendStatus(client);
 }
 else{
  client.println("HTTP/1.1 404 Not Found");
  client.println("Connection: close");
  client.println();
  client.println("404");
 }

 delay(1);
 client.stop();
}

// ==================================================
// RASPBERRY PI SERIAL CONTROL
// ==================================================
// Commands are newline terminated. The USB serial connection is trusted.
// SECURITY,LOCKDOWN | SECURITY,NORMAL | STATUS
// MODE,STANDBY | MODE,MANUAL | MODE,AUTO
// MOVE,FORWARD | MOVE,BACKWARD | MOVE,LEFT | MOVE,RIGHT | MOVE,STOP

void sendSerialStatus(){
 Serial.print("STATUS,SECURITY=");
 Serial.print(securityMode == LOCKDOWN_SECURITY ? "LOCKDOWN" : "NORMAL");
 Serial.print(",ROBOT=");
 if(robotMode == STANDBY_MODE) Serial.print("STANDBY");
 else if(robotMode == MANUAL_MODE) Serial.print("MANUAL");
 else Serial.print("AUTO");
 Serial.print(",MOVEMENT="); Serial.print(currentMovement);
 Serial.print(",STATE="); Serial.println(securityState);
}

void handlePiCommand(String command){
 command.trim(); command.toUpperCase();
 if(command.length()==0) return;
 if(command=="STATUS"){ sendSerialStatus(); return; }
 if(command=="SECURITY,LOCKDOWN"){
  securityMode=LOCKDOWN_SECURITY; ownerAuthorizedSession=false; robotMode=STANDBY_MODE; stopMotors();
  Serial.println("OK,SECURITY=LOCKDOWN"); return;
 }
 if(command=="SECURITY,NORMAL"){
  // Recovery always returns to STANDBY; it never starts the motors.
  securityMode=NORMAL_SECURITY; ownerAuthorizedSession=false; robotMode=STANDBY_MODE; stopMotors();
  Serial.println("OK,SECURITY=NORMAL,ROBOT=STANDBY"); return;
 }
 if(securityMode==LOCKDOWN_SECURITY){ stopMotors(); Serial.println("ERR,LOCKDOWN"); return; }
 if(command=="MODE,STANDBY"){
  robotMode=STANDBY_MODE; stopMotors(); Serial.println("OK,MODE=STANDBY");
 } else if(command=="MODE,MANUAL"){
  robotMode=MANUAL_MODE; stopMotors(); websiteStarted=true; Serial.println("OK,MODE=MANUAL");
 } else if(command=="MODE,AUTO"){
  robotMode=AUTO_MODE; stopMotors(); websiteStarted=true;
  ultraServo.writeMicroseconds(ULTRA_CENTER); scanPosition=SCAN_CENTER;
  distanceLeft=distanceCenter=distanceRight=-1; lastScan=millis()-SCAN_INTERVAL;
  Serial.println("OK,MODE=AUTO");
 } else if(command=="MOVE,STOP"){
  stopMotors(); Serial.println("OK,MOVE=STOP");
 } else if(robotMode!=MANUAL_MODE){
  stopMotors(); Serial.println("ERR,SET_MODE_MANUAL_FIRST");
 } else if(securityState=="ATTACK" || securityState=="ALERT"){
  stopMotors(); Serial.println("ERR,SECURITY_STATE");
 } else if(command=="MOVE,FORWARD"){
  moveForward(); Serial.println("OK,MOVE=FORWARD");
 } else if(command=="MOVE,BACKWARD"){
  moveBackward(); Serial.println("OK,MOVE=BACKWARD");
 } else if(command=="MOVE,LEFT"){
  moveLeft(); Serial.println("OK,MOVE=LEFT");
 } else if(command=="MOVE,RIGHT"){
  moveRight(); Serial.println("OK,MOVE=RIGHT");
 } else { stopMotors(); Serial.println("ERR,UNKNOWN_COMMAND"); }
}

void handlePiSerial(){
 while(Serial.available()>0){
  char c=(char)Serial.read();
  if(c=='\r') continue;
  if(c=='\n'){
   handlePiCommand(serialCommandBuffer); serialCommandBuffer="";
  } else if(serialCommandBuffer.length()<80){
   serialCommandBuffer += c;
  } else { serialCommandBuffer=""; Serial.println("ERR,COMMAND_TOO_LONG"); }
 }
}

// ==================================================
// SETUP
// ==================================================

void setup(){
 Serial.begin(115200);
 delay(800);

 pinMode(IN1,OUTPUT);
 pinMode(IN2,OUTPUT);
 pinMode(IN3,OUTPUT);
 pinMode(IN4,OUTPUT);
 stopMotors();

 pinMode(TRIG_PIN,OUTPUT);
 pinMode(ECHO_PIN,INPUT);
 digitalWrite(TRIG_PIN,LOW);

 cameraServo.attach(CAMERA_SERVO_PIN);
 cameraServo.write(cameraAngle);
 delay(300);

 ultraServo.attach(ULTRA_SERVO_PIN,500,2500);
 ultraServo.writeMicroseconds(ULTRA_CENTER);
 delay(500);

 Wire.begin();
 lcd.init();
 lcd.backlight();
 lcd.clear();
 lcd.setCursor(0,0);
 lcd.print("SENTINEL");
 lcd.setCursor(0,1);
 lcd.print("STANDBY");

 Serial.println("Starting SENTINEL WiFi AP...");

 if(WiFi.beginAP(ssid,pass)!=WL_AP_LISTENING){
  lcd.clear();
  lcd.print("WiFi ERROR");
  Serial.println("WiFi AP FAILED");

  while(true){
   stopMotors();
   delay(1000);
  }
 }

 delay(1500);
 server.begin();

 Serial.print("SSID: ");
 Serial.println(ssid);
 Serial.print("Open http://");
 Serial.println(WiFi.localIP());

 lcd.clear();
 lcd.setCursor(0,0);
 lcd.print("SENTINEL READY");
 lcd.setCursor(0,1);
 lcd.print("STANDBY");

 robotMode=STANDBY_MODE;
 websiteStarted=false;
 stopMotors();
}

// ==================================================
// MAIN LOOP
// ==================================================

void loop(){
 handlePiSerial();
 handleWebClient();

 if(securityMode==LOCKDOWN_SECURITY && !ownerAuthorizedSession){
  stopMotors();
 }
 else if(robotMode==AUTO_MODE){
  updateAutoMode();
  distanceCM=distanceCenter;
 }
 else if(robotMode==MANUAL_MODE){
  distanceCM=readDistance();

  if(millis()-lastMotorCommand>MOTOR_TIMEOUT)
   stopMotors();
 }
 else{
  stopMotors();
 }

 if(millis()-lastLCDUpdate>=LCD_INTERVAL){
  lastLCDUpdate=millis();
  updateLCD();
 }
}
