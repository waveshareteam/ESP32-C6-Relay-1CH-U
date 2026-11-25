#include "WS_WIFI.h"

// The name and password of the WiFi access point
const char *ssid = STASSID;                
const char *password = STAPSK;               

char ipStr[16];
WebServer server(80);         
bool WIFI_Connection = 0;                      

void handleRoot() {
  String myhtmlPage =
    String("") +
    "<html>"+
    "<head>"+
    "    <meta charset=\"utf-8\">"+
    "    <title>" RELAY_DEVICE_NAME "</title>"+
    "    <style>" +
    "        body {" +
    "            font-family: Arial, sans-serif;" +
    "            background-color: #f0f0f0;" +
    "            margin: 0;" +
    "            padding: 0;" +
    "        }" +
    "        .header {" +
    "            text-align: center;" +
    "            padding: 20px 0;" +
    "            background-color: #333;" +
    "            color: #fff;" +
    "            margin-bottom: 20px;" +
    "        }" +
    "        .container {" +
    "            max-width: 600px;" +
    "            margin: 0 auto;" +
    "            padding: 20px;" +
    "            background-color: #fff;" +
    "            border-radius: 5px;" +
    "            box-shadow: 0 0 5px rgba(0, 0, 0, 0.3);" +
    "        }" +
    "        .input-container {//" +
    "            display: flex;" +
    "            align-items: center;" +
    "            margin-bottom: 10px;" +
    "        }" +
    "        .input-container label {" +
    "            width: 80px;" + 
    "            margin-right: 10px;" +
    "        }" +
    "        .input-container input[type=\"text\"] {" +
    "            flex: 1;" +
    "            padding: 5px;" +
    "            border: 1px solid #ccc;" +
    "            border-radius: 3px;" +
    "            margin-right: 10px; "+ 
    "        }" +
    "        .input-container button {" +
    "            padding: 5px 10px;" +
    "            background-color: #333;" +
    "            color: #fff;" +
    "            font-size: 14px;" +
    "            font-weight: bold;" +
    "            border: none;" +
    "            border-radius: 3px;" +
    "            text-transform: uppercase;" +
    "            cursor: pointer;" +
    "        }" +
    "        .button-container {" +
    "            margin-top: 20px;" +
    "            text-align: center;" +
    "        }" +
    "        .button-container button {" +
    "            margin: 0 5px;" +
    "            padding: 10px 15px;" +
    "            background-color: #333;" +
    "            color: #fff;" +
    "            font-size: 14px;" +
    "            font-weight: bold;" +
    "            border: none;" +
    "            border-radius: 3px;" +
    "            text-transform: uppercase;" +
    "            cursor: pointer;" +
    "        }" +
    "        .button-container button:hover {" +
    "            background-color: #555;" +
    "        }" +
    "        nav {" +
    "            margin: 15px 0;" +
    "            text-align: center;" +
    "        }" +
    "        nav a {" +
    "            padding: 10px 50px;" +
    "            background-color: #333;" +
    "            color: white;" +
    "            text-decoration: none;" +
    "            font-weight: bold;" +
    "            border-radius: 5px;" +
    "        }" +
    "        nav a.relayControlActive {" + 
    "            background-color: #fff;" +   
    "            color: #333;" +
    "            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3), 0 1px 3px rgba(0, 0, 0, 0.1);" +
    "            transform: translateY(-4px);" +
    "            transition: all 0.2s ease-in-out;" +
    "        }" + 
    "        nav a.rtcEventActive {" + 
    "            background-color: #555;" +
    "        }" + 
    "    </style>" +
    "</head>"+
    "<body>"+
    "    <script defer=\"defer\">"+
    "        function ledSwitch(ledNumber) {"+
    "            var xhttp = new XMLHttpRequest();" +
    "            xhttp.onreadystatechange = function() {" +
    "                if (this.readyState == 4 && this.status == 200) {" +
    "                    console.log('LED ' + ledNumber + ' state changed');" +
    "                }" +
    "            };" +
    "            if (ledNumber == 1) {" +
    "             xhttp.open('GET', '/Switch' + ledNumber, true);" +
    "            }" +
    "            else if(ledNumber == 9){" +
    "            xhttp.open('GET', '/RelayOn', true);" +
    "            }" +
    "            else if(ledNumber == 0){" +
    "            xhttp.open('GET', '/RelayOff', true);" +
    "            }" +
    "            xhttp.send();" +
    "        }" +
    "        function updateData() {"
    "            var xhr = new XMLHttpRequest();"
    "            xhr.open('GET', '/getData', true);"
    "            xhr.onreadystatechange = function() {"
    "              if (xhr.readyState === 4 && xhr.status === 200) {"
    "                var dataArray = JSON.parse(xhr.responseText);"
    "                document.getElementById('ch1').value = dataArray[0];"
    // "                // Remove the button's disabled attribute to make it clickable"+
    "                document.getElementById('btn1').removeAttribute(\'disabled\');"+
    "                document.getElementById('btn9').removeAttribute(\'disabled\');"+
    "                document.getElementById('btn0').removeAttribute(\'disabled\');"+
    "              }"+
    "            };"+
    "            xhr.send();"+
    "        }"+
    "        function displayErrorTextBox(show) {"+
    "          var errorTextbox = document.getElementById('errorTextbox');"+
    "          errorTextbox.style.display = show ? 'block' : 'none';"+
    "        }"+
    "        function resetErrorTextBox() {"+
    "          document.getElementById(\'errorTextbox\').value = \'\';"+
    "        }"+
    "        var refreshInterval = 200;"+                                     
    "        setInterval(updateData, refreshInterval);"+       
    "    </script>" +
    "    <div class=\"header\">"+
    "        <h1>" RELAY_DEVICE_NAME "</h1>"+
    "    </div>"+
    "    <nav>" +
    "        <a href=\"/\" id=\"relayControlLink\" class=\"relayControlActive\">Relay Control</a>" +  
    "        <a href=\"/RTC_Event\" id=\"rtcEventLink\" class=\"rtcEventActive\">RTC Event</a>" + 
    "    </nav>" +
    "    <div class=\"container\">"+
    "        <div class=\"input-container\" style=\"margin-left: 140px;\">"+
    "            <label for=\"input1\">CH1</label>"+
    "            <input type=\"text\" id=\"ch1\" />"+
    "            <button value=\"Switch1\" id=\"btn1\" disabled onclick=\"ledSwitch(1)\">Flipped State</button>"+
    "        </div>"+
    "        <div class=\"button-container\">"+
    "            <button value=\"RelayOn\" id=\"btn9\" disabled onclick=\"ledSwitch(9)\">Relay On</button>"+
    "            <button value=\"RelayOff\" id=\"btn0\" disabled onclick=\"ledSwitch(0)\">Relay Off</button>"+
    "        </div>"+
    "        <div id=\"errorTextbox\" style=\"display: none;\"> "+     
    "            <p>English:Please refresh the page</p>"+      
    "            <p>Chinese:请刷新页面</p>"+
    "        </div>"+
    "    </div>"+
    "</body>"+
    "</html>";
    
  server.send(200, "text/html", myhtmlPage); 
  printf("The user visited the home page\r\n");
  
}
void handleRTCPage() {      
    String rtcPage = String("") + 
    "<html>" + 
    "<head>" + 
    "    <meta charset=\"utf-8\">" + 
    "    <title>" RELAY_DEVICE_NAME "</title>" +
    "    <style>" + 
    "        body {" + 
    "            font-family: Arial, sans-serif;" + 
    "            background-color: #f0f0f0;" + 
    "            margin: 0;" + 
    "            padding: 0;" + 
    "        }" + 
    "        .header {" + 
    "            text-align: center;" + 
    "            padding: 20px 0;" + 
    "            background-color: #333;" + 
    "            color: #fff;" + 
    "            margin-bottom: 20px;" + 
    "        }" + 
    "        .container {" + 
    "            max-width: 600px;" + 
    "            margin: 10px auto;" + 
    "            padding: 20px;" + 
    "            background-color: #fff;" + 
    "            border-radius: 5px;" + 
    "            box-shadow: 0 0 5px rgba(0, 0, 0, 0.3);" + 
    "        }" + 
    "        .form-group {" + 
    "            margin-bottom: 15px;" + 
    "        }" + 
    "        .form-group label {" + 
    "            display: block;" + 
    "            font-weight: bold;" + 
    "        }" + 
    "        .form-group input {" + 
    "            width: 80px;" + 
    "            height: 25px;" + 
    "            padding: 4px;" + 
    "            margin-top: 5px;" + 
    "            border: 1px solid #ddd;" + 
    "            border-radius: 4px;" + 
    "            box-sizing: border-box;" + 
    "            text-align: right; " + 
    "        }" + 
    "        .form-group select {" + 
    "            width: 80px;" + 
    "            height: 25px;" + 
    "            padding: 4px;" + 
    "            margin-top: 5px;" + 
    "            border: 1px solid #ddd;" + 
    "            border-radius: 4px;" + 
    "            box-sizing: border-box;" + 
    "            text-align: right; " + 
    "        }" + 
    "        .form-group .btn {" + 
    "            padding: 10px 20px;" + 
    "            background-color: #333;" + 
    "            color: white;" + 
    "            border: none;" + 
    "            border-radius: 5px;" + 
    "            cursor: pointer;" + 
    "        }" + 
    "        .form-group .btn:hover {" + 
    "            background-color: #555;" + 
    "        }" + 
    "        .Events{"+
    "            font-size: 13px;"+
    "            word-wrap: break-word;"+
    "            overflow-wrap: break-word;"+
    "            max-width: 100%;"+
    "            white-space: nowrap;"+
    "            padding: 2px;"+
    "        }"+
    "        .Events button {"+
    "            float: right;" + 
    "            margin-left: 1px;" + 
    "        }"+
    "        .Events li {"+
    "            font-size: 13px;" + 
    "        }"+
    "        nav {" + 
    "            margin: 15px 0;" + 
    "            text-align: center;" + 
    "        }" + 
    "        nav a {" + 
    "            padding: 10px 50px;" + 
    "            background-color: #333;" + 
    "            color: white;" + 
    "            text-decoration: none;" + 
    "            font-weight: bold;" + 
    "            border-radius: 5px;" + 
    "        }" + 
    "        nav a.relayControlActive {" + 
    "            background-color: #555;" + 
    "        }" + 
    "        nav a.rtcEventActive {" + 
    "            background-color: #fff;" + 
    "            color: #333;" + 
    "            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3), 0 1px 3px rgba(0, 0, 0, 0.1);" + 
    "            transform: translateY(-4px);" + 
    "            transition: all 0.2s ease-in-out;" + 
    "        }" + 
    "    </style>" + 
    "</head>" + 
    "<body>" +
    "    <script defer=\"defer\">" +
    "        function getRtcEventData() {" +
    "            var dateBox1 = document.getElementById('DateBox1').value;" +
    "            var dateBox2 = document.getElementById('DateBox2').value;" +
    "            var dateBox3 = document.getElementById('DateBox3').value;" +
    "            var week = document.getElementById('Week').value;" +
    "            var timeBox1 = document.getElementById('TimeBox1').value;" +
    "            var timeBox2 = document.getElementById('TimeBox2').value;" +
    "            var timeBox3 = document.getElementById('TimeBox3').value;" +
    "            var relayCH1 = document.getElementById('RelayCH1').value;" +
    "            var cycleBox = document.getElementById('CycleBox').value;" +
    "            var WebData = " +
    "                'Date: ' + dateBox1 + '/' + dateBox2 + '/' + dateBox3 + '  ' + '\\n' + " + 
    "                'Week: ' + week + '  ' + '\\n' + " + 
    "                'Time: ' + timeBox1 + ':' + timeBox2 + ':' + timeBox3 +  '  ' + '\\n' + " + 
    "                'Relay CH1: ' + relayCH1 + '  ' + '\\n' + " + 
    "                'Cycle: ' + cycleBox + '  ' + '\\n' ;" + 
    "            var xhr = new XMLHttpRequest();" +
    "            xhr.open('GET', '/NewEvent?data=' + encodeURIComponent(WebData), true);" +
    "            xhr.send();" +
    "        }" +
    "        function deleteEvent(eventId) {"+
    "            var xhr = new XMLHttpRequest();"+
    "            var EventId = eventId;"+
    "            xhr.open('GET', '/DeleteEvent?id=' + EventId, true);"+
    "            xhr.send();"+
    "        }"+
    "        function updateList(data) {"+
    "            var list = document.getElementById(\"myList\");"+
    "            list.innerHTML = \'\'; "+
    "            for (let i = 0; i < data.eventCount; i++) {"+
    "                var newItem = document.createElement(\"li\");"+
    "                var eventContent = data[\"eventStr\" + (i + 1)].replace(/\\n/g, \"<br>\");"+
    "                newItem.innerHTML = eventContent;"+
    "                var eventButton = document.createElement(\"button\");"+
    "                eventButton.textContent = \"Delete\" + \"Event\" + (i + 1);"+
    "                eventButton.onclick = function() {"+
    "                    deleteEvent(i+1);"+
    "                };"+
    "                newItem.style.display = 'flex';"+
    "                newItem.style.justifyContent = 'space-between';"+
    "                newItem.style.alignItems = 'center';"+
    "                newItem.appendChild(eventButton);"+
    "                list.appendChild(newItem);"+
    "            }"+
    "        }"+
    "        function upTime() {"+
    "            var xhr = new XMLHttpRequest();"+
    "            xhr.open('GET', '/getTimeAndEvent', true); "+
    "            xhr.onreadystatechange = function() {"+
    "                if (xhr.readyState === 4 && xhr.status === 200) {"+
    "                    var data = JSON.parse(xhr.responseText); "+
    "                    document.getElementById(\"Time\").textContent = data.time;"+
    "                    updateList(data); "+
    "                }"+
    "            };"+
    "            xhr.send();"+
    // "        fetch(\'/getTimeAndEvent\')"+
    // "            .then(response => response.json())"+
    // "            .then(data => {"+
    // "                document.getElementById(\"Time\").textContent = data.time;"+
    // "                updateList(data);"+
    // "            })"+
    // "            .catch(error => {"+
    // "                console.error(\'Error fetching time and events:\', error);"+
    // "            });"+
    "        }"+
    "        var refreshInterval = 400;"+                                     
    "        setInterval(upTime, refreshInterval);"+         
    "    </script>" +
    "    <div class=\"header\">"+
    "        <h1>" RELAY_DEVICE_NAME "</h1>" +
    "    </div>" + 
    "    <nav>" + 
    "        <a href=\"/\" id=\"relayControlLink\" class=\"relayControlActive\">Relay Control</a>" +  
    "        <a href=\"/RTC_Event\" id=\"rtcEventLink\" class=\"rtcEventActive\">RTC Event</a>" +  
    "    </nav>" + 
    "    <div class=\"container\">" +  
    "        <div class=\"form-group\">" + 
    "            <label for=\"Date\">Date:(example:2024/12/20)</label>" + 
    "            <input type=\"text\" id=\"DateBox1\" style=\"width: 50px;\" value=\"2024\">" + 
    "            <span>/</span>" + 
    "            <input type=\"text\" id=\"DateBox2\" style=\"width: 50px;\" value=\"12\">" + 
    "            <span>/</span>" + 
    "            <input type=\"text\" id=\"DateBox3\" style=\"width: 50px;\" value=\"20\">" + 
    "            <span>&nbsp;&nbsp;&nbsp;</span>" + 
    "            <select id=\"Week\" style=\"width: 150px;\">" + 
    "                <option value=\"1\">星期一(Monday)</option>" + 
    "                <option value=\"2\">星期二(Tuesday)</option>" + 
    "                <option value=\"3\">星期三(Wednesday)</option>" + 
    "                <option value=\"4\">星期四(Thursday)</option>" + 
    "                <option value=\"5\">星期五(Friday)</option>" + 
    "                <option value=\"6\">星期六(Saturday)</option>" + 
    "                <option value=\"0\">星期日(Sunday)</option>" + 
    "            </select>" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <label for=\"Time\">Time:(example:16:51:21)</label>" + 
    "            <input type=\"text\" id=\"TimeBox1\" style=\"width: 50px;\" value=\"0\">" + 
    "            <span>:</span>" + 
    "            <input type=\"text\" id=\"TimeBox2\" style=\"width: 50px;\" value=\"0\">" + 
    "            <span>:</span>" + 
    "            <input type=\"text\" id=\"TimeBox3\" style=\"width: 50px;\" value=\"0\">" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <label for=\"relayBox\">Relay:</label>" +
    "            <span>CH1: </span>" + 
    "            <select id=\"RelayCH1\" style=\"width: 100px;\">" + 
    "                <option value=\"2\">保留(Retain)</option>" + 
    "                <option value=\"0\">关闭(close)</option>" + 
    "                <option value=\"1\">打开(Open)</option>" + 
    "            </select>" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <label for=\"cycleBox\">Cycle:</label>" + 
    "            <select id=\"CycleBox\" style=\"width: 150px;\">" + 
    "                <option value=\"0\">无重复(Aperiodicity)</option>" + 
    "                <option value=\"1\">每天(everyday)</option>" + 
    "                <option value=\"2\">每周(Weekly)</option>" + 
    "                <option value=\"3\">每月(monthly)</option>" + 
    "            </select>" + 
    "        </div>" + 
    "        <div class=\"form-group\">" + 
    "            <button class=\"btn\" id=\"NewEvent\" onclick=\"getRtcEventData()\">New Event</button>" + 
    "        </div>" + 
    "    </div>" + 
    "    <div class=\"container\">" +  
    "        <div class=\"form-group\">" + 
    "            <span id=\"Time\"></span> "+
    "        </div>" + 
    "        <div class=\"Events\">" + 
    "            <ul id=\"myList\"> "+
    "            </ul> "+
    "        </div>" + 
    "    </div>" + 
    "</body>" + 
    "</html>"; 

    server.send(200, "text/html", rtcPage);   
    printf("The user visited the RTC Event page\r\n"); 
}



void handleGetData() {
  String json = "[";
  for (int i = 0; i < Relay_Number_MAX; i++) {
    json += String(Relay_Flag[i]);
    if (i < Relay_Number_MAX - 1) {
      json += ",";
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleSwitch(uint8_t ledNumber) {
  uint8_t Data[1]={0};
  Data[0]=ledNumber+48;
  Relay_Analysis(Data,WIFI_Mode);
  server.send(200, "text/plain", "OK");
}
void handleSwitch1() { handleSwitch(1); }
void handleSwitch9() { handleSwitch(9); }
void handleSwitch0() { handleSwitch(0); }

void handleNewEvent() {
  const String text = server.arg("data");
  int year, month, day, weekday, hour, minute, second, relay, cycle;
  int end = 0;
  const int fields = sscanf(text.c_str(),
    "Date: %4d/%2d/%2d Week: %1d Time: %2d:%2d:%2d Relay CH1: %1d Cycle: %1d %n",
    &year, &month, &day, &weekday, &hour, &minute, &second, &relay, &cycle, &end);
  if (fields != 9 || end == 0 || text[end] != '\0' || year < 1 || month < 1 || day < 1 ||
      hour < 0 || minute < 0 || second < 0 || relay < STATE_Closs || relay > STATE_Retain ||
      cycle < Repetition_NONE || cycle > Repetition_monthly) {
    server.send(400, "text/plain", "Invalid RTC event.");
    return;
  }

  const datetime_t time = {
    static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day),
    static_cast<uint8_t>(weekday), static_cast<uint8_t>(hour), static_cast<uint8_t>(minute),
    static_cast<uint8_t>(second),
  };
  if (!TimerEvent_Add(time, static_cast<Status_adjustment>(relay), static_cast<Repetition_event>(cycle))) {
    server.send(400, "text/plain", "Invalid RTC event or event list full.");
    return;
  }
  server.send(200, "text/plain", "OK");
}

void handleUpTimeAndEvent() {
  server.send(200, "application/json", TimerEvent_Json());
}

void handleDeleteEvent() {
  const String id = server.arg("id");
  const char *value = id.c_str();
  char *end = nullptr;
  const long eventNumber = strtol(value, &end, 10);
  if (end == value || *end != '\0' || !TimerEvent_Del_Number(eventNumber)) {
    server.send(400, "text/plain", "Invalid event ID.");
    return;
  }
  server.send(200, "text/plain", "Event " + id + " deleted.");
}

void WIFI_Init()
{
  xTaskCreatePinnedToCore(
    WifiStaTask,    
    "WifiStaTask",   
    4096,                
    NULL,                 
    3,                   
    NULL,                 
    0                   
  );
}


void WifiStaTask(void *parameter) {
  server.on("/", handleRoot);            // Relay Control page
  server.on("/getData", handleGetData);
  server.on("/Switch1", handleSwitch1);
  server.on("/RelayOn"  , handleSwitch9);
  server.on("/RelayOff" , handleSwitch0);

  server.on("/RTC_Event", handleRTCPage);      // RTC Event page
  server.on("/NewEvent" , handleNewEvent);
  server.on("/getTimeAndEvent", handleUpTimeAndEvent);
  server.on("/DeleteEvent", handleDeleteEvent);

  uint8_t Count = 0;
  WiFi.mode(WIFI_STA);                                   
  WiFi.setSleep(true);      
  WiFi.begin(ssid, password);                         // Connect to the specified Wi-Fi network
  while(1){
    if(WiFi.status() != WL_CONNECTED)
    {
      WIFI_Connection = 0;
      printf(".\n");  
      Count++;
      if(Count >= 10){
        Count = 0;
        printf("\r\n"); 
        WiFi.disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        WiFi.mode(WIFI_OFF);
        vTaskDelay(pdMS_TO_TICKS(100));
        WiFi.mode(WIFI_STA);
        vTaskDelay(pdMS_TO_TICKS(100));
        WiFi.begin(ssid, password);
      }
    }
    else{
      WIFI_Connection = 1;
      IPAddress myIP = WiFi.localIP();
      printf("IP Address: ");
      sprintf(ipStr, "%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
      printf("%s\r\n", ipStr);
      
      server.begin(); 
      printf("Web server started\r\n"); 

      while (WiFi.status() == WL_CONNECTED){
        server.handleClient(); // Processing requests from clients
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      server.stop();
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  vTaskDelete(NULL);
}
