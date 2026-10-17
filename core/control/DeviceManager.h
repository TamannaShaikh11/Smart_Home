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
#include "../storage/Database.h"

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

    void addDevice(Appliance* device);

    void addTemporaryDevice(
        Appliance* device,
        int lifetimeSeconds
    );

    bool removeDevice(const std::string& id);
    bool hasDevice(const std::string& id) const;

    void turnOn(const std::string& id);
    void turnOff(const std::string& id);
    void setDeviceSetting(const std::string& id, int setting);
    void setLightBrightness(const std::string& id, int brightness);
    void setLightColor(const std::string& id, const std::string& color);
    void setLightColorTemperature(const std::string& id, int kelvin);
    void updateDeviceDetails(
        const std::string& id,
        const std::string& name,
        int powerWatts);

    bool showDeviceStatus(const std::string& id);
    void showAllDevices();

    int getDeviceCount() const;
    // Safely copy the current records for all devices.
    std::vector<DeviceRecord> getAllDeviceRecords() const;
    
    // Safely copy a device's current state for database persistence.
    bool getDeviceRecord(
        const std::string& id,
        DeviceRecord& record
    ) const;

    void setExpiryCallback(
        std::function<void(const std::string&)> callback
    );

    void stopExpiryWorker();
};

#endif
