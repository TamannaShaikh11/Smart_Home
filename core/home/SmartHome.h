#ifndef SMARTHOME_H
#define SMARTHOME_H

#include <string>
#include <vector>

#include "Room.h"
#include "../control/DeviceManager.h"

class SmartHome
{
private:
    std::string homeName;
    std::vector<Room> rooms;

    // DeviceManager remains the sole owner of appliance objects.
    DeviceManager deviceManager;

    Room* findRoom(const std::string& roomName);
    const Room* findRoom(const std::string& roomName) const;

public:
    explicit SmartHome(const std::string& name);

    bool addRoom(const std::string& roomName);

    // Ownership transfers to SmartHome only if registration succeeds.
    void addDevice(Appliance* device);

    void turnOn(const std::string& deviceId);
    void turnOff(const std::string& deviceId);

    Appliance* findDevice(const std::string& deviceId);

    void showRooms();
    void showRoomDevices(const std::string& roomName);
    void showAllDevices();

    int getRoomCount() const;
    int getDeviceCount();

    std::string getHomeName() const;
};

#endif