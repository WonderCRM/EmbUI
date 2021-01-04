// This framework originaly based on JeeUI2 lib used under MIT License Copyright (c) 2019 Marsel Akhkamov
// then re-written and named by (c) 2020 Anton Zolotarev (obliterator) (https://github.com/anton-zolotarev)
// also many thanks to Vortigont (https://github.com/vortigont), kDn (https://github.com/DmytroKorniienko)
// and others people

#include "EmbUI.h"
#include "ui.h"

#ifdef EMBUI_DEBUG
 #include "MemoryInfo.h"
#endif

#ifndef MAX_WS_CLIENTS
#define MAX_WS_CLIENTS 4
#endif
#ifndef PUB_PERIOD
#define PUB_PERIOD 10000            // Publication period, ms
#endif
#define SECONDARY_PERIOD 300U       // second handler timer, ms

// Update defs
#ifndef ESP_IMAGE_HEADER_MAGIC
 #define ESP_IMAGE_HEADER_MAGIC 0xE9
#endif

#ifndef GZ_HEADER
 #define GZ_HEADER 0x1F
#endif

#ifdef ESP32
 #define U_FS   U_SPIFFS
#endif


EmbUI embui;

void section_main_frame(Interface *interf, JsonObject *data) {}
void pubCallback(Interface *interf){}
String httpCallback(const String &param, const String &value, bool isSet) { return String(); }

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
    if(type == WS_EVT_CONNECT){
        LOG(printf_P, PSTR("UI: ws[%s][%u] connect MEM: %u\n"), server->url(), client->id(), ESP.getFreeHeap());

        Interface *interf = new Interface(&embui, client);
        section_main_frame(interf, nullptr);
        embui.send_pub();
        delete interf;

    } else
    if(type == WS_EVT_DISCONNECT){
        LOG(printf_P, PSTR("ws[%s][%u] disconnect\n"), server->url(), client->id());
    } else
    if(type == WS_EVT_ERROR){
        LOG(printf_P, PSTR("ws[%s][%u] error(%u): %s\n"), server->url(), client->id(), *((uint16_t*)arg), (char*)data);
    } else
    if(type == WS_EVT_PONG){
        LOG(printf_P, PSTR("ws[%s][%u] pong[%u]: %s\n"), server->url(), client->id(), len, (len)?(char*)data:"");
    } else
    if(type == WS_EVT_DATA){
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if(info->final && info->index == 0 && info->len == len){
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, data, info->len);

            const char *pkg = doc["pkg"];
            if (!pkg) return;
            if (!strcmp(pkg, "post")) {
                JsonObject data = doc["data"];
                embui.post(data);
            }
        }
  }
}

void EmbUI::post(JsonObject data){
    section_handle_t *section = new section_handle_t();
    section->callback = nullptr;       // make sure callback is null
    int count = 0;
    Interface *interf = new Interface(this, &ws, 512);
    interf->json_frame_value();

    for (JsonPair kv : data) {
        String key = kv.key().c_str(), val = kv.value();
        LOG(printf_P, PSTR("DEBUG: Key:val = '%s:%s'\n"), key.c_str(), val.c_str());

        if (val != FPSTR(P_null)) {
            LOG(printf_P, PSTR("DEBUG: val='%s' is not null, add value\n"), val.c_str());
            interf->value(key, val);
            ++count;
        }

        // сначала ищем секцию точно совпадающую по имени
        auto lookup = section_handle.find(key.c_str());
        if (lookup != section_handle.end()){
            section->name = lookup->first.c_str();      // вообще это имя здесь нужно только для дебаг лога
            section->callback = lookup->second;
            LOG(printf_P, PSTR("DEBUG: found section: '%s'!\n"), lookup->first.c_str());
            continue;
        } else {
            LOG(println, "exact section not found");
        }

        // не нашли точно такуюже секцию, проверяем все ключи на шаблон вида 'section*'
        for (const auto& x : section_handle){
            const char *mall = strchr(x.first.c_str(), '*');
            if (!mall)
                continue;

            unsigned len = mall - x.first.c_str() - 1;
            if (strncmp(x.first.c_str(), key.c_str(), len) == 0) {
                section->name = x.first.c_str();      // вообще это имя здесь нужно только для дебаг лога
                section->callback = x.second;
            }
        }
    }

    if (count) {
        interf->json_frame_flush();
    } else {
        interf->json_frame_clear();
    }
    delete interf;

    if (section->callback) {
        LOG(printf_P, PSTR("\nUI: POST SECTION: %s\n\n"), section->name.c_str());
        Interface *interf = new Interface(this, &ws);
        section->callback(interf, &data);   // выполняем найденую секцию
        delete interf;
    } else {
        //LOG(printf_P, PSTR("\nUI: No section found for: %s\n\n"), key.c_str());
        LOG(printf, "Sec not found, printing full list of %d elements:\n", section_handle.size());
        if (section_handle.empty()){
            LOG(println, ", but map is empty!!!");
        }
        //section_handle.begin();
        /*
        for (const auto &p : section_handle) {
            LOG(print, ".");
            LOG(printf, "sec: %s\n", p.first.c_str());
        }
        */
       //
        for (auto p = section_handle.cbegin(); p != section_handle.cend(); ++p) {
            LOG(print, ".");
            LOG(printf, "sec: %s\n", p->first.c_str());
        }
        //
       /*
       std::for_each(section_handle.begin(), section_handle.end(),
            [](const )
       )
       */
        /*  C++17 iterator unsupported
        for (const auto & [key, value ] : section_handle) {
            LOG(print, ".");
            LOG(printf, "sec: %s\n", key);
            //std::cout << p.first << " => " << p.second << '\n';
        }
        */

    }

    delete section;
}

void EmbUI::send_pub(){
    if (!ws.count()) return;
    Interface *interf = new Interface(this, &ws, 512);
    pubCallback(interf);
    delete interf;
}

void EmbUI::var(const String &key, const String &value, bool force)
{
    // JsonObject of N element	8 + 16 * N
    //unsigned len = key.length() + value.length() + 16;
    //size_t cap = cfg.capacity(), mem = cfg.memoryUsage();

    LOG(printf_P, PSTR("UI UPDATE: key (%s) value (%s)\n"), key.c_str(), value.substring(0, 15).c_str());
    force ? cfg->insert(key, value) : cfg->update(key, value);

/*
    if (force){
        LOG(printf_P, PSTR("UI ERROR: KEY (%s) is NOT initialized!\n"), key.c_str());
        return;
    }
*/
/*
    if (cap - mem < len) {
        cfg.garbageCollect();
        LOG(printf_P, PSTR("UI: garbage cfg %u(%u) of %u\n"), mem, cfg.memoryUsage(), cap);

    }
    if (cap - mem < len) {
        LOG(printf_P, PSTR("UI ERROR: KEY (%s) NOT WRITE !!!!!!!!\n"), key.c_str());
        return;
    }
*/
    sysData.isNeedSave = true;

    LOG(printf_P, PSTR("UI CFG mem size: %u\n"), cfg->size());

    // if (mqtt_remotecontrol) {
    //     publish(String(F("embui/set/")) + key, value, true);
    // }
}

void EmbUI::var_create(const String &key, const String &value)
{
    cfg->emplace(key, value);   // only create missing/new keys, do NOT update existing values (from config.json)
    LOG(printf_P, PSTR("UI CREATE key: (%s) value: (%s)\n"), key.c_str(), value.substring(0, 15).c_str());
}

void EmbUI::section_handle_add(const String &name, buttonCallback response)
{
/*
    section_handle_t *section = new section_handle_t;
    section->name = name.c_str();
    section->callback = response;
    section_handle.add(section);
*/
    section_handle.emplace(name.c_str(), response);
    //section_handle.emplace(std::make_pair(name.c_str(), response));
    /*
    section_handle.emplace(std::piecewise_construct,
            std::forward_as_tuple(name.c_str()),
            std::forward_as_tuple(response));
    */

    LOG(printf_P, PSTR("UI REGISTER: %s, total: %d\n"), name.c_str(), section_handle.size());

        auto lookup = section_handle.find(name.c_str());
        if (lookup != section_handle.end()){
            //section->name = lookup->first.c_str();      // вообще это имя здесь нужно только для дебаг лога
            //section->callback = lookup->second;
            LOG(printf_P, PSTR("DEBUG: key registered: %s\n"), lookup->first.c_str());
        } else {
            LOG(println, "was unable to reg key!!!");
        }

}

/**
 * Возвращает указатель на строку со значением параметра из конфига
 * В случае отсутствующего параметра возвращает пустой указатель
 */
const char* EmbUI::param(const char* key)
{
    const char* value = (*cfg)[key].c_str();
    if (value){
        LOG(printf_P, PSTR("UI READ: key (%s) value (%s)\n"), key, value);
    }

    return value;
}

/**
 * обертка над param в виде String
 * В случае несуществующего ключа возвращает пустой String("")
 */
String EmbUI::param(const String &key)
{
    LOG(printf_P, PSTR("UI READ: key (%s) value (%s)\n"), key.c_str(), (*cfg)[key].c_str());
    return (*cfg)[key];
}

String EmbUI::deb()
{
    return cfg->json();
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, FPSTR(PGmimetxt), FPSTR(PG404));
}

void EmbUI::begin(){
    load();                 // try to load config from file
    create_sysvars();       // create system variables (if missing)
    create_parameters();    // weak function, creates user-defined variables

    LOG(println, String(F("UI CONFIG: ")) + embui.deb());
    #ifdef ESP8266
        e1 = WiFi.onStationModeGotIP(std::bind(&EmbUI::onSTAGotIP, this, std::placeholders::_1));
        e2 = WiFi.onStationModeDisconnected(std::bind(&EmbUI::onSTADisconnected, this, std::placeholders::_1));
        e3 = WiFi.onStationModeConnected(std::bind(&EmbUI::onSTAConnected, this, std::placeholders::_1));
        e4 = WiFi.onWiFiModeChange(std::bind(&EmbUI::onWiFiMode, this, std::placeholders::_1));
    #elif defined ESP32
        WiFi.onEvent(std::bind(&EmbUI::WiFiEvent, this, std::placeholders::_1, std::placeholders::_2));
        //WiFi.onEvent(std::bind(&TimeProcessor::WiFiEvent, &timeProcessor, std::placeholders::_1, std::placeholders::_2));
    #endif

    // восстанавливаем настройки времени
    timeProcessor.tzsetup(param(FPSTR(P_TZSET)).c_str());
    timeProcessor.setcustomntp(param(FPSTR(P_userntp)).c_str());

    // запускаем WiFi
    wifi_init();
    
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

#ifdef USE_SSDP
    ssdp_begin(); LOG(println, F("Start SSDP"));
#endif

/*
#ifdef ESP32
  server.addHandler(new SPIFFSEditor(LittleFS, http_username,http_password));
#elif defined(ESP8266)
  //server.addHandler(new SPIFFSEditor(http_username,http_password, LittleFS));
  server.addHandler(new SPIFFSEditor(F("esp8266"),F("esp8266"), LittleFS));
#endif
*/

    server.on(PSTR("/version"), HTTP_ANY, [this](AsyncWebServerRequest *request) {
        request->send(200, FPSTR(PGmimetxt), F("EmbUI ver: " TOSTRING(EMBUIVER)));
    });

    server.on(PSTR("/cmd"), HTTP_ANY, [this](AsyncWebServerRequest *request) {
        String result; 
        int params = request->params();
        for(int i=0;i<params;i++){
            AsyncWebParameter* p = request->getParam(i);
            if(p->isFile()){ //p->isPost() is also true
                //Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
            } else if(p->isPost()){
                //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
            } else {
                //Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
                result = httpCallback(p->name(), p->value(), !p->value().isEmpty());
            }
        }
        request->send(200, FPSTR(PGmimetxt), result);
    });

    server.on(PSTR("/config"), HTTP_ANY, [this](AsyncWebServerRequest *request) {
        String config = deb();
        AsyncWebServerResponse *response = request->beginResponse(200, FPSTR(PGmimejson), config);

        response->addHeader(FPSTR(PGhdrcachec), FPSTR(PGnocache));
        request->send(response);
    });

/*
    // может пригодится позже, файлы отдаются как статика

    server.on(PSTR("/config.json"), HTTP_ANY, [this](AsyncWebServerRequest *request) {
        request->send(LittleFS, FPSTR(P_cfgfile), String(), true);
    });

    server.on(PSTR("/events_config.json"), HTTP_ANY, [this](AsyncWebServerRequest *request) {
        request->send(LittleFS, F("/events_config.json"), String(), true);
    });
*/
    server.on(PSTR("/restart"), HTTP_ANY, [this](AsyncWebServerRequest *request) {
        sysData.shouldReboot = true;
        request->send(200, FPSTR(PGmimetxt), F("Ok"));
    });

    server.on(PSTR("/heap"), HTTP_GET, [this](AsyncWebServerRequest *request){
        String out = "Heap: "+String(ESP.getFreeHeap());
#ifdef EMBUI_DEBUG
    #ifdef ESP8266
        out += "\nFrac: " + String(getFragmentation());
    #endif
        out += "\nClient: " + String(ws.count());
#endif
        request->send(200, FPSTR(PGmimetxt), out);
    });

    // Simple Firmware Update Form
    server.on(PSTR("/update"), HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, FPSTR(PGmimehtml), F("<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>"));
    });

    server.on(PSTR("/update"), HTTP_POST, [this](AsyncWebServerRequest *request){
        sysData.shouldReboot = !Update.hasError();
        if (sysData.shouldReboot) {
            request->redirect("/");
        } else {
            AsyncWebServerResponse *response = request->beginResponse(500, FPSTR(PGmimetxt), F("FAIL"));
            response->addHeader(F("Connection"), F("close"));
            request->send(response);
        }
    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if (!index) {
            #ifdef ESP8266
        	int type = (data[0] == ESP_IMAGE_HEADER_MAGIC || data[0] == GZ_HEADER)? U_FLASH : U_FS;
                Update.runAsync(true);
                // TODO: разобраться почему под littlefs образ генерится чуть больше чем размер доступной памяти по константам
                size_t size = (type == U_FLASH)? request->contentLength() : (uintptr_t)&_FS_end - (uintptr_t)&_FS_start;
            #endif
            #ifdef ESP32
                int type = (data[0] == ESP_IMAGE_HEADER_MAGIC)? U_FLASH : U_FS;
                size_t size = (type == U_FLASH)? request->contentLength() : UPDATE_SIZE_UNKNOWN;
            #endif
            LOG(printf_P, PSTR("Updating %s, file size:%u\n"), (type == U_FLASH)? F("Firmware") : F("Filesystem"), request->contentLength());

            if (!Update.begin(size, type)) {
                Update.printError(Serial);
            }
        }
        if (!Update.hasError()) {
            if(Update.write(data, len) != len){
                Update.printError(Serial);
            }
        }
        if (final) {
            if(Update.end(true)){
                LOG(printf_P, PSTR("Update Success: %uB\n"), index+len);
            } else {
                Update.printError(Serial);
            }
        }
        uploadProgress(index + len, request->contentLength());
    });

    //First request will return 0 results unless you start scan from somewhere else (loop/setup)
    //Do not request more often than 3-5 seconds
    server.on(PSTR("/scan"), HTTP_GET, [](AsyncWebServerRequest *request){
        String json = F("[");
        int n = WiFi.scanComplete();
        if(n == -2){
            WiFi.scanNetworks(true);
        } else if(n){
            for (int i = 0; i < n; ++i){
            if(i) json += F(",");
            json += F("{");
            json += String(F("\"rssi\":"))+String(WiFi.RSSI(i));
            json += String(F(",\"ssid\":\""))+WiFi.SSID(i)+F("\"");
            json += String(F(",\"bssid\":\""))+WiFi.BSSIDstr(i)+F("\"");
            json += String(F(",\"channel\":"))+String(WiFi.channel(i));
            json += String(F(",\"secure\":"))+String(WiFi.encryptionType(i));
#ifdef ESP8266
            json += String(F(",\"hidden\":"))+String(WiFi.isHidden(i)?FPSTR(P_true):FPSTR(P_false)); // что-то сломали и в esp32 больше не работает...
#endif
            json += F("}");
            }
            WiFi.scanDelete();
            if(WiFi.scanComplete() == -2){
            WiFi.scanNetworks(true);
            }
        }
        json += F("]");
        request->send(200, FPSTR(PGmimejson), json);
        json = String();
    });

    // server all files from LittleFS root
    server.serveStatic("/", LittleFS, "/")
        .setDefaultFile(PSTR("index.html"))
        .setCacheControl(PSTR("max-age=14400"));

    server.onNotFound(notFound);

    server.begin();
}

void EmbUI::led(uint8_t pin, bool invert){
    if (pin == 31) return;
    sysData.LED_PIN = pin;
    sysData.LED_INVERT = invert;
    pinMode(sysData.LED_PIN, OUTPUT);
}

void EmbUI::handle(){
    if (sysData.shouldReboot) {
        LOG(println, F("Rebooting..."));
        delay(100);
        ESP.restart();
    }

#ifdef ESP8266
    MDNS.update();
#endif
    //_connected();
    mqtt_handle();
    udpLoop();

    static unsigned long timer = 0;
    if (timer + SECONDARY_PERIOD > millis()) return;
    timer = millis();

    //btn();
    //led_handle();
    autosave();
    ws.cleanupClients(MAX_WS_CLIENTS);

    //публикация изменяющихся значений
    static unsigned long timer_pub = 0;
    if (timer_pub + PUB_PERIOD > millis()) return;
    timer_pub = millis();
    send_pub();
}

/**
 * метод для установки коллбеков на системные события, типа:
 * - WiFi подключиля/отключился
 * - получено время от NTP
 */
void EmbUI::set_callback(CallBack set, CallBack action, callback_function_t callback){

    switch (action){
        case CallBack::STAConnected :
            set ? _cb_STAConnected = std::move(callback) : _cb_STAConnected = nullptr;
            break;
        case CallBack::STADisconnected :
            set ? _cb_STADisconnected = std::move(callback) : _cb_STADisconnected = nullptr;
            break;
        case CallBack::STAGotIP :
            set ? _cb_STAGotIP = std::move(callback) : _cb_STAGotIP = nullptr;
            break;
        case CallBack::TimeSet :
            set ? timeProcessor.attach_callback(callback) : timeProcessor.dettach_callback();
            break;
        default:
            return;
    }
};

/*
 * OTA update progress
 */
uint8_t uploadProgress(size_t len, size_t total){
    static int prev = 0;
    float part = total / 25.0;  // logger chunks
    int curr = len / part;
    uint8_t progress = 100*len/total;
    if (curr != prev) {
        prev = curr;
        LOG(printf_P, PSTR("%u%%.."), progress );
    }
    return progress;
}

/**
 * call to create system-dependent variables,
 * both run-time and persistent
 */
void EmbUI::create_sysvars(){
    LOG(println, F("UI: Creating system vars"));
    var_create(FPSTR(P_hostname), "");                // device hostname (autogenerated on first-run)
    var_create(FPSTR(P_APonly),  FPSTR(P_false));     // режим AP-only (только точка доступа), не трогать
    var_create(FPSTR(P_APpwd), "");                   // пароль внутренней точки доступа
    // параметры подключения к MQTT
    var_create(FPSTR(P_m_host), "");                   // MQTT server hostname
    var_create(FPSTR(P_m_port), "");                   // MQTT port
    var_create(FPSTR(P_m_user), "");                   // MQTT login
    var_create(FPSTR(P_m_pass), "");                   // MQTT pass
    var_create(FPSTR(P_m_pref), embui.mc);             // MQTT topic == use ESP MAC address
    var_create(FPSTR(P_m_tupd), F("30"));              // интервал отправки данных по MQTT в секундах
    // date/time related vars
    var_create(FPSTR(P_TZSET), "");                   // TimeZone/DST rule (empty value == GMT/no DST)
    var_create(FPSTR(P_userntp), "");                 // Backup NTP server
}
