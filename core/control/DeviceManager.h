#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <vector>
#include <string>
#include "../devices/Appliance.h"

class DeviceManager {
private:
    std::vector<Appliance*> devices;

public:
    ~DeviceManager();

    void addDevice(Appliance* device);

    Appliance* findDevice(const std::string& id);

    void turnOn(const std::string& id);
    void turnOff(const std::string& id);

    void showAllDevices() const;

    int getDeviceCount() const;
};

#endif