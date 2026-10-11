#include "SmartHome.h"

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <ctime>

#include "../devices/Light.h"
#include "../devices/Fan.h"
#include "../devices/AC.h"
#include "../devices/Heater.h"

namespace
{
    DeviceRecord makeDeviceRecord(const Appliance& device)
    {
        DeviceRecord record;

        record.id = device.getId();
        record.name = device.getName();
        record.type = device.getType();
        record.roomName = device.getRoom();
        record.status =
            device.getStatus() == DeviceStatus::ON ? 1 : 0;
        record.powerWatts = device.getPowerWatts();
        record.runtimeMinutes = device.getRuntimeMinutes();
        record.lifetime = static_cast<int>(device.getLifetime());
        record.expiryTime =
            static_cast<long long>(device.getExpiryTime());

        record.setting = device.getState().setting;

        return record;
    }

    std::unique_ptr<Appliance> createRestoredDevice(
        const DeviceRecord& record)
    {
        std::unique_ptr<Appliance> device;

        if (record.type == "Light")
        {
            device = std::make_unique<Light>(
                record.id, record.name, record.roomName,
                record.powerWatts);
        }
        else if (record.type == "Fan")
        {
            auto fan = std::make_unique<Fan>(
                record.id, record.name, record.roomName,
                record.powerWatts);

            fan->setSpeed(record.setting);
            device = std::move(fan);
        }
        else if (record.type == "AC")
        {
            auto ac = std::make_unique<AC>(
                record.id, record.name, record.roomName,
                record.powerWatts);

            ac->setTemperature(record.setting);
            device = std::move(ac);
        }
        else if (record.type == "Heater")
        {
            auto heater = std::make_unique<Heater>(
                record.id, record.name, record.roomName,
                record.powerWatts);

            heater->setTemperature(record.setting);
            device = std::move(heater);
        }
        else
        {
            std::cerr << "Skipping unsupported saved device type: "
                      << record.type << '\n';
            return nullptr;
        }

        device->restoreState(
            record.status == 1 ? DeviceStatus::ON : DeviceStatus::OFF,
            record.runtimeMinutes);

        return device;
    }
}

SmartHome::SmartHome(const std::string& name)
    : homeName(name),
      database("smarthome.db"),
      deviceManager()
{
    if (homeName.empty())
        throw std::invalid_argument(
            "Smart home name cannot be empty.");

    if (!database.initializeSchema())
        throw std::runtime_error(
            "Could not initialize the SmartHome database.");

    // Restore rooms first, so saved devices have valid rooms.
    for (const std::string& roomName : database.loadRooms())
        rooms.emplace_back(roomName);

    deviceManager.setExpiryCallback(
        [this](const std::string& deviceId)
        {
            handleExpiredDevice(deviceId);
        });

    // Restore devices from SQLite.
    const std::time_t now = std::time(nullptr);

    for (const DeviceRecord& record : database.loadDevices())
    {
        // Remove temporary devices that expired while the app was closed.
        if (record.lifetime ==
                static_cast<int>(DeviceLifetime::TEMPORARY) &&
            record.expiryTime != 0 &&
            record.expiryTime <= static_cast<long long>(now))
        {
            database.deleteDevice(record.id);
            continue;
        }

        std::unique_ptr<Appliance> device =
            createRestoredDevice(record);

        if (!device)
        {
            database.deleteDevice(record.id);
            continue;
        }

        try
        {
            Room* room = findRoom(record.roomName);

            if (room == nullptr)
            {
                rooms.emplace_back(record.roomName);
                room = &rooms.back();
                database.saveRoom(record.roomName);
            }

            room->addDevice(record.id);

            if (record.lifetime ==
                static_cast<int>(DeviceLifetime::TEMPORARY))
            {
                int remainingSeconds = 0;

                if (record.expiryTime > 0)
                {
                    remainingSeconds = static_cast<int>(
                        record.expiryTime -
                        static_cast<long long>(now));
                }

                deviceManager.addTemporaryDevice(
                    device.get(), remainingSeconds);
            }
            else
            {
                deviceManager.addDevice(device.get());
            }

            // DeviceManager now owns the object.
            device.release();
        }
        catch (const std::exception& error)
        {
            if (Room* room = findRoom(record.roomName))
                room->removeDevice(record.id);

            std::cerr << "Could not restore device "
                      << record.id << ": "
                      << error.what() << '\n';
        }
    }

    std::cout << "SmartHome created: " << homeName << '\n';
    std::cout << "Saved devices restored: "
              << deviceManager.getDeviceCount() << '\n';
}

SmartHome::~SmartHome()
{
    deviceManager.setExpiryCallback(nullptr);
    deviceManager.stopExpiryWorker();
}

Room* SmartHome::findRoom(const std::string& roomName)
{
    for (Room& room : rooms)
    {
        if (room.getName() == roomName)
            return &room;
    }

    return nullptr;
}

const Room* SmartHome::findRoom(
    const std::string& roomName) const
{
    for (const Room& room : rooms)
    {
        if (room.getName() == roomName)
            return &room;
    }

    return nullptr;
}

bool SmartHome::addRoom(const std::string& roomName)
{
    if (roomName.empty())
        return false;

    std::lock_guard<std::mutex> lock(homeMutex);

    if (findRoom(roomName) != nullptr)
        return false;

    rooms.emplace_back(roomName);

    if (!database.saveRoom(roomName))
    {
        rooms.pop_back();
        return false;
    }

    std::cout << "Room added: " << roomName << '\n';
    return true;
}

void SmartHome::addDevice(Appliance* device)
{
    if (device == nullptr)
        throw std::invalid_argument("Device cannot be null.");

    const std::string roomName = device->getRoom();
    const std::string deviceId = device->getId();

    if (roomName.empty() || deviceId.empty())
    {
        delete device;
        throw std::invalid_argument(
            "Device ID and room name cannot be empty.");
    }

    bool roomWasCreated = false;

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        if (findRoom(roomName) == nullptr)
        {
            rooms.emplace_back(roomName);
            roomWasCreated = true;

            if (!database.saveRoom(roomName))
            {
                rooms.pop_back();
                delete device;
                throw std::runtime_error(
                    "Could not save the device's room.");
            }
        }

        findRoom(roomName)->addDevice(deviceId);
    }

    try
    {
        deviceManager.addDevice(device);

        DeviceRecord record = makeDeviceRecord(*device);

        if (!database.saveDevice(record))
        {
            deviceManager.removeDevice(deviceId);

            std::lock_guard<std::mutex> lock(homeMutex);
            if (Room* room = findRoom(roomName))
                room->removeDevice(deviceId);

            throw std::runtime_error(
                "Could not save device to the database.");
        }
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lock(homeMutex);

            if (Room* room = findRoom(roomName))
                room->removeDevice(deviceId);

            if (roomWasCreated)
            {
                auto it = std::find_if(
                    rooms.begin(), rooms.end(),
                    [&roomName](const Room& room)
                    {
                        return room.getName() == roomName;
                    });

                if (it != rooms.end() &&
                    it->getDeviceCount() == 0)
                {
                    rooms.erase(it);
                }
            }
        }

        // If registration failed, DeviceManager did not take ownership.
        // If saving failed, the device was already removed by the manager.
        // Do not delete the pointer here: its ownership depends on the
        // point at which the exception occurred.
        throw;
    }

    std::cout << "Permanent device added: "
              << deviceId << '\n';
}

void SmartHome::addTemporaryDevice(
    Appliance* device, int lifetimeSeconds)
{
    if (device == nullptr)
        throw std::invalid_argument("Device cannot be null.");

    if (lifetimeSeconds < 0)
    {
        delete device;
        throw std::invalid_argument(
            "Lifetime cannot be negative.");
    }

    const std::string roomName = device->getRoom();
    const std::string deviceId = device->getId();

    if (roomName.empty() || deviceId.empty())
    {
        delete device;
        throw std::invalid_argument(
            "Device ID and room name cannot be empty.");
    }

    bool roomWasCreated = false;

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        if (findRoom(roomName) == nullptr)
        {
            rooms.emplace_back(roomName);
            roomWasCreated = true;

            if (!database.saveRoom(roomName))
            {
                rooms.pop_back();
                delete device;
                throw std::runtime_error(
                    "Could not save the device's room.");
            }
        }

        findRoom(roomName)->addDevice(deviceId);
    }

    try
    {
        deviceManager.addTemporaryDevice(
            device, lifetimeSeconds);

        DeviceRecord record = makeDeviceRecord(*device);

        if (!database.saveDevice(record))
        {
            deviceManager.removeDevice(deviceId);

            std::lock_guard<std::mutex> lock(homeMutex);
            if (Room* room = findRoom(roomName))
                room->removeDevice(deviceId);

            throw std::runtime_error(
                "Could not save temporary device to the database.");
        }
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lock(homeMutex);

            if (Room* room = findRoom(roomName))
                room->removeDevice(deviceId);

            if (roomWasCreated)
            {
                auto it = std::find_if(
                    rooms.begin(), rooms.end(),
                    [&roomName](const Room& room)
                    {
                        return room.getName() == roomName;
                    });

                if (it != rooms.end() &&
                    it->getDeviceCount() == 0)
                {
                    rooms.erase(it);
                }
            }
        }

        throw;
    }

    std::cout << "Temporary device added: "
              << deviceId << '\n';
}

bool SmartHome::removeDevice(const std::string& deviceId)
{
    if (!deviceManager.removeDevice(deviceId))
        return false;

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        for (Room& room : rooms)
            room.removeDevice(deviceId);
    }

    database.deleteDevice(deviceId);

    std::cout << "Device removed: " << deviceId << '\n';
    return true;
}

void SmartHome::turnOn(const std::string& deviceId)
{
    deviceManager.turnOn(deviceId);

    DeviceRecord record;

    if (deviceManager.getDeviceRecord(deviceId, record))
    {
        if (!database.updateDevice(record))
        {
            std::cerr
                << "Warning: Could not save device ON state to database.\n";
        }
    }
}

void SmartHome::turnOff(const std::string& deviceId)
{
    deviceManager.turnOff(deviceId);

    DeviceRecord record;

    if (deviceManager.getDeviceRecord(deviceId, record))
    {
        if (!database.updateDevice(record))
        {
            std::cerr
                << "Warning: Could not save device OFF state to database.\n";
        }
    }
}

void SmartHome::showRooms()
{
    std::lock_guard<std::mutex> lock(homeMutex);

    std::cout << "\nRooms in " << homeName << ":\n";

    for (const Room& room : rooms)
    {
        std::cout << "- " << room.getName()
                  << " (" << room.getDeviceCount()
                  << " devices)\n";
    }
}

void SmartHome::showRoomDevices(const std::string& roomName)
{
    std::lock_guard<std::mutex> lock(homeMutex);

    const Room* room = findRoom(roomName);

    if (room == nullptr)
    {
        std::cout << "Room not found: " << roomName << '\n';
        return;
    }

    std::cout << "\nDevices in " << roomName << ":\n";

    for (const std::string& id : room->getDeviceIds())
    {
        if (!deviceManager.showDeviceStatus(id))
            std::cout << "Device unavailable: " << id << '\n';
    }
}

void SmartHome::showAllDevices()
{
    deviceManager.showAllDevices();
}

int SmartHome::getRoomCount() const
{
    std::lock_guard<std::mutex> lock(homeMutex);
    return static_cast<int>(rooms.size());
}

int SmartHome::getDeviceCount()
{
    return deviceManager.getDeviceCount();
}

std::string SmartHome::getHomeName() const
{
    return homeName;
}

void SmartHome::handleExpiredDevice(
    const std::string& deviceId)
{
    {
        std::lock_guard<std::mutex> lock(homeMutex);

        for (Room& room : rooms)
            room.removeDevice(deviceId);
    }

    database.deleteDevice(deviceId);

    std::cout << "Temporary device expired and removed: "
              << deviceId << '\n';
}
