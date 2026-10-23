// [MOCKED!] Arduino String and runtime sink only; real registration helpers, CM types and ArduinoJson.
#include <string>
class String {
    std::string value_;
public:
    String() = default;
    String(const char* value) : value_(value ? value : "") {}
    bool isEmpty() const { return value_.empty(); }
    size_t length() const { return value_.length(); }
    const char* c_str() const { return value_.c_str(); }
    String& operator+=(const String& other) { value_ += other.value_; return *this; }
    bool operator==(const String& other) const { return value_ == other.value_; }
};
#include <ArduinoJson.h>
namespace ArduinoJson {
template<> struct Converter<String> {
    static void toJson(const String& value, JsonVariant json) { json.set(value.c_str()); }
};
}
#include "runtime/CompactLiveMetadata.h"
#include "sbt/SbtRuntimeMetadata.h"
#include <cassert>
#include <functional>
#include <map>
#include <set>
#include <type_traits>
#include <vector>
struct Record { RuntimeFieldBase base; RuntimeFieldKind kind; std::string unit; int precision; };
struct Sink {
    std::vector<Record> fields;
    struct Provider {String source;std::function<void(JsonObject&)> fill;int order;};
    std::vector<Provider> providers;
    int failAfter = -1;
    unsigned attempts = 0;
    template<class T> RuntimeFieldId addRuntimeMeta(const T& m) {
        ++attempts;
        if(failAfter == 0) return {};
        if(failAfter > 0) --failAfter;
        RuntimeFieldKind kind;
        if constexpr(std::is_same_v<T,RuntimeFloatMeta>) kind=RuntimeFieldKind::Float;
        else if constexpr(std::is_same_v<T,RuntimeIntMeta>) kind=RuntimeFieldKind::Int;
        else if constexpr(std::is_same_v<T,RuntimeBoolMeta>) kind=RuntimeFieldKind::Bool;
        else if constexpr(std::is_same_v<T,RuntimeTextMeta>) kind=RuntimeFieldKind::Text;
        else static_assert(!sizeof(T),"Only compact measurement types are accepted");
        Record r{m.base,kind,"",kind==RuntimeFieldKind::Int?0:2};
        if constexpr(std::is_same_v<T,RuntimeFloatMeta> || std::is_same_v<T,RuntimeIntMeta>) r.unit=m.unit.c_str();
        if constexpr(std::is_same_v<T,RuntimeFloatMeta>) r.precision=m.precision;
        fields.push_back(r);
        return {static_cast<uint32_t>(fields.size())};
    }
    void addRuntimeProvider(const String& source,std::function<void(JsonObject&)> fill,int order) {providers.push_back({source,fill,order});}
};
int main() {
    Sink receiver;
    assert(sbt::registerRuntimeMetadata(receiver));
    assert(receiver.fields.size()==30 && receiver.providers.empty());
    std::set<std::string> keys;
    for(const auto& field:receiver.fields) {
        assert(keys.insert(field.base.key.c_str()).second);
        assert(field.base.sourceGroup=="sbt" && field.base.page=="BMS / MPPT 150/35");
        assert(field.base.group==field.base.card && !field.base.label.isEmpty());
    }
    for(const char* key:{"bms_voltage_v","bms_current_a","bms_power_w","bms_remaining_ah",
        "bms_full_ah","bms_temperature_max_c","bms_temperature_min_c","bms_cell_min_v",
        "bms_cell_max_v","bms_cell_delta_v","bms_cell_count","bms_temperature_sensor_count",
        "victron_pv_voltage_v","victron_pv_current_a","victron_battery_power_w"}) assert(!keys.count(key));
    for(const char* key:{"bms_soc_pct","bms_temperature_1_c","bms_temperature_2_c","bms_protection",
        "victron_solar_power_w","victron_battery_voltage_v","victron_charge_current_a","bms_age","victron_age","emergency_state"}) assert(keys.count(key));
    for(const auto& f:receiver.fields) {
        if(f.base.key=="bms_temperature_1_c") assert(f.base.label=="NTC 1 (Top)" && f.unit=="\u00B0C");
        if(f.base.key=="bms_temperature_2_c") assert(f.base.label=="NTC 2 (Bottom)" && f.unit=="\u00B0C");
    }
    for(int failAt:{0,20,28}) {Sink failed;failed.failAfter=failAt;assert(!sbt::registerRuntimeMetadata(failed));assert(failed.attempts==unsigned(failAt+1) && failed.fields.size()==size_t(failAt));}
    Sink heap;
    assert(sil::registerHeapMetadata(heap));
    assert(heap.fields.size()==3);
    for(const auto& f:heap.fields) assert(f.base.page=="Limiter" && f.base.card=="Limiter");
    const auto h=sil::heapSnapshot(2000,1000,800);
    assert(h.used==1000 && h.free==1000 && h.fragmentation==20);
    assert(sil::heapSnapshot(2000,0,0).fragmentation==100);
    assert(sil::heapSnapshot(100,200,300).used==0);
    Sink values;
    float reading=12.345f;int slots=3;bool enabled=true;
    {
        sil::CompactLiveGroup group(values,"Limiter","Limiter","Limiter","Limiter Status");
        group.value<RuntimeFloatMeta>("voltage",[&]{return reading;},"Voltage","V",3,1);
        group.value<RuntimeIntMeta>("slots",[&]{return slots;},"Slots","",0,2);
        group.value<RuntimeBoolMeta>("enabled",[&]{return enabled;},"Enabled","",2,3);
        group.value<RuntimeTextMeta>("status",[]{return String("ready");},"Status","",2,4);
        assert(group.valid());
    }
    assert(values.fields.size()==4 && values.providers.size()==4);
    JsonDocument doc;JsonObject object=doc.to<JsonObject>();
    for(const auto& p:values.providers){assert(p.source=="Limiter");p.fill(object);}
    assert(doc["voltage"].as<float>()==reading && doc["slots"].as<int>()==3 && doc["enabled"].as<bool>());
    assert(std::string(doc["status"].as<const char*>())=="ready");
    reading=2.5f;values.providers[0].fill(object);assert(doc["voltage"].as<float>()==2.5f);
    assert(values.fields[0].base.group=="Limiter Status" && values.fields[0].precision==3 && values.fields[0].unit=="V");
    for(unsigned i=0;i<4;++i)assert(values.providers[i].order==int(i)+1);
    Sink failed;failed.failAfter=0;
    sil::CompactLiveGroup group(failed,"sensors","Temperature Sensor","Sensors","Sensor Readings");
    group.value<RuntimeFloatMeta>("temp",[]{return 1.0f;},"Temperature","C",1,2);
    group.value<RuntimeFloatMeta>("hum",[]{return 2.0f;},"Humidity","%",1,11);
    assert(!group.valid() && failed.fields.empty() && failed.providers.empty() && failed.attempts==1);
    std::puts("[MOCKED!] SIL compact metadata: 30 visible receiver fields and guarded heap tuple, units/routing/kinds/failures and real provider helper passed");
}
