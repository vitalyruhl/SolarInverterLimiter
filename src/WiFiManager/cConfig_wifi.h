#ifndef CCONFIG_WIFI_H
#define CCONFIG_WIFI_H
#pragma once

#include <WebServer.h>

class cConfig_wifi {
public:
    virtual void attachWebEndpoint(WebServer& server) {
        // default: do nothing, its a pure virtual function to not vorget the implementation in the derived class
    }
    virtual ~cConfig_wifi() = default; // Destruktor
};

#endif //CCONFIG_WIFI_H
