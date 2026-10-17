#ifndef SMARTHOME_H
#define SMARTHOME_H

#include <string>
#include <vector>
#include <mutex>

#include "Room.h"
#include "../control/DeviceManager.h"
#include "../storage/Database.h"
#include "../storage/EnergyTracker.h"

class SmartHome
{
private:
    std::string homeName;
    std::vector<Room> rooms;

    mutable std::mutex homeMutex;

    // Database is declared before DeviceManager so that
    // DeviceManager is destroyed first.
    Database database;
    EnergyTracker energyTracker;
    DeviceManager deviceManager;

    Room *findRoom(const std::string &roomName);
    const Room *findRoom(const std::string &roomName) const;

    void handleExpiredDevice(const std::string &deviceId);

public:
    explicit SmartHome(const std::string &name);
    ~SmartHome();

    SmartHome(const SmartHome &) = delete;
    SmartHome &operator=(const SmartHome &) = delete;

    bool addRoom(const std::string &roomName);

    bool renameRoom(
        const std::string &oldName,
        const std::string &newName);

    bool deleteRoom(const std::string &roomName);

    bool roomExists(const std::string &roomName) const;

    void addDevice(Appliance *device);

    // Zero means manual removal only.
    void addTemporaryDevice(
        Appliance *device,
        int lifetimeSeconds);

    bool removeDevice(const std::string &deviceId);

    void turnOn(const std::string &deviceId);
    void turnOff(const std::string &deviceId);

    bool hasDevice(const std::string &deviceId) const;

    void showRooms();
    void showRoomDevices(const std::string &roomName);
    void showAllDevices();

    int getRoomCount() const;
    int getDeviceCount();

    std::string getHomeName() const;

    // Read-only snapshots for the frontend API.
    std::vector<std::string> getRoomNames() const;
    std::vector<DeviceRecord> getAllDeviceRecords() const;
    std::vector<EnergySessionRecord> getEnergySessions() const;

    // Persistent user profiles and room preferences.
    bool createUserProfile(const UserProfile &profile);
    std::vector<UserProfile> getUserProfiles() const;

    bool saveUserPreferences(
        const UserPreferenceRecord &preferences);

    bool getUserPreferences(
        const std::string &userId,
        const std::string &roomName,
        UserPreferenceRecord &preferences) const;
};

#endif