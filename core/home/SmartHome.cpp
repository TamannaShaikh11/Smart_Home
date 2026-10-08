#include "SmartHome.h"

#include <iostream>
#include <stdexcept>
#include <utility>

SmartHome::SmartHome(const std::string& name)
{
    if (name.empty())
    {
        throw std::invalid_argument(
            "Home name cannot be empty."
        );
    }

    homeName = name;

    deviceManager.setExpiryCallback(
        [this](const std::string& deviceId)
        {
            handleExpiredDevice(deviceId);
        }
    );
}

SmartHome::~SmartHome()
{
    // Ensure no worker callback accesses this object during destruction.
    deviceManager.stopExpiryWorker();
    deviceManager.setExpiryCallback({});
}

Room* SmartHome::findRoom(const std::string& roomName)
{
    for (Room& room : rooms)
    {
        if (room.getName() == roomName)
        {
            return &room;
        }
    }

    return nullptr;
}

const Room* SmartHome::findRoom(
    const std::string& roomName
) const
{
    for (const Room& room : rooms)
    {
        if (room.getName() == roomName)
        {
            return &room;
        }
    }

    return nullptr;
}

bool SmartHome::addRoom(const std::string& roomName)
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
    return true;
}

void SmartHome::addDevice(Appliance* device)
{
    if (device == nullptr)
    {
        throw std::invalid_argument(
            "Cannot add a null device."
        );
    }

    std::lock_guard<std::mutex> lock(homeMutex);

    if (deviceManager.hasDevice(device->getId()))
    {
        throw std::invalid_argument(
            "A device with this ID already exists."
        );
    }

    const std::string roomName = device->getRoom();

    if (roomName.empty())
    {
        throw std::invalid_argument(
            "Device must have a room name."
        );
    }

    Room* room = findRoom(roomName);

    if (room == nullptr)
    {
        rooms.emplace_back(roomName);
        room = &rooms.back();
    }

    // Ownership transfers only after successful registration.
    deviceManager.addDevice(device);
    room->addDeviceId(device->getId());
}

void SmartHome::addTemporaryDevice(
    Appliance* device,
    int lifetimeSeconds
)
{
    if (device == nullptr)
    {
        throw std::invalid_argument(
            "Cannot add a null device."
        );
    }

    if (lifetimeSeconds < 0)
    {
        throw std::invalid_argument(
            "Expiry duration cannot be negative."
        );
    }

    std::lock_guard<std::mutex> lock(homeMutex);

    if (deviceManager.hasDevice(device->getId()))
    {
        throw std::invalid_argument(
            "A device with this ID already exists."
        );
    }

    const std::string roomName = device->getRoom();

    if (roomName.empty())
    {
        throw std::invalid_argument(
            "Device must have a room name."
        );
    }

    Room* room = findRoom(roomName);

    if (room == nullptr)
    {
        rooms.emplace_back(roomName);
        room = &rooms.back();
    }

    deviceManager.addTemporaryDevice(device, lifetimeSeconds);
    room->addDeviceId(device->getId());
}

bool SmartHome::removeDevice(const std::string& deviceId)
{
    std::lock_guard<std::mutex> lock(homeMutex);

    if (!deviceManager.removeDevice(deviceId))
    {
        return false;
    }

    for (Room& room : rooms)
    {
        room.removeDeviceId(deviceId);
    }

    return true;
}

void SmartHome::handleExpiredDevice(
    const std::string& deviceId
)
{
    std::lock_guard<std::mutex> lock(homeMutex);

    for (Room& room : rooms)
    {
        room.removeDeviceId(deviceId);
    }

    std::cout << "\n[TIMER] Temporary device expired: "
              << deviceId << '\n';
}

void SmartHome::turnOn(const std::string& deviceId)
{
    deviceManager.turnOn(deviceId);
}

void SmartHome::turnOff(const std::string& deviceId)
{
    deviceManager.turnOff(deviceId);
}

bool SmartHome::hasDevice(const std::string& deviceId) const
{
    return deviceManager.hasDevice(deviceId);
}

void SmartHome::showRooms()
{
    std::lock_guard<std::mutex> lock(homeMutex);

    std::cout << "\n====================================\n";
    std::cout << homeName << '\n';
    std::cout << "====================================\n";

    for (const Room& room : rooms)
    {
        std::cout << "\nRoom: " << room.getName() << '\n';
        std::cout << "Devices: " << room.getDeviceCount() << '\n';

        for (const std::string& id : room.getDeviceIds())
        {
            if (deviceManager.hasDevice(id))
            {
                std::cout << "  - " << id << '\n';
            }
        }
    }
}

void SmartHome::showRoomDevices(const std::string& roomName)
{
    std::lock_guard<std::mutex> lock(homeMutex);

    Room* room = findRoom(roomName);

    if (room == nullptr)
    {
        std::cout << "Room not found: " << roomName << '\n';
        return;
    }

    std::cout << "\n--- " << room->getName() << " ---\n";

    bool found = false;

    for (const std::string& id : room->getDeviceIds())
    {
        if (deviceManager.showDeviceStatus(id))
        {
            found = true;
            std::cout << "--------------------------\n";
        }
    }

    if (!found)
    {
        std::cout << "No devices in this room.\n";
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