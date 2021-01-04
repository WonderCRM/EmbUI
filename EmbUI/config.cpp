// This framework originaly based on JeeUI2 lib used under MIT License Copyright (c) 2019 Marsel Akhkamov
// then re-written and named by (c) 2020 Anton Zolotarev (obliterator) (https://github.com/anton-zolotarev)
// also many thanks to Vortigont (https://github.com/vortigont), kDn (https://github.com/DmytroKorniienko)
// and others people

#include "EmbUI.h"

#ifdef ESP8266
 #define FORMAT_LITTLEFS_IF_FAILED
#endif

#ifdef ESP32
 #ifndef FORMAT_LITTLEFS_IF_FAILED
  #define FORMAT_LITTLEFS_IF_FAILED true
 #endif
#endif

void EmbUI::save(const char *_cfg, bool force){
    if ((sysData.isNeedSave || force) && !sysData.cfgCorrupt){
      LittleFS.begin();
    } else {
        sysData.isNeedSave = false;
        return;
    }

    File configFile;
    if (_cfg == nullptr) {
        LOG(println, F("UI: Save default main config file"));
        LittleFS.rename(FPSTR(P_cfgfile),FPSTR(P_cfgfile_bkp));
        configFile = LittleFS.open(FPSTR(P_cfgfile), "w"); // PSTR("w") использовать нельзя, будет исключение!
    } else {
        LOG(printf_P, PSTR("UI: Save %s main config file\n"), _cfg);
        configFile = LittleFS.open(_cfg, "w"); // PSTR("w") использовать нельзя, будет исключение!
    }

    //String cfg_str;
    //serializeJson(cfg, cfg_str);
    if(cfg->jsize())
        configFile.print(cfg->json());
    configFile.close();

    //cfg.garbageCollect(); // несколько раз ловил Exception (3) предположительно тут, возвращаю пока проверенный способ
    
/*    delay(DELAY_AFTER_FS_WRITING); // задержка после записи    
    DeserializationError error;
    error = deserializeJson(cfg, cfg_str); // произошла ошибка, пытаемся восстановить конфиг
    if (error){
        load(_cfg);
    }
*/
    sysData.isNeedSave = false;
}

void EmbUI::autosave(){
    if (sysData.isNeedSave && millis() > astimer + sysData.asave*1000){
        save();
        LOG(println, F("UI: AutoSave"));
        astimer = millis();
    }
}

void EmbUI::load(const char *_cfg){
    
    File configFile;
    String path = _cfg ? String(_cfg) : String(FPSTR(P_cfgfile));
    char mode[] = "r";

    if (!openfile(path.c_str(), configFile, mode) ){    // 0x72 stands for asci 'r'
        path = FPSTR(P_cfgfile_bkp);    // в случае ошибки пробуем восстановить конфиг из резервной копии
        if (!openfile(path.c_str(), configFile, mode)){  // 0x72 stands for asci 'r'
            LOG(println, F("UI: Fatal error - can't read from either config or backup file"));
            return;
        }
    }

    byte* buff = nullptr;
    buff = (uint8_t*)malloc(configFile.size()+1);

    if (!buff){
        LOG(printf_P, PSTR("UI: Fatal error - can't allocate buffer memory %d\n"), configFile.size());
        return;
    }
    buff[configFile.size()+1] = '\0';   // make sure buff is terminated with null

    configFile.read(buff, configFile.size());
    configFile.close();

    cfg->destroy();
    if (cfg->jload((char *)buff) != 0){
        LOG(println, F("UI: Error - can't parse json config-file"));        
    }

/*
    DeserializationError error;
    if (configFile){
        error = deserializeJson(cfg, configFile);
        configFile.close();
    } else {
        configFile = _cfg ? LittleFS.open(_cfg, "r") : LittleFS.open(FPSTR(P_cfgfile_bkp), "r"); // в случае ошибки пробуем восстановить конфиг из резервной копии
        if (configFile){
            error = deserializeJson(cfg, configFile);
            configFile.close();
        } else {
            LOG(println, F("UI: Fatal error - missed configs"));
            return;
        }
    }

    if (error) {
        configFile = _cfg ? LittleFS.open(_cfg, "r") : LittleFS.open(FPSTR(P_cfgfile_bkp), "r"); // в случае ошибки пробуем восстановить конфиг из резервной копии
        if (configFile){
            error = deserializeJson(cfg, configFile);
            configFile.close();
        }
        if(error){
            // тут выясняется, что оба конфига повреждены, запрещаем запись
            LittleFS.check();
            LittleFS.gc();
            sysData.cfgCorrupt = true;
            LOG(print, F("UI: Critical JSON config deserializeJson error, config saving disabled: "));
            LOG(println, error.code());
        }
    }
*/
}

/**
 * Try to open file for read/write
 * return true on success, false on error
 * if read - returns false if file size is zero 
 */
bool EmbUI::openfile(const char *_path, File& _handler, const char *mode){
    int8_t retry_cnt = 5;

    // FORMAT_LITTLEFS_IF_FAILED works only for ESP32
    while(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED) && --retry_cnt){
        if(!retry_cnt){
            LOG(println, F("UI: Fatal error - can't initialize LittleFS"));
            return false;
        }
        delay(100);
    }

    _handler = LittleFS.open(_path, mode);

    if (!_handler)
        return false;

    if (mode[0] == 0x72)   // 0x72 stands for asci 'r'
        return (bool)_handler.size();

    return true;
}
