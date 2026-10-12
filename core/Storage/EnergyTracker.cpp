
#include "EnergyTracker.h"

#include <ctime>
#include <iostream>

EnergyTracker::EnergyTracker(sqlite3* connection)
    : db(connection)
{
}

bool EnergyTracker::initializeSchema()
{
    if (db == nullptr)
        return false;

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS energy_sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT NOT NULL,
            device_name TEXT NOT NULL,
            room_name TEXT NOT NULL,
            power_watts INTEGER NOT NULL,
            started_at INTEGER NOT NULL,
            ended_at INTEGER NOT NULL DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_energy_sessions_device
        ON energy_sessions(device_id, ended_at);

        CREATE INDEX IF NOT EXISTS idx_energy_sessions_started
        ON energy_sessions(started_at);
    )";

    char* errorMessage = nullptr;

    const int result = sqlite3_exec(
        db, sql, nullptr, nullptr, &errorMessage
    );

    if (result != SQLITE_OK)
    {
        std::cerr << "Energy schema error: "
                  << (errorMessage ? errorMessage : "Unknown error")
                  << '\n';

        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

bool EnergyTracker::startSession(
    const DeviceRecord& device,
    long long startedAt)
{
    if (db == nullptr || device.id.empty())
        return false;

    if (device.status != 1)
        return true;

    if (startedAt == 0)
        startedAt = static_cast<long long>(std::time(nullptr));

    // The INSERT only happens if the device has no active session.
    // This prevents duplicate sessions on repeated ON requests
    // and when SmartHomeOS restarts.
    const char* sql = R"(
        INSERT INTO energy_sessions (
            device_id,
            device_name,
            room_name,
            power_watts,
            started_at,
            ended_at
        )
        SELECT ?, ?, ?, ?, ?, 0
        WHERE NOT EXISTS (
            SELECT 1
            FROM energy_sessions
            WHERE device_id = ?
              AND ended_at = 0
        );
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare energy session failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, device.id.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(
        stmt, 2, device.name.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(
        stmt, 3, device.roomName.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_int(stmt, 4, device.powerWatts);
    sqlite3_bind_int64(stmt, 5, startedAt);

    sqlite3_bind_text(
        stmt, 6, device.id.c_str(), -1, SQLITE_TRANSIENT);

    const int result = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE)
    {
        std::cerr << "Start energy session failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    return true;
}

bool EnergyTracker::endSession(
    const std::string& deviceId,
    long long endedAt)
{
    if (db == nullptr || deviceId.empty())
        return false;

    if (endedAt == 0)
        endedAt = static_cast<long long>(std::time(nullptr));

    const char* sql = R"(
        UPDATE energy_sessions
        SET ended_at = ?
        WHERE device_id = ?
          AND ended_at = 0;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare end session failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    sqlite3_bind_int64(stmt, 1, endedAt);

    sqlite3_bind_text(
        stmt, 2, deviceId.c_str(), -1, SQLITE_TRANSIENT);

    const int result = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE)
    {
        std::cerr << "End energy session failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    return true;
}

std::vector<EnergySessionRecord>
EnergyTracker::loadSessions() const
{
    std::vector<EnergySessionRecord> sessions;

    if (db == nullptr)
        return sessions;

    const char* sql = R"(
        SELECT
            id,
            device_id,
            device_name,
            room_name,
            power_watts,
            started_at,
            ended_at
        FROM energy_sessions
        ORDER BY started_at DESC, id DESC;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Load energy sessions failed: "
                  << sqlite3_errmsg(db) << '\n';
        return sessions;
    }

    const long long now =
        static_cast<long long>(std::time(nullptr));

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        EnergySessionRecord session;

        session.id = sqlite3_column_int64(stmt, 0);

        auto readText = [stmt](int column) -> std::string
        {
            const unsigned char* value =
                sqlite3_column_text(stmt, column);

            return value
                ? reinterpret_cast<const char*>(value)
                : "";
        };

        session.deviceId = readText(1);
        session.deviceName = readText(2);
        session.roomName = readText(3);

        session.powerWatts = sqlite3_column_int(stmt, 4);
        session.startedAt = sqlite3_column_int64(stmt, 5);
        session.endedAt = sqlite3_column_int64(stmt, 6);

        const long long effectiveEnd =
            session.endedAt == 0 ? now : session.endedAt;

        const long long durationSeconds =
            effectiveEnd > session.startedAt
                ? effectiveEnd - session.startedAt
                : 0;

        // Energy (kWh) = watts × seconds / 3,600,000.
        session.energyKwh =
            static_cast<double>(session.powerWatts) *
            static_cast<double>(durationSeconds) /
            3600000.0;

        sessions.push_back(session);
    }

    sqlite3_finalize(stmt);

    return sessions;
}