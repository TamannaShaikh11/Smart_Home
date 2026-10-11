
#ifndef SMARTHOME_H
#define SMARTHOME_H

#include <string>
#include <vector>
#include <mutex>

#include "Room.h"
#include "../control/DeviceManager.h"
#include "../storage/Database.h"

class SmartHome
{
private:
    std::string homeName;
    std::vector<Room> rooms;

    mutable std::mutex homeMutex;

    // Database is declared before DeviceManager so that
    // DeviceManager is destroyed first.
    Database database;
    DeviceManager deviceManager;

    Room* findRoom(const std::string& roomName);
    const Room* findRoom(const std::string& roomName) const;

    void handleExpiredDevice(const std::string& deviceId);

public:
    explicit SmartHome(const std::string& name);
    ~SmartHome();

    SmartHome(const SmartHome&) = delete;
    SmartHome& operator=(const SmartHome&) = delete;

    bool addRoom(const std::string& roomName);

    void addDevice(Appliance* device);

    // Zero means manual removal only.
    void addTemporaryDevice(
        Appliance* device,
        int lifetimeSeconds
    );

    bool removeDevice(const std::string& deviceId);

    void turnOn(const std::string& deviceId);
    void turnOff(const std::string& deviceId);

    bool hasDevice(const std::string& deviceId) const;

    void showRooms();
    void showRoomDevices(const std::string& roomName);
    void showAllDevices();

    int getRoomCount() const;
    int getDeviceCount();

    std::string getHomeName() const;
};

#endif