#include "DeviceManager.h"
#include "../storage/Database.h"
#include "../devices/AC.h"
#include "../devices/Fan.h"
#include "../devices/Heater.h"
#include "../devices/Light.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <utility>

DeviceManager::DeviceManager()
    : stopping(false)
{
    expiryThread = std::thread(
        &DeviceManager::expiryWorker,
        this
    );
}

DeviceManager::~DeviceManager()
{
    stopExpiryWorker();

    std::lock_guard<std::mutex> lock(devicesMutex);

    for (Appliance* device : devices)
    {
        delete device;
    }

    devices.clear();
}

void DeviceManager::stopExpiryWorker()
{
    {
        std::lock_guard<std::mutex> lock(devicesMutex);
        stopping = true;
    }

    expiryCondition.notify_all();

    if (expiryThread.joinable())
    {
        expiryThread.join();
    }
}

void DeviceManager::registerDevice(
    Appliance* device,
    DeviceLifetime lifetime,
    std::time_t expiry
)
{
    if (device == nullptr)
    {
        throw std::invalid_argument(
            "Cannot register a null device."
        );
    }

    {
        std::lock_guard<std::mutex> lock(devicesMutex);

        if (stopping)
        {
            throw std::runtime_error(
                "Device manager is shutting down."
            );
        }

        for (Appliance* existing : devices)
        {
            if (existing->getId() == device->getId())
            {
                throw std::invalid_argument(
                    "A device with this ID already exists."
                );
            }
        }

        device->setLifetime(lifetime, expiry);
        devices.push_back(device);
    }

    expiryCondition.notify_all();
}

void DeviceManager::addDevice(Appliance* device)
{
    registerDevice(
        device,
        DeviceLifetime::PERMANENT,
        0
    );
}

void DeviceManager::addTemporaryDevice(
    Appliance* device,
    int lifetimeSeconds
)
{
    if (device == nullptr)
    {
        throw std::invalid_argument(
            "Cannot register a null device."
        );
    }

    if (lifetimeSeconds < 0)
    {
        throw std::invalid_argument(
            "Device lifetime cannot be negative."
        );
    }

    std::time_t expiry = 0;

    if (lifetimeSeconds > 0)
    {
        expiry = std::time(nullptr) + lifetimeSeconds;
    }

    registerDevice(
        device,
        DeviceLifetime::TEMPORARY,
        expiry
    );
}

bool DeviceManager::removeDevice(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lock(devicesMutex);

        auto it = std::find_if(
            devices.begin(),
            devices.end(),
            [&id](Appliance* device)
            {
                return device->getId() == id;
            }
        );

        if (it == devices.end())
        {
            return false;
        }

        delete *it;
        devices.erase(it);
    }

    expiryCondition.notify_all();
    return true;
}

bool DeviceManager::hasDevice(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    for (Appliance* device : devices)
    {
        if (device->getId() == id)
        {
            return true;
        }
    }

    return false;
}

void DeviceManager::turnOn(const std::string& id)
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    for (Appliance* device : devices)
    {
        if (device->getId() == id)
        {
            if (device->getStatus() == DeviceStatus::ON)
            {
                std::cout << device->getName()
                          << " is already ON.\n";
                return;
            }

            device->turnOn();

            std::cout << device->getName()
                      << " turned ON successfully.\n";
            return;
        }
    }

    throw std::invalid_argument("Device not found: " + id);
}

void DeviceManager::turnOff(const std::string& id)
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    for (Appliance* device : devices)
    {
        if (device->getId() == id)
        {
            if (device->getStatus() == DeviceStatus::OFF)
            {
                std::cout << device->getName()
                          << " is already OFF.\n";
                return;
            }

            device->turnOff();

            std::cout << device->getName()
                      << " turned OFF successfully.\n";
            return;
        }
    }

    throw std::invalid_argument("Device not found: " + id);
}

void DeviceManager::setDeviceSetting(const std::string& id, int setting)
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    for (Appliance* device : devices)
    {
        if (device->getId() != id)
            continue;

        if (auto* ac = dynamic_cast<AC*>(device))
        {
            ac->setTemperature(setting);
            return;
        }

        if (auto* heater = dynamic_cast<Heater*>(device))
        {
            heater->setTemperature(setting);
            return;
        }

        if (auto* fan = dynamic_cast<Fan*>(device))
        {
            fan->setSpeed(setting);
            return;
        }

        throw std::invalid_argument(
            "Device does not support adjustable settings: " + id);
    }

    throw std::invalid_argument("Device not found: " + id);
}

void DeviceManager::setLightBrightness(
    const std::string& id, int brightness)
{
    std::lock_guard<std::mutex> lock(devicesMutex);
    for (Appliance* device : devices)
    {
        if (device->getId() != id)
            continue;
        auto* light = dynamic_cast<Light*>(device);
        if (light == nullptr)
            throw std::invalid_argument("Device is not a configurable light: " + id);
        light->setBrightness(brightness);
        return;
    }
    throw std::invalid_argument("Device not found: " + id);
}

void DeviceManager::setLightColor(
    const std::string& id, const std::string& color)
{
    std::lock_guard<std::mutex> lock(devicesMutex);
    for (Appliance* device : devices)
    {
        if (device->getId() != id)
            continue;
        auto* light = dynamic_cast<Light*>(device);
        if (light == nullptr)
            throw std::invalid_argument("Device is not a configurable light: " + id);
        light->setColor(color);
        return;
    }
    throw std::invalid_argument("Device not found: " + id);
}

void DeviceManager::setLightColorTemperature(
    const std::string& id, int kelvin)
{
    std::lock_guard<std::mutex> lock(devicesMutex);
    for (Appliance* device : devices)
    {
        if (device->getId() != id)
            continue;
        auto* light = dynamic_cast<Light*>(device);
        if (light == nullptr)
            throw std::invalid_argument("Device is not a configurable light: " + id);
        light->setColorTemperature(kelvin);
        return;
    }
    throw std::invalid_argument("Device not found: " + id);
}

void DeviceManager::updateDeviceDetails(
    const std::string& id,
    const std::string& name,
    int powerWatts)
{
    std::lock_guard<std::mutex> lock(devicesMutex);
    for (Appliance* device : devices)
    {
        if (device->getId() == id)
        {
            device->updateDetails(name, powerWatts);
            return;
        }
    }
    throw std::invalid_argument("Device not found: " + id);
}


bool DeviceManager::showDeviceStatus(const std::string& id)
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    for (Appliance* device : devices)
    {
        if (device->getId() == id)
        {
            device->showStatus();
            return true;
        }
    }

    return false;
}

void DeviceManager::showAllDevices()
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    std::cout << "\n================================\n";
    std::cout << "       SMART HOME DEVICES\n";
    std::cout << "================================\n";

    if (devices.empty())
    {
        std::cout << "No devices registered.\n";
        return;
    }

    for (Appliance* device : devices)
    {
        std::cout << '\n';
        device->showStatus();
        std::cout << "-------------------------------\n";
    }
}

int DeviceManager::getDeviceCount() const
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    return static_cast<int>(devices.size());
}

void DeviceManager::setExpiryCallback(
    std::function<void(const std::string&)> callback
)
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    expiryCallback = std::move(callback);
}

void DeviceManager::removeExpiredDevices()
{
    std::vector<std::string> expiredIds;
    std::function<void(const std::string&)> callback;

    {
        std::lock_guard<std::mutex> lock(devicesMutex);

        const std::time_t now = std::time(nullptr);

        auto it = devices.begin();

        while (it != devices.end())
        {
            Appliance* device = *it;

            if (device->getLifetime() == DeviceLifetime::TEMPORARY &&
                device->getExpiryTime() != 0 &&
                now >= device->getExpiryTime())
            {
                expiredIds.push_back(device->getId());

                delete device;
                it = devices.erase(it);
            }
            else
            {
                ++it;
            }
        }

        callback = expiryCallback;
    }

    // Notify SmartHome only after releasing devicesMutex.
    // This avoids locking the home while holding the device lock.
    if (callback)
    {
        for (const std::string& id : expiredIds)
        {
            callback(id);
        }
    }
}

bool DeviceManager::getDeviceRecord(
    const std::string& id,
    DeviceRecord& record
) const
{
    std::lock_guard<std::mutex> lock(devicesMutex);

    for (const Appliance* device : devices)
    {
        if (device->getId() == id)
        {
            record.id = device->getId();
            record.name = device->getName();
            record.type = device->getType();
            record.roomName = device->getRoom();

            record.status =
                device->getStatus() == DeviceStatus::ON ? 1 : 0;

            record.powerWatts = device->getPowerWatts();
            record.runtimeMinutes = device->getRuntimeMinutes();
            record.setting = device->getState().setting;

            record.lifetime =
                static_cast<int>(device->getLifetime());

            record.expiryTime =
                static_cast<long long>(device->getExpiryTime());
            if (const auto* light = dynamic_cast<const Light*>(device))
            {
                record.lightType = light->getLightType();
                record.brightness = light->getBrightness();
                record.color = light->getColor();
                record.colorTemperature = light->getColorTemperature();
                record.supportsBrightness = light->hasBrightnessControl();
                record.supportsColor = light->hasColorControl();
                record.supportsColorTemperature =
                    light->hasColorTemperatureControl();
            }

            return true;
        }
    }

    return false;
}


std::vector<DeviceRecord> DeviceManager::getAllDeviceRecords() const
{
    std::vector<DeviceRecord> records;

    std::lock_guard<std::mutex> lock(devicesMutex);

    records.reserve(devices.size());

    for (const Appliance* device : devices)
    {
        DeviceRecord record;

        record.id = device->getId();
        record.name = device->getName();
        record.type = device->getType();
        record.roomName = device->getRoom();

        record.status =
            device->getStatus() == DeviceStatus::ON ? 1 : 0;

        record.powerWatts = device->getPowerWatts();
        record.runtimeMinutes = device->getRuntimeMinutes();
        record.setting = device->getState().setting;

        record.lifetime =
            static_cast<int>(device->getLifetime());

        record.expiryTime =
            static_cast<long long>(device->getExpiryTime());
        if (const auto* light = dynamic_cast<const Light*>(device))
        {
            record.lightType = light->getLightType();
            record.brightness = light->getBrightness();
            record.color = light->getColor();
            record.colorTemperature = light->getColorTemperature();
            record.supportsBrightness = light->hasBrightnessControl();
            record.supportsColor = light->hasColorControl();
            record.supportsColorTemperature =
                light->hasColorTemperatureControl();
        }

        records.push_back(record);
    }

    return records;
}

void DeviceManager::expiryWorker()
{
    std::unique_lock<std::mutex> lock(devicesMutex);

    while (!stopping)
    {
        std::time_t nearestExpiry = 0;

        for (Appliance* device : devices)
        {
            if (device->getLifetime() != DeviceLifetime::TEMPORARY)
            {
                continue;
            }

            const std::time_t expiry = device->getExpiryTime();

            // Expiry time zero means manual removal only.
            if (expiry == 0)
            {
                continue;
            }

            if (nearestExpiry == 0 || expiry < nearestExpiry)
            {
                nearestExpiry = expiry;
            }
        }

        if (nearestExpiry == 0)
        {
            expiryCondition.wait(
                lock,
                [this]()
                {
                    if (stopping)
                    {
                        return true;
                    }

                    for (Appliance* device : devices)
                    {
                        if (device->getLifetime() ==
                                DeviceLifetime::TEMPORARY &&
                            device->getExpiryTime() != 0)
                        {
                            return true;
                        }
                    }

                    return false;
                }
            );

            continue;
        }

        const auto deadline =
            std::chrono::system_clock::from_time_t(nearestExpiry);

        // Any notification causes the worker to recalculate deadlines.
        expiryCondition.wait_until(lock, deadline);

        if (stopping)
        {
            break;
        }

        lock.unlock();
        removeExpiredDevices();
        lock.lock();
    }
}