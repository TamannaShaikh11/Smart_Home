
#include "SmartHome.h"

#include <iostream>
#include <stdexcept>

// Constructor
SmartHome::SmartHome(const std::string& name)
    : homeName(name),
      database("smarthome.db"),
      deviceManager()
{
    if (homeName.empty()) {
        throw std::invalid_argument(
            "Smart home name cannot be empty."
        );
    }

    if (!database.initializeSchema()) {
        std::cerr << "Warning: Database schema initialization failed.\n";
    }

    deviceManager.setExpiryCallback(
        [this](const std::string& deviceId) {
            handleExpiredDevice(deviceId);
        }
    );

    std::cout << "SmartHome created: "
              << homeName << '\n';
}

// Destructor
SmartHome::~SmartHome()
{
    // Stop the worker before the rest of SmartHome is destroyed.
    deviceManager.setExpiryCallback(nullptr);
    deviceManager.stopExpiryWorker();
}

// Find a room
Room* SmartHome::findRoom(const std::string& roomName)
{
    for (auto& room : rooms) {
        if (room.getName() == roomName) {
            return &room;
        }
    }

    return nullptr;
}

// Find a room (const version)
const Room* SmartHome::findRoom(
    const std::string& roomName
) const
{
    for (const auto& room : rooms) {
        if (room.getName() == roomName) {
            return &room;
        }
    }

    return nullptr;
}

// Add a room
bool SmartHome::addRoom(const std::string& roomName)
{
    if (roomName.empty()) {
        std::cout << "Room name cannot be empty.\n";
        return false;
    }

    std::lock_guard<std::mutex> lock(homeMutex);

    if (findRoom(roomName) != nullptr) {
        std::cout << "Room already exists: "
                  << roomName << '\n';
        return false;
    }

    rooms.emplace_back(roomName);

    if (!database.saveRoom(roomName)) {
        std::cerr << "Warning: Could not save room to database.\n";
    }

    std::cout << "Room added: " << roomName << '\n';
    return true;
}

// Add a permanent device
void SmartHome::addDevice(Appliance* device)
{
    if (device == nullptr) {
        std::cout << "Cannot add a null device.\n";
        return;
    }

    const std::string roomName = device->getRoom();
    const std::string deviceId = device->getId();

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        if (roomName.empty()) {
            delete device;
            throw std::invalid_argument(
                "Device room name cannot be empty."
            );
        }

        if (findRoom(roomName) == nullptr) {
            rooms.emplace_back(roomName);

            if (!database.saveRoom(roomName)) {
                std::cerr << "Warning: Could not save room.\n";
            }
        }

        Room* room = findRoom(roomName);

        if (room != nullptr) {
            room->addDevice(deviceId);
        }
    }

    deviceManager.addDevice(device);

    std::cout << "Permanent device added: "
              << deviceId << '\n';
}

// Add a temporary device
void SmartHome::addTemporaryDevice(
    Appliance* device,
    int lifetimeSeconds
)
{
    if (device == nullptr) {
        std::cout << "Cannot add a null device.\n";
        return;
    }

    if (lifetimeSeconds < 0) {
        delete device;
        throw std::invalid_argument(
            "Device lifetime cannot be negative."
        );
    }

    const std::string roomName = device->getRoom();
    const std::string deviceId = device->getId();

    if (roomName.empty()) {
        delete device;
        throw std::invalid_argument(
            "Device room name cannot be empty."
        );
    }

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        if (findRoom(roomName) == nullptr) {
            rooms.emplace_back(roomName);

            if (!database.saveRoom(roomName)) {
                std::cerr << "Warning: Could not save room.\n";
            }
        }

        Room* room = findRoom(roomName);

        if (room != nullptr) {
            room->addDevice(deviceId);
        }
    }

    deviceManager.addTemporaryDevice(
        device,
        lifetimeSeconds
    );

    std::cout << "Temporary device added: "
              << deviceId << '\n';
}

// Remove a device
bool SmartHome::removeDevice(const std::string& deviceId)
{
    if (deviceId.empty()) {
        return false;
    }

    const bool removed = deviceManager.removeDevice(deviceId);

    if (!removed) {
        std::cout << "Device not found: "
                  << deviceId << '\n';
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        for (auto& room : rooms) {
            room.removeDevice(deviceId);
        }
    }

    if (!database.deleteDevice(deviceId)) {
        std::cerr << "Warning: Could not delete device from database.\n";
    }

    std::cout << "Device removed: "
              << deviceId << '\n';

    return true;
}

// Turn a device on
void SmartHome::turnOn(const std::string& deviceId)
{
    deviceManager.turnOn(deviceId);
}

// Turn a device off
void SmartHome::turnOff(const std::string& deviceId)
{
    deviceManager.turnOff(deviceId);
}

// Check whether a device exists
bool SmartHome::hasDevice(const std::string& deviceId) const
{
    return deviceManager.hasDevice(deviceId);
}

// Display all rooms
void SmartHome::showRooms()
{
    std::lock_guard<std::mutex> lock(homeMutex);

    if (rooms.empty()) {
        std::cout << "No rooms available.\n";
        return;
    }

    std::cout << "\n===== ROOMS =====\n";

    for (const auto& room : rooms) {
        std::cout << "Room: " << room.getName()
                  << " | Devices: "
                  << room.getDeviceCount() << '\n';
    }
}

// Display devices assigned to a room
void SmartHome::showRoomDevices(
    const std::string& roomName
)
{
    std::vector<std::string> deviceIds;

    {
        std::lock_guard<std::mutex> lock(homeMutex);

        const Room* room = findRoom(roomName);

        if (room == nullptr) {
            std::cout << "Room not found: "
                      << roomName << '\n';
            return;
        }

        deviceIds = room->getDeviceIds();
    }

    std::cout << "\n===== DEVICES IN "
              << roomName << " =====\n";

    if (deviceIds.empty()) {
        std::cout << "No devices in this room.\n";
        return;
    }

    for (const auto& id : deviceIds) {
        std::cout << "Device ID: " << id << '\n';
    }
}

// Display all devices
void SmartHome::showAllDevices()
{
    deviceManager.showAllDevices();
}

// Get room count
int SmartHome::getRoomCount() const
{
    std::lock_guard<std::mutex> lock(homeMutex);

    return static_cast<int>(rooms.size());
}

// Get device count
int SmartHome::getDeviceCount()
{
    return deviceManager.getDeviceCount();
}

// Get home name
std::string SmartHome::getHomeName() const
{
    return homeName;
}

// Handle a temporary device that has expired
void SmartHome::handleExpiredDevice(
    const std::string& deviceId
)
{
    {
        std::lock_guard<std::mutex> lock(homeMutex);

        for (auto& room : rooms) {
            room.removeDevice(deviceId);
        }
    }

    if (!database.deleteDevice(deviceId)) {
        std::cerr << "Warning: Could not delete expired device from database.\n";
    }

    std::cout << "Temporary device expired and removed: "
              << deviceId << '\n';
}