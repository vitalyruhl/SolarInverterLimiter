#pragma once

// [MOCKED!] Only the configuration and logging interfaces used by the driver.
template<class T> struct ConfigOptions {
    const char *key;
    const char *name;
    const char *category;
    T defaultValue;
    int sortOrder;
};

template<class T> class Config {
public:
    explicit Config(const ConfigOptions<T> &options) : value_(options.defaultValue) {}
    T get() const { return value_; }
    void set(T value) { value_ = value; }

private:
    T value_;
};

class ConfigManagerClass {
public:
    template<class T> void addSetting(T *) {}
};

#define CM_LOG(...) ((void)0)
#define CM_LOG_VERBOSE(...) ((void)0)
