#include "SmartHome.h"

#include <iostream>
#include <stdexcept>

SmartHome::SmartHome(const std::string& name)
{
    if (name.empty())
    {
        throw std::invalid_argument(
            "Home name cannot be empty."
        );
    }

    homeName = name;
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
    if (roomName.empty() || findRoom(roomName) != nullptr)
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

    // Reject duplicate IDs before transferring ownership.
    if (deviceManager.findDevice(device->getId()) != nullptr)
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

    // Create the room if it doesn't already exist.
    if (findRoom(roomName) == nullptr)
    {
        addRoom(roomName);
    }

    Room* room = findRoom(roomName);

    // DeviceManager takes ownership after successful registration.
    deviceManager.addDevice(device);

    room->addDeviceId(device->getId());
}

void SmartHome::turnOn(const std::string& deviceId)
{
    deviceManager.turnOn(deviceId);
}

void SmartHome::turnOff(const std::string& deviceId)
{
    deviceManager.turnOff(deviceId);
}

Appliance* SmartHome::findDevice(const std::string& deviceId)
{
    return deviceManager.findDevice(deviceId);
}

void SmartHome::showRooms()
{
    std::cout << "\n====================================\n";
    std::cout << "             " << homeName << '\n';
    std::cout << "====================================\n";

    for (const Room& room : rooms)
    {
        std::cout << "\nRoom: " << room.getName() << '\n';
        std::cout << "Devices: " << room.getDeviceCount() << '\n';

        for (const std::string& id : room.getDeviceIds())
        {
            Appliance* device = deviceManager.findDevice(id);

            if (device != nullptr)
            {
                std::cout << "  - "
                          << device->getName()
                          << " [" << device->getId() << "]\n";
            }
        }
    }
}

void SmartHome::showRoomDevices(const std::string& roomName)
{
    Room* room = findRoom(roomName);

    if (room == nullptr)
    {
        std::cout << "Room not found: " << roomName << '\n';
        return;
    }

    std::cout << "\n--- " << room->getName() << " ---\n";

    if (room->getDeviceCount() == 0)
    {
        std::cout << "No devices in this room.\n";
        return;
    }

    for (const std::string& id : room->getDeviceIds())
    {
        Appliance* device = deviceManager.findDevice(id);

        if (device != nullptr)
        {
            device->showStatus();
            std::cout << "--------------------------\n";
        }
    }
}

void SmartHome::showAllDevices()
{
    deviceManager.showAllDevices();
}

int SmartHome::getRoomCount() const
{
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