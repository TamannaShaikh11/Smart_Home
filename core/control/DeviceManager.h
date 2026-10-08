#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <vector>
#include <string>
#include <ctime>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "../devices/Appliance.h"

class DeviceManager
{
private:
    std::vector<Appliance*> devices;

    mutable std::mutex devicesMutex;
    std::condition_variable expiryCondition;
    std::thread expiryThread;

    bool stopping;

    std::function<void(const std::string&)> expiryCallback;

    void expiryWorker();
    void removeExpiredDevices();
    void registerDevice(
        Appliance* device,
        DeviceLifetime lifetime,
        std::time_t expiry
    );

public:
    DeviceManager();
    ~DeviceManager();

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // DeviceManager owns the pointer after successful registration.
    void addDevice(Appliance* device);

    // A duration of zero means manual removal only.
    void addTemporaryDevice(
        Appliance* device,
        int lifetimeSeconds
    );

    bool removeDevice(const std::string& id);

    bool hasDevice(const std::string& id) const;

    void turnOn(const std::string& id);
    void turnOff(const std::string& id);

    bool showDeviceStatus(const std::string& id);
    void showAllDevices();

    int getDeviceCount() const;

    void setExpiryCallback(
        std::function<void(const std::string&)> callback
    );

    // Stop and join the worker before the owning SmartHome is destroyed.
    void stopExpiryWorker();
};

#endif