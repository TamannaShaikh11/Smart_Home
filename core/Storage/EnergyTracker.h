
#ifndef ENERGYTRACKER_H
#define ENERGYTRACKER_H

#include <string>
#include <vector>
#include <sqlite3.h>

#include "Database.h"

struct EnergySessionRecord
{
    long long id = 0;
    std::string deviceId;
    std::string deviceName;
    std::string roomName;

    int powerWatts = 0;

    long long startedAt = 0;
    long long endedAt = 0; // 0 means the session is active.

    double energyKwh = 0.0;
};

class EnergyTracker
{
private:
    sqlite3* db;

public:
    explicit EnergyTracker(sqlite3* connection);

    bool initializeSchema();

    // Start a session only if this device has no active session.
    bool startSession(
        const DeviceRecord& device,
        long long startedAt = 0
    );

    // Close the active session without deleting its history.
    bool endSession(
        const std::string& deviceId,
        long long endedAt = 0
    );

    std::vector<EnergySessionRecord> loadSessions() const;
};

#endif