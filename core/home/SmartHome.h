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
    bool setDeviceSetting(const std::string &deviceId, int setting);
    bool setLightBrightness(const std::string &deviceId, int brightness);
    bool setLightColor(const std::string &deviceId, const std::string &color);
    bool setLightColorTemperature(
        const std::string &deviceId, int kelvin);
    bool updateDeviceDetails(
        const std::string &deviceId,
        const std::string &name,
        int powerWatts);

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

    bool addAutomationEnrollment(
        const AutomationEnrollmentRecord &enrollment);
    std::vector<AutomationEnrollmentRecord>
    getAutomationEnrollments() const;
    bool getAutomationEnrollment(
        const std::string &id,
        AutomationEnrollmentRecord &enrollment) const;
    bool deleteAutomationEnrollment(const std::string &id);
    bool recordAutomationEvent(const AutomationEventRecord &event);
    std::vector<AutomationEventRecord> getAutomationEvents(
        int limit = 50) const;
    bool getAutomationEnabled() const;
    bool setAutomationEnabled(bool enabled);

    bool saveModePreset(const ModePresetRecord &preset);
    std::vector<ModePresetRecord> getModePresets(
        const std::string &modeType) const;
    bool recordModeHistory(ModeHistoryRecord &history);
    std::vector<ModeHistoryRecord> getModeHistory(int limit) const;
    bool getLatestRestorableModeHistory(ModeHistoryRecord &history) const;
    bool updateModeHistoryRestore(
        long long id,
        const std::string &restoreDetailsJson,
        long long restoredAt);
};

#endif