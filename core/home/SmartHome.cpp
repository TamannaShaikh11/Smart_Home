
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
#include "../devices/GenericAppliance.h"

namespace
{
    DeviceRecord makeDeviceRecord(const Appliance &device)
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
        if (const auto *light = dynamic_cast<const Light *>(&device))
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

        return record;
    }

    std::unique_ptr<Appliance> createRestoredDevice(
        const DeviceRecord &record)
    {
        std::unique_ptr<Appliance> device;

        if (record.type == "Light")
        {
            auto light = std::make_unique<Light>(
                record.id,
                record.name,
                record.roomName,
                record.powerWatts,
                record.lightType);
            if (light->hasBrightnessControl())
                light->setBrightness(record.brightness);
            if (light->hasColorControl())
                light->setColor(record.color);
            if (light->hasColorTemperatureControl())
                light->setColorTemperature(record.colorTemperature);
            device = std::move(light);
        }
        else if (record.type == "Fan")
        {
            auto fan = std::make_unique<Fan>(
                record.id,
                record.name,
                record.roomName,
                record.powerWatts);

            fan->setSpeed(record.setting);
            device = std::move(fan);
        }
        else if (record.type == "AC")
        {
            auto ac = std::make_unique<AC>(
                record.id,
                record.name,
                record.roomName,
                record.powerWatts);

            ac->setTemperature(record.setting);
            device = std::move(ac);
        }
        else if (record.type == "Heater")
        {
            auto heater = std::make_unique<Heater>(
                record.id,
                record.name,
                record.roomName,
                record.powerWatts);

            heater->setTemperature(record.setting);
            device = std::move(heater);
        }
        else
        {
            // Restore any other saved device type generically.
            device = std::make_unique<GenericAppliance>(
                record.id,
                record.name,
                record.roomName,
                record.powerWatts,
                record.type);
        }

        device->restoreState(
            record.status == 1
                ? DeviceStatus::ON
                : DeviceStatus::OFF,
            record.runtimeMinutes);

        return device;
    }

}

SmartHome::SmartHome(const std::string &name)
    : homeName(name),
      database("smarthome.db"),
      energyTracker(database.getConnection()),
      deviceManager()
{
    if (homeName.empty())
    {
        throw std::invalid_argument(
            "Smart home name cannot be empty.");
    }

    if (!database.initializeSchema())
    {
        throw std::runtime_error(
            "Could not initialize the SmartHome database.");
    }

    // Initialize the persistent energy-session table.
    if (!energyTracker.initializeSchema())
    {
        throw std::runtime_error(
            "Could not initialize energy tracking.");
    }

    // Restore saved rooms first.
    for (const std::string &roomName : database.loadRooms())
    {
        rooms.emplace_back(roomName);
    }

    deviceManager.setExpiryCallback(
        [this](const std::string &deviceId)
        {
            handleExpiredDevice(deviceId);
        });

    const std::time_t now = std::time(nullptr);

    // Restore saved devices from SQLite.
    for (const DeviceRecord &record : database.loadDevices())
    {
        // Remove temporary devices that expired while the app was closed.
        if (record.lifetime ==
                static_cast<int>(DeviceLifetime::TEMPORARY) &&
            record.expiryTime != 0 &&
            record.expiryTime <= static_cast<long long>(now))
        {
            // Close any active energy session before deleting the device.
            if (!energyTracker.endSession(record.id))
            {
                std::cerr
                    << "Warning: Could not close energy session for "
                    << record.id << '\n';
            }

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
            Room *room = findRoom(record.roomName);

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

            // DeviceManager now owns the device.
            device.release();

            // Continue an energy session for a device saved as ON.
            if (record.status == 1)
            {
                DeviceRecord restoredRecord;

                if (deviceManager.getDeviceRecord(
                        record.id, restoredRecord))
                {
                    if (!energyTracker.startSession(restoredRecord))
                    {
                        std::cerr
                            << "Warning: Could not start energy session "
                            << "for restored device "
                            << record.id << '\n';
                    }
                }
            }
        }
        catch (const std::exception &error)
        {   
            if (Room *existingRoom = findRoom(record.roomName))
            {
                existingRoom->removeDevice(record.id);
            }

            std::cerr << "Could not restore device "
                      << record.id << ": "
                      << error.what() << '\n';
        }
    }

    std::cout << "SmartHome created: "
              << homeName << '\n';

    std::cout << "Saved devices restored: "
              << deviceManager.getDeviceCount() << '\n';
}

SmartHome::~SmartHome()
{
    deviceManager.setExpiryCallback(nullptr);
    deviceManager.stopExpiryWorker();
}

Room *SmartHome::findRoom(const std::string &roomName)
{
    for (Room &room : rooms)
    {
        if (room.getName() == roomName)
        {
            return &room;
        }
    }

    return nullptr;
}

const Room *SmartHome::findRoom(
    const std::string &roomName) const
{
    for (const Room &room : rooms)
    {
        if (room.getName() == roomName)
        {
            return &room;
        }
    }

    return nullptr;
}

bool SmartHome::addRoom(const std::string &roomName)
{
    if (roomName.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(homeMutex);

    if (findRoom(roomName) != nullptr)
    {
        return false;
    }

    rooms.emplace_back(roomName);

    if (!database.saveRoom(roomName))
    {
        rooms.pop_back();
        return false;
    }

    std::cout << "Room added: " << roomName << '\n';
    return true;
}

void SmartHome::addDevice(Appliance *device)
{
    if (device == nullptr)
    {
        throw std::invalid_argument("Device cannot be null.");
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

    bool registered = false;

    try
    {
        deviceManager.addDevice(device);
        registered = true;
        DeviceRecord record = makeDeviceRecord(*device);

        if (!database.saveDevice(record))
        {
            deviceManager.removeDevice(deviceId);

            std::lock_guard<std::mutex> lock(homeMutex);

            if (Room *room = findRoom(roomName))
            {
                room->removeDevice(deviceId);
            }

            throw std::runtime_error(
                "Could not save device to the database.");
        }
    }
    catch (...)
    {
        if (registered)
        {
            // Remove it only if DeviceManager still owns it.
            // removeDevice() deletes the registered device.
            if (deviceManager.hasDevice(deviceId))
            {
                deviceManager.removeDevice(deviceId);
            }
        }
        else
        {
            // Registration failed before DeviceManager accepted ownership.
            delete device;
        }
        std::lock_guard<std::mutex> lock(homeMutex);

        if (Room *room = findRoom(roomName))
        {
            room->removeDevice(deviceId);
        }

        if (roomWasCreated)
        {
            auto it = std::find_if(
                rooms.begin(), rooms.end(),
                [&roomName](const Room &room)
                {
                    return room.getName() == roomName;
                });

            if (it != rooms.end() &&
                it->getDeviceCount() == 0)
            {
                rooms.erase(it);
            }
        }

        throw;
    }

    std::cout << "Permanent device added: "
              << deviceId << '\n';
}

void SmartHome::addTemporaryDevice(
    Appliance *device, int lifetimeSeconds)
{
    if (device == nullptr)
    {
        throw std::invalid_argument("Device cannot be null.");
    }

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

    bool registered = false;

    try
    {
        deviceManager.addTemporaryDevice(
            device, lifetimeSeconds);
        registered = true;
        DeviceRecord record = makeDeviceRecord(*device);

        if (!database.saveDevice(record))
        {
            deviceManager.removeDevice(deviceId);

            std::lock_guard<std::mutex> lock(homeMutex);

            if (Room *room = findRoom(roomName))
            {
                room->removeDevice(deviceId);
            }

            throw std::runtime_error(
                "Could not save temporary device to the database.");
        }
    }
    catch (...)
    {
        if (registered)
        {
            // Remove it only if DeviceManager still owns it.
            // removeDevice() deletes the registered device.
            if (deviceManager.hasDevice(deviceId))
            {
                deviceManager.removeDevice(deviceId);
            }
        }
        else
        {
            // Registration failed before DeviceManager accepted ownership.
            delete device;
        }

        std::lock_guard<std::mutex> lock(homeMutex);

        if (Room *room = findRoom(roomName))
        {
            room->removeDevice(deviceId);
        }

        if (roomWasCreated)
        {
            auto it = std::find_if(
                rooms.begin(), rooms.end(),
                [&roomName](const Room &room)
                {
                    return room.getName() == roomName;
                });

            if (it != rooms.end() &&
                it->getDeviceCount() == 0)
            {
                rooms.erase(it);
            }
        }

        throw;
    }

    std::cout << "Temporary device added: "
              << deviceId << '\n';
}

bool SmartHome::removeDevice(const std::string &deviceId)
{
    DeviceRecord previous;
    if (!deviceManager.getDeviceRecord(deviceId, previous))
        return false;

    if (previous.status == 1 && !energyTracker.endSession(deviceId))
    {
        std::cerr << "Could not close energy session for "
                  << deviceId << "; device was not removed.\n";
        return false;
    }

    if (!database.deleteDevice(deviceId))
    {
        if (previous.status == 1 && !energyTracker.startSession(previous))
            std::cerr << "Could not resume energy session for "
                      << deviceId << " after failed deletion.\n";
        return false;
    }

    if (!deviceManager.removeDevice(deviceId))
    {
        if (!database.saveDevice(previous))
            std::cerr << "Could not restore deleted database record for "
                      << deviceId << ".\n";
        if (previous.status == 1 && !energyTracker.startSession(previous))
            std::cerr << "Could not resume energy session for "
                      << deviceId << " after failed removal.\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        for (Room &room : rooms)
        {
            room.removeDevice(deviceId);
        }
    }

    std::cout << "Device removed: "
              << deviceId << '\n';

    return true;
}

void SmartHome::turnOn(const std::string &deviceId)
{
    DeviceRecord previous;
    if (!deviceManager.getDeviceRecord(deviceId, previous))
        throw std::invalid_argument("Device not found: " + deviceId);

    deviceManager.turnOn(deviceId);

    DeviceRecord record;

    if (!deviceManager.getDeviceRecord(deviceId, record))
        throw std::invalid_argument("Device no longer exists: " + deviceId);

    // Start a new energy session if this device is ON.
    if (record.status == 1)
    {
        if (!energyTracker.startSession(record))
        {
            if (previous.status != 1)
                deviceManager.turnOff(deviceId);
            throw std::runtime_error(
                "Could not start energy session for " + deviceId);
        }
    }

    if (!database.updateDevice(record))
    {
        if (previous.status != 1)
        {
            deviceManager.turnOff(deviceId);
            energyTracker.endSession(deviceId);
        }
        throw std::runtime_error(
            "Could not persist device ON state for " + deviceId);
    }
}

void SmartHome::turnOff(const std::string &deviceId)
{
    DeviceRecord previous;
    if (!deviceManager.getDeviceRecord(deviceId, previous))
        throw std::invalid_argument("Device not found: " + deviceId);

    deviceManager.turnOff(deviceId);

    // Close the active energy session before saving the OFF state.
    if (!energyTracker.endSession(deviceId))
    {
        deviceManager.turnOn(deviceId);
        throw std::runtime_error(
            "Could not close energy session for " + deviceId);
    }

    DeviceRecord record;

    if (!deviceManager.getDeviceRecord(deviceId, record))
        throw std::invalid_argument("Device no longer exists: " + deviceId);

    if (!database.updateDevice(record))
    {
        if (previous.status == 1)
        {
            deviceManager.turnOn(deviceId);
            if (!energyTracker.startSession(previous))
                std::cerr << "Could not resume energy session for "
                          << deviceId << " after failed OFF persistence.\n";
        }
        throw std::runtime_error(
            "Could not persist device OFF state for " + deviceId);
    }
}

bool SmartHome::setDeviceSetting(
    const std::string &deviceId,
    int setting)
{
    DeviceRecord previous;

    if (!deviceManager.getDeviceRecord(deviceId, previous))
        throw std::invalid_argument("Device not found: " + deviceId);

    deviceManager.setDeviceSetting(deviceId, setting);

    DeviceRecord updated;
    if (!deviceManager.getDeviceRecord(deviceId, updated))
        throw std::invalid_argument("Device no longer exists: " + deviceId);

    if (database.updateDevice(updated))
        return true;

    try
    {
        deviceManager.setDeviceSetting(deviceId, previous.setting);
    }
    catch (const std::exception &error)
    {
        std::cerr << "Could not roll back setting for "
                  << deviceId << ": " << error.what() << '\n';
    }

    return false;
}

bool SmartHome::setLightBrightness(
    const std::string &deviceId, int brightness)
{
    DeviceRecord previous;
    if (!deviceManager.getDeviceRecord(deviceId, previous))
        throw std::invalid_argument("Device not found: " + deviceId);
    deviceManager.setLightBrightness(deviceId, brightness);
    DeviceRecord updated;
    if (!deviceManager.getDeviceRecord(deviceId, updated))
        throw std::invalid_argument("Device no longer exists: " + deviceId);
    if (database.updateDevice(updated))
        return true;
    try
    {
        deviceManager.setLightBrightness(deviceId, previous.brightness);
    }
    catch (const std::exception &error)
    {
        std::cerr << "Could not roll back light brightness for "
                  << deviceId << ": " << error.what() << '\n';
    }
    return false;
}

bool SmartHome::setLightColor(
    const std::string &deviceId, const std::string &color)
{
    DeviceRecord previous;
    if (!deviceManager.getDeviceRecord(deviceId, previous))
        throw std::invalid_argument("Device not found: " + deviceId);
    deviceManager.setLightColor(deviceId, color);
    DeviceRecord updated;
    if (!deviceManager.getDeviceRecord(deviceId, updated))
        throw std::invalid_argument("Device no longer exists: " + deviceId);
    if (database.updateDevice(updated))
        return true;
    try
    {
        deviceManager.setLightColor(deviceId, previous.color);
    }
    catch (const std::exception &error)
    {
        std::cerr << "Could not roll back light color for "
                  << deviceId << ": " << error.what() << '\n';
    }
    return false;
}

bool SmartHome::setLightColorTemperature(
    const std::string &deviceId, int kelvin)
{
    DeviceRecord previous;
    if (!deviceManager.getDeviceRecord(deviceId, previous))
        throw std::invalid_argument("Device not found: " + deviceId);
    deviceManager.setLightColorTemperature(deviceId, kelvin);
    DeviceRecord updated;
    if (!deviceManager.getDeviceRecord(deviceId, updated))
        throw std::invalid_argument("Device no longer exists: " + deviceId);
    if (database.updateDevice(updated))
        return true;
    try
    {
        deviceManager.setLightColorTemperature(
            deviceId, previous.colorTemperature);
    }
    catch (const std::exception &error)
    {
        std::cerr << "Could not roll back light color temperature for "
                  << deviceId << ": " << error.what() << '\n';
    }
    return false;
}

bool SmartHome::updateDeviceDetails(
    const std::string &deviceId,
    const std::string &name,
    int powerWatts)
{
    DeviceRecord previous;
    if (!deviceManager.getDeviceRecord(deviceId, previous))
        throw std::invalid_argument("Device not found: " + deviceId);
    if (name.empty() || name.size() > 100 ||
        name.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::invalid_argument("Device name must contain 1 to 100 characters.");
    if (powerWatts < 1 || powerWatts > 100000)
        throw std::invalid_argument("Rated power must be between 1 and 100000 watts.");
    if (name == previous.name && powerWatts == previous.powerWatts)
        return true;

    const bool sessionActive = previous.status == 1;
    if (sessionActive && !energyTracker.endSession(deviceId))
        return false;

    deviceManager.updateDeviceDetails(deviceId, name, powerWatts);
    DeviceRecord updated;
    if (!deviceManager.getDeviceRecord(deviceId, updated))
        throw std::invalid_argument("Device no longer exists: " + deviceId);

    if (!database.updateDevice(updated))
    {
        try
        {
            deviceManager.updateDeviceDetails(
                deviceId, previous.name, previous.powerWatts);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Could not roll back device details for "
                      << deviceId << ": " << error.what() << '\n';
        }
        if (sessionActive && !energyTracker.startSession(previous))
            std::cerr << "Could not resume energy session for "
                      << deviceId << " after update rollback.\n";
        return false;
    }

    if (sessionActive && !energyTracker.startSession(updated))
    {
        try
        {
            deviceManager.updateDeviceDetails(
                deviceId, previous.name, previous.powerWatts);
            if (!database.updateDevice(previous))
                std::cerr << "Could not restore persisted device details for "
                          << deviceId << ".\n";
        }
        catch (const std::exception &error)
        {
            std::cerr << "Could not restore device details for "
                      << deviceId << ": " << error.what() << '\n';
        }
        if (!energyTracker.startSession(previous))
            std::cerr << "Could not resume the previous energy session for "
                      << deviceId << ".\n";
        return false;
    }
    return true;
}

void SmartHome::showRooms()
{
    std::lock_guard<std::mutex> lock(homeMutex);

    std::cout << "\nRooms in " << homeName << ":\n";

    for (const Room &room : rooms)
    {
        std::cout << "- " << room.getName()
                  << " (" << room.getDeviceCount()
                  << " devices)\n";
    }
}

void SmartHome::showRoomDevices(const std::string &roomName)
{
    std::lock_guard<std::mutex> lock(homeMutex);

    const Room *room = findRoom(roomName);

    if (room == nullptr)
    {
        std::cout << "Room not found: "
                  << roomName << '\n';
        return;
    }

    std::cout << "\nDevices in " << roomName << ":\n";

    for (const std::string &id : room->getDeviceIds())
    {
        if (!deviceManager.showDeviceStatus(id))
        {
            std::cout << "Device unavailable: "
                      << id << '\n';
        }
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

std::vector<EnergySessionRecord> SmartHome::getEnergySessions() const
{
    return energyTracker.loadSessions();
}

void SmartHome::handleExpiredDevice(
    const std::string &deviceId)
{
    // Close the session before deleting an expired temporary device.
    if (!energyTracker.endSession(deviceId))
    {
        std::cerr
            << "Warning: Could not close energy session for "
            << deviceId << '\n';
    }

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        for (Room &room : rooms)
        {
            room.removeDevice(deviceId);
        }
    }

    database.deleteDevice(deviceId);

    std::cout << "Temporary device expired and removed: "
              << deviceId << '\n';
}

std::vector<std::string> SmartHome::getRoomNames() const
{
    std::lock_guard<std::mutex> lock(homeMutex);

    std::vector<std::string> names;
    names.reserve(rooms.size());

    for (const Room &room : rooms)
    {
        names.push_back(room.getName());
    }

    return names;
}

std::vector<DeviceRecord> SmartHome::getAllDeviceRecords() const
{
    return deviceManager.getAllDeviceRecords();
}

bool SmartHome::hasDevice(const std::string &deviceId) const
{
    return deviceManager.hasDevice(deviceId);
}

bool SmartHome::roomExists(const std::string &roomName) const
{
    std::lock_guard<std::mutex> lock(homeMutex);
    return findRoom(roomName) != nullptr;
}

bool SmartHome::renameRoom(
    const std::string &oldName,
    const std::string &newName)
{
    if (oldName.empty() || newName.empty())
        return false;

    std::lock_guard<std::mutex> lock(homeMutex);

    Room *oldRoom = findRoom(oldName);

    if (!oldRoom)
        return false;

    if (oldName == newName)
        return true;

    if (findRoom(newName))
        return false;

    if (!database.renameRoom(oldName, newName))
        return false;

    if (!oldRoom->rename(newName))
        return false;

    return true;
}

bool SmartHome::deleteRoom(const std::string &roomName)
{
    if (roomName.empty())
        return false;

    std::lock_guard<std::mutex> lock(homeMutex);

    Room *room = findRoom(roomName);

    if (!room || room->getDeviceCount() != 0)
        return false;

    if (!database.deleteRoom(roomName))
        return false;

    rooms.erase(
        std::remove_if(
            rooms.begin(),
            rooms.end(),
            [&roomName](const Room &item)
            {
                return item.getName() == roomName;
            }),
        rooms.end());

    return true;
}

bool SmartHome::createUserProfile(const UserProfile& profile)
{
    if (profile.id.empty() || profile.displayName.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(homeMutex);

    return database.createProfile(profile);
}


std::vector<UserProfile> SmartHome::getUserProfiles() const
{
    std::lock_guard<std::mutex> lock(homeMutex);

    return database.loadProfiles();
}


bool SmartHome::saveUserPreferences(
    const UserPreferenceRecord& preferences)
{
    if (preferences.userId.empty() ||
        preferences.roomName.empty() ||
        (preferences.acTemperature != -1 &&
         (preferences.acTemperature < 16 || preferences.acTemperature > 30)) ||
        (preferences.heaterTemperature != -1 &&
         (preferences.heaterTemperature < 20 || preferences.heaterTemperature > 45)) ||
        (preferences.fanSpeed != -1 &&
         (preferences.fanSpeed < 1 || preferences.fanSpeed > 5)) ||
        preferences.lightOn < -1 ||
        preferences.lightOn > 1)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(homeMutex);

    if (findRoom(preferences.roomName) == nullptr ||
        !database.profileExists(preferences.userId))
    {
        return false;
    }

    return database.savePreferences(preferences);
}


bool SmartHome::getUserPreferences(
    const std::string& userId,
    const std::string& roomName,
    UserPreferenceRecord& preferences) const
{
    if (userId.empty() || roomName.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(homeMutex);

    if (findRoom(roomName) == nullptr ||
        !database.profileExists(userId))
    {
        return false;
    }

    return database.loadPreferences(
        userId,
        roomName,
        preferences
    );
}

bool SmartHome::confirmUserPreferenceAutomation(
    const std::string &userId,
    const std::string &roomName)
{
    if (userId.empty() || roomName.empty())
        return false;

    std::lock_guard<std::mutex> lock(homeMutex);
    if (findRoom(roomName) == nullptr ||
        !database.profileExists(userId))
        return false;
    return database.confirmPreferenceAutomation(userId, roomName);
}

bool SmartHome::getRoomAutomationSettings(
    const std::string &roomName,
    RoomAutomationSettings &settings) const
{
    if (roomName.empty())
        return false;

    std::lock_guard<std::mutex> lock(homeMutex);
    if (findRoom(roomName) == nullptr)
        return false;
    return database.loadRoomAutomationSettings(roomName, settings);
}

bool SmartHome::saveRoomAutomationSettings(
    const RoomAutomationSettings &settings)
{
    if (settings.roomName.empty())
        return false;

    std::lock_guard<std::mutex> lock(homeMutex);
    if (findRoom(settings.roomName) == nullptr)
        return false;
    return database.saveRoomAutomationSettings(settings);
}

bool SmartHome::addAutomationEnrollment(
    const AutomationEnrollmentRecord &enrollment)
{
    if (enrollment.id.empty() ||
        enrollment.profileId.empty() ||
        enrollment.profileName.empty() ||
        enrollment.createdAt <= 0)
        return false;

    std::lock_guard<std::mutex> lock(homeMutex);
    if (!database.profileExists(enrollment.profileId))
        return false;
    return database.addAutomationEnrollment(enrollment);
}

std::vector<AutomationEnrollmentRecord>
SmartHome::getAutomationEnrollments() const
{
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.loadAutomationEnrollments();
}

bool SmartHome::getAutomationEnrollment(
    const std::string &id,
    AutomationEnrollmentRecord &enrollment) const
{
    if (id.empty())
        return false;
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.loadAutomationEnrollment(id, enrollment);
}

bool SmartHome::deleteAutomationEnrollment(const std::string &id)
{
    if (id.empty())
        return false;
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.deleteAutomationEnrollment(id);
}

bool SmartHome::recordAutomationEvent(
    const AutomationEventRecord &event)
{
    if (event.roomName.empty() ||
        event.eventTime <= 0 ||
        event.outcome.empty() ||
        event.settingsChanged.empty())
        return false;

    std::lock_guard<std::mutex> lock(homeMutex);
    if (findRoom(event.roomName) == nullptr ||
        (!event.profileId.empty() &&
         !database.profileExists(event.profileId)))
        return false;
    return database.addAutomationEvent(event);
}

std::vector<AutomationEventRecord> SmartHome::getAutomationEvents(
    int limit) const
{
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.loadAutomationEvents(limit);
}

bool SmartHome::getAutomationEnabled() const
{
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.getAutomationEnabled();
}

bool SmartHome::setAutomationEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.setAutomationEnabled(enabled);
}

bool SmartHome::saveModePreset(const ModePresetRecord &preset)
{
    if ((preset.modeType != "season" && preset.modeType != "mood") ||
        preset.presetId.empty() || preset.overridesJson.empty() ||
        preset.updatedAt <= 0)
        return false;
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.saveModePreset(preset);
}

std::vector<ModePresetRecord> SmartHome::getModePresets(
    const std::string &modeType) const
{
    if (modeType != "season" && modeType != "mood")
        return {};
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.loadModePresets(modeType);
}

bool SmartHome::recordModeHistory(ModeHistoryRecord &history)
{
    if (history.appliedAt <= 0 || history.outcome.empty() ||
        history.detailsJson.empty() || history.snapshotJson.empty())
        return false;
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.addModeHistory(history);
}

std::vector<ModeHistoryRecord> SmartHome::getModeHistory(int limit) const
{
    if (limit < 1 || limit > 500)
        return {};
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.loadModeHistory(limit);
}

bool SmartHome::getLatestRestorableModeHistory(
    ModeHistoryRecord &history) const
{
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.loadLatestRestorableModeHistory(history);
}

bool SmartHome::updateModeHistoryRestore(
    long long id,
    const std::string &restoreDetailsJson,
    long long restoredAt)
{
    if (id <= 0 || restoreDetailsJson.empty() || restoredAt < 0)
        return false;
    std::lock_guard<std::mutex> lock(homeMutex);
    return database.updateModeHistoryRestore(
        id, restoreDetailsJson, restoredAt);
}
