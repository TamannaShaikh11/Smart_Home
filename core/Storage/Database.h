#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include <sqlite3.h>

struct DeviceRecord
{
    std::string id;
    std::string name;
    std::string type;
    std::string roomName;

    int status = 0;
    int powerWatts = 0;
    int setting = 0;
    int runtimeMinutes = 0;

    int lifetime = 0;
    long long expiryTime = 0;
    std::string lightType = "standard";
    int brightness = 100;
    std::string color = "#FFFFFF";
    int colorTemperature = 4000;
    bool supportsBrightness = false;
    bool supportsColor = false;
    bool supportsColorTemperature = false;
};

struct UserProfile
{
    std::string id;
    std::string displayName;
};

struct UserPreferenceRecord
{
    std::string userId;
    std::string roomName;

    // -1 means no preference has been set.
    int acTemperature = -1;
    int heaterTemperature = -1;
    int fanSpeed = -1;

    // -1 = unspecified, 0 = prefer OFF, 1 = prefer ON.
    int lightOn = -1;
};

struct AutomationEnrollmentRecord
{
    std::string id;
    std::string profileId;
    std::string profileName;
    long long createdAt = 0;
};

struct AutomationEventRecord
{
    long long id = 0;
    std::string roomName;
    std::string profileId;
    long long eventTime = 0;
    std::string outcome;
    std::string settingsChanged;
};

class Database
{
private:
    sqlite3 *db;
    std::string filename;

public:
    explicit Database(const std::string &filename);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool execute(const std::string &sql);
    bool initializeSchema();

    bool saveRoom(const std::string &roomName);
    bool saveDevice(const DeviceRecord &device);
    bool updateDevice(const DeviceRecord &device);
    bool deleteDevice(const std::string &deviceId);

    bool createProfile(const UserProfile &profile);
    bool profileExists(const std::string &userId) const;
    std::vector<UserProfile> loadProfiles() const;

    bool savePreferences(const UserPreferenceRecord &preferences);
    bool loadPreferences(
        const std::string &userId,
        const std::string &roomName,
        UserPreferenceRecord &preferences)const;

    bool addAutomationEnrollment(
        const AutomationEnrollmentRecord &enrollment);
    std::vector<AutomationEnrollmentRecord>
    loadAutomationEnrollments() const;
    bool loadAutomationEnrollment(
        const std::string &id,
        AutomationEnrollmentRecord &enrollment) const;
    bool deleteAutomationEnrollment(const std::string &id);
    bool addAutomationEvent(const AutomationEventRecord &event);
    std::vector<AutomationEventRecord> loadAutomationEvents(
        int limit = 50) const;
    bool getAutomationEnabled() const;
    bool setAutomationEnabled(bool enabled);

    bool renameRoom(
        const std::string &oldName,
        const std::string &newName);

    bool deleteRoom(const std::string &roomName);

    std::vector<std::string> loadRooms();
    std::vector<DeviceRecord> loadDevices();

    sqlite3 *getConnection() const;
};

#endif
