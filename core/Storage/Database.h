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
};

class Database
{
private:
    sqlite3* db;

public:
    explicit Database(const std::string& filename);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool execute(const std::string& sql);
    bool initializeSchema();

    bool saveRoom(const std::string& roomName);
    bool saveDevice(const DeviceRecord& device);
    bool updateDevice(const DeviceRecord& device);
    bool deleteDevice(const std::string& deviceId);
    
    bool renameRoom(
        const std::string& oldName,
        const std::string& newName
    );

    bool deleteRoom(const std::string& roomName);

    std::vector<std::string> loadRooms();
    std::vector<DeviceRecord> loadDevices();

    sqlite3* getConnection() const;
};

#endif
