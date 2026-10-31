#pragma once
#include <ArduinoJson.h>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <type_traits>
#include <cstring>
// [MOCKED!] CM presentation, NVS byte I/O and FreeRTOS locking boundaries only.
class String : public std::string {public: using std::string::string;};
inline int portMAX_DELAY=0;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) (++*(mux))
#define portEXIT_CRITICAL(mux) (--*(mux))
using SemaphoreHandle_t=int *;
inline int modeTestUiMutex=0;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {return &modeTestUiMutex;}
inline void xSemaphoreTake(SemaphoreHandle_t m,int) {assert(*m==0);++*m;}
inline void xSemaphoreGive(SemaphoreHandle_t m) {assert(*m==1);--*m;}
class Preferences {
public:
    inline static std::vector<uint8_t> stored;
    inline static std::vector<uint8_t> staged;
    inline static unsigned writes=0;
    inline static bool failOpen=false,failWrite=false,failRead=false,failPromote=false,corruptRead=false,shortWrite=false,webTask=false;
    bool begin(const char *ns,bool) {assert(!std::strcmp(ns,"SILModes"));return !failOpen;}
    void end() {}
    size_t getBytesLength(const char *key) {return !std::strcmp(key,"profile")?stored.size():staged.size();}
    size_t getBytes(const char *key,void *data,size_t n) {
        const auto &bytes=!std::strcmp(key,"profile")?stored:staged;
        if(failRead || bytes.size()!=n) return 0;
        std::memcpy(data,bytes.data(),n);
        if(corruptRead) static_cast<uint8_t *>(data)[0]^=0x80;
        return n;
    }
    size_t putBytes(const char *key,const void *data,size_t n) {
        assert(!webTask);++writes;
        const bool active=!std::strcmp(key,"profile");
        if(failWrite || (failPromote && active)) return 0;
        if(shortWrite) return n-1;
        auto &bytes=active?stored:staged;
        bytes.assign(static_cast<const uint8_t *>(data),static_cast<const uint8_t *>(data)+n);return n;
    }
};
template<class T> struct ConfigOptions {const char *key,*name,*category;T defaultValue;int sortOrder;};
class BaseSetting {
protected: bool modified=false;
public:
    virtual ~BaseSetting()=default;
    virtual void load(Preferences &)=0;
    virtual void save(Preferences &)=0;
    virtual bool fromJSON(const JsonVariant &)=0;
    virtual void toJSON(JsonObject &) const=0;
    virtual void setDefault()=0;
};
template<class T> class Config : public BaseSetting {
    T value_;
public:
    explicit Config(const ConfigOptions<T> &options):value_(options.defaultValue) {}
    void set(const T &value) {value_=value;modified=true;}
    void toJSON(JsonObject &object) const override {
        assert(modeTestUiMutex==1);
        if constexpr(std::is_same_v<T,String>) object["value"]=value_.c_str();
        else object["value"]=value_;
    }
};
class ConfigManagerClass {
public:
    std::vector<BaseSetting *> settings;
    void addSetting(BaseSetting *setting) {settings.push_back(setting);}
    bool edit(size_t index,const char *json) {
        JsonDocument doc;assert(!deserializeJson(doc,json));
        Preferences::webTask=true;
        const bool accepted=settings[index]->fromJSON(doc.as<JsonVariant>());
        Preferences prefs;if(accepted) settings[index]->save(prefs);
        Preferences::webTask=false;return accepted;
    }
    std::string displayedMode() {
        JsonDocument doc;JsonObject object=doc.to<JsonObject>();settings[0]->toJSON(object);
        return object["value"].as<const char *>();
    }
};
