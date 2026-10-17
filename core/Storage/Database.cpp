#include "Database.h"

#include <iostream>
#include <stdexcept>

Database::Database(const std::string &filename)
    : db(nullptr)
{
    int result = sqlite3_open(filename.c_str(), &db);

    if (result != SQLITE_OK)
    {
        std::string error =
            db != nullptr ? sqlite3_errmsg(db) : "Unknown error";

        if (db != nullptr)
        {
            sqlite3_close(db);
            db = nullptr;
        }

        throw std::runtime_error(
            "Failed to open database: " + error);
    }

    std::cout << "SQLite database connected successfully.\n";
}

Database::~Database()
{
    if (db != nullptr)
        sqlite3_close(db);
}

bool Database::execute(const std::string &sql)
{
    char *errorMessage = nullptr;

    int result = sqlite3_exec(
        db, sql.c_str(), nullptr, nullptr, &errorMessage);

    if (result != SQLITE_OK)
    {
        std::cerr << "Database error: "
                  << (errorMessage ? errorMessage : "Unknown error")
                  << '\n';

        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

bool Database::initializeSchema()
{
    if (!execute("PRAGMA foreign_keys = ON;"))
        return false;

    const char *roomsSql = R"(
        CREATE TABLE IF NOT EXISTS rooms (
            name TEXT PRIMARY KEY
        );
    )";

    const char *devicesSql = R"(
        CREATE TABLE IF NOT EXISTS devices (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            device_type TEXT NOT NULL,
            room_name TEXT NOT NULL,
            status INTEGER NOT NULL DEFAULT 0,
            power_watts INTEGER NOT NULL DEFAULT 0,
            setting INTEGER NOT NULL DEFAULT 0,
            runtime_minutes INTEGER NOT NULL DEFAULT 0,
            lifetime INTEGER NOT NULL DEFAULT 0,
            expiry_time INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (room_name) REFERENCES rooms(name)
        );
    )";

    if (!execute(roomsSql) || !execute(devicesSql))
        return false;

    const char *profilesSql = R"(
    CREATE TABLE IF NOT EXISTS user_profiles (
        id TEXT PRIMARY KEY,
        display_name TEXT NOT NULL
    );
)";

    const char *preferencesSql = R"(
    CREATE TABLE IF NOT EXISTS user_preferences (
        user_id TEXT NOT NULL,
        room_name TEXT NOT NULL,
        ac_temperature INTEGER NOT NULL DEFAULT -1,
        heater_temperature INTEGER NOT NULL DEFAULT -1,
        fan_speed INTEGER NOT NULL DEFAULT -1,
        light_on INTEGER NOT NULL DEFAULT -1,

        PRIMARY KEY (user_id, room_name),

        FOREIGN KEY (user_id)
            REFERENCES user_profiles(id)
            ON DELETE CASCADE
    );
)";

    return execute(profilesSql) && execute(preferencesSql);
}

bool Database::saveRoom(const std::string &roomName)
{
    const char *sql =
        "INSERT OR IGNORE INTO rooms (name) VALUES (?);";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare room failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, roomName.c_str(), -1, SQLITE_TRANSIENT);

    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE)
    {
        std::cerr << "Save room failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    return true;
}

bool Database::saveDevice(const DeviceRecord &device)
{
    const char *sql = R"(
        INSERT OR REPLACE INTO devices (
            id, name, device_type, room_name,
            status, power_watts, setting, runtime_minutes,
            lifetime, expiry_time
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare device failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    sqlite3_bind_text(stmt, 1, device.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, device.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, device.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, device.roomName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, device.status);
    sqlite3_bind_int(stmt, 6, device.powerWatts);
    sqlite3_bind_int(stmt, 7, device.setting);
    sqlite3_bind_int(stmt, 8, device.runtimeMinutes);
    sqlite3_bind_int(stmt, 9, device.lifetime);
    sqlite3_bind_int64(stmt, 10, device.expiryTime);

    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE)
    {
        std::cerr << "Save device failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    return true;
}

bool Database::updateDevice(const DeviceRecord &device)
{
    const char *sql = R"(
        UPDATE devices SET
            name = ?,
            device_type = ?,
            room_name = ?,
            status = ?,
            power_watts = ?,
            setting = ?,
            runtime_minutes = ?,
            lifetime = ?,
            expiry_time = ?
        WHERE id = ?;
    )";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare update failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    sqlite3_bind_text(stmt, 1, device.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, device.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, device.roomName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, device.status);
    sqlite3_bind_int(stmt, 5, device.powerWatts);
    sqlite3_bind_int(stmt, 6, device.setting);
    sqlite3_bind_int(stmt, 7, device.runtimeMinutes);
    sqlite3_bind_int(stmt, 8, device.lifetime);
    sqlite3_bind_int64(stmt, 9, device.expiryTime);
    sqlite3_bind_text(stmt, 10, device.id.c_str(), -1, SQLITE_TRANSIENT);

    int result = sqlite3_step(stmt);
    int changed = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE)
    {
        std::cerr << "Update device failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    return changed > 0;
}

bool Database::deleteDevice(const std::string &deviceId)
{
    const char *sql =
        "DELETE FROM devices WHERE id = ?;";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(
            db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare delete failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    int bindResult = sqlite3_bind_text(
        stmt,
        1,
        deviceId.c_str(),
        -1,
        SQLITE_TRANSIENT);

    if (bindResult != SQLITE_OK)
    {
        std::cerr << "Bind delete parameter failed: "
                  << sqlite3_errmsg(db) << '\n';

        sqlite3_finalize(stmt);
        return false;
    }

    int result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
    {
        std::cerr << "Delete device failed: "
                  << sqlite3_errmsg(db) << '\n';

        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);

    // SQLITE_DONE means the DELETE statement succeeded.
    // Zero matching rows is not an SQL execution failure.
    return true;
}

std::vector<std::string> Database::loadRooms()
{
    std::vector<std::string> rooms;
    const char *sql = "SELECT name FROM rooms ORDER BY name;";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Load rooms failed: "
                  << sqlite3_errmsg(db) << '\n';
        return rooms;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char *value = sqlite3_column_text(stmt, 0);

        if (value != nullptr)
            rooms.emplace_back(
                reinterpret_cast<const char *>(value));
    }

    sqlite3_finalize(stmt);
    return rooms;
}

std::vector<DeviceRecord> Database::loadDevices()
{
    std::vector<DeviceRecord> devices;

    const char *sql = R"(
        SELECT id, name, device_type, room_name,
               status, power_watts, setting, runtime_minutes,
               lifetime, expiry_time
        FROM devices
        ORDER BY id;
    )";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Load devices failed: "
                  << sqlite3_errmsg(db) << '\n';
        return devices;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        DeviceRecord device;

        auto readText = [stmt](int column) -> std::string
        {
            const unsigned char *value =
                sqlite3_column_text(stmt, column);

            return value
                       ? reinterpret_cast<const char *>(value)
                       : "";
        };

        device.id = readText(0);
        device.name = readText(1);
        device.type = readText(2);
        device.roomName = readText(3);
        device.status = sqlite3_column_int(stmt, 4);
        device.powerWatts = sqlite3_column_int(stmt, 5);
        device.setting = sqlite3_column_int(stmt, 6);
        device.runtimeMinutes = sqlite3_column_int(stmt, 7);
        device.lifetime = sqlite3_column_int(stmt, 8);
        device.expiryTime = sqlite3_column_int64(stmt, 9);

        devices.push_back(device);
    }

    sqlite3_finalize(stmt);
    return devices;
}

sqlite3 *Database::getConnection() const
{
    return db;
}

bool Database::renameRoom(
    const std::string &oldName,
    const std::string &newName)
{
    if (oldName.empty() || newName.empty())
        return false;

    if (oldName == newName)
        return true;

    if (!execute("BEGIN TRANSACTION;"))
        return false;

    auto runUpdate = [this](
                         const char *sql,
                         const std::string &first,
                         const std::string &second) -> bool
    {
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(
                db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            return false;
        }

        sqlite3_bind_text(
            stmt, 1, first.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_bind_text(
            stmt, 2, second.c_str(), -1, SQLITE_TRANSIENT);

        bool success = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);

        return success;
    };

    bool success = runUpdate(
        "INSERT INTO rooms(name) "
        "SELECT ? WHERE EXISTS "
        "(SELECT 1 FROM rooms WHERE name = ?);",
        newName, oldName);

    if (success)
    {
        success = runUpdate(
            "UPDATE devices SET room_name = ? "
            "WHERE room_name = ?;",
            newName, oldName);
    }

    if (success)
    {
        success = runUpdate(
            "DELETE FROM rooms WHERE name = ?;",
            oldName, oldName);
    }

    if (success)
        success = execute("COMMIT;");
    else
        execute("ROLLBACK;");

    return success;
}

bool Database::deleteRoom(const std::string &roomName)
{
    if (roomName.empty())
        return false;

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(
            db,
            "DELETE FROM rooms WHERE name = ? "
            "AND NOT EXISTS "
            "(SELECT 1 FROM devices WHERE room_name = ?);",
            -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, roomName.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(
        stmt, 2, roomName.c_str(), -1, SQLITE_TRANSIENT);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    int changed = sqlite3_changes(db);

    sqlite3_finalize(stmt);

    return success && changed > 0;
}

bool Database::createProfile(const UserProfile& profile)
{
    if (profile.id.empty() || profile.displayName.empty())
        return false;

    const char* sql =
        "INSERT INTO user_profiles (id, display_name) "
        "VALUES (?, ?);";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr)
        != SQLITE_OK)
    {
        std::cerr << "Prepare profile failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, profile.id.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(
        stmt, 2, profile.displayName.c_str(),
        -1, SQLITE_TRANSIENT);

    const int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE)
    {
        std::cerr << "Create profile failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    return true;
}

bool Database::profileExists(const std::string& userId)
{
    const char* sql =
        "SELECT 1 FROM user_profiles WHERE id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr)
        != SQLITE_OK)
        return false;

    sqlite3_bind_text(
        stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);

    const int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return result == SQLITE_ROW;
}

std::vector<UserProfile> Database::loadProfiles() const
{
    std::vector<UserProfile> profiles;

    const char* sql =
        "SELECT id, display_name "
        "FROM user_profiles ORDER BY display_name;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr)
        != SQLITE_OK)
    {
        std::cerr << "Load profiles failed: "
                  << sqlite3_errmsg(db) << '\n';
        return profiles;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        UserProfile profile;

        const unsigned char* id =
            sqlite3_column_text(stmt, 0);

        const unsigned char* name =
            sqlite3_column_text(stmt, 1);

        if (id)
            profile.id =
                reinterpret_cast<const char*>(id);

        if (name)
            profile.displayName =
                reinterpret_cast<const char*>(name);

        profiles.push_back(profile);
    }

    sqlite3_finalize(stmt);
    return profiles;
}

bool Database::savePreferences(
    const UserPreferenceRecord& preferences)
{
    if (preferences.userId.empty() ||
        preferences.roomName.empty() ||
        !profileExists(preferences.userId))
        return false;

    const char* sql = R"(
        INSERT INTO user_preferences (
            user_id, room_name,
            ac_temperature, heater_temperature,
            fan_speed, light_on
        )
        VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(user_id, room_name)
        DO UPDATE SET
            ac_temperature = excluded.ac_temperature,
            heater_temperature = excluded.heater_temperature,
            fan_speed = excluded.fan_speed,
            light_on = excluded.light_on;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr)
        != SQLITE_OK)
    {
        std::cerr << "Prepare preferences failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, preferences.userId.c_str(),
        -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(
        stmt, 2, preferences.roomName.c_str(),
        -1, SQLITE_TRANSIENT);

    sqlite3_bind_int(stmt, 3, preferences.acTemperature);
    sqlite3_bind_int(stmt, 4, preferences.heaterTemperature);
    sqlite3_bind_int(stmt, 5, preferences.fanSpeed);
    sqlite3_bind_int(stmt, 6, preferences.lightOn);

    const int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE)
    {
        std::cerr << "Save preferences failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    return true;
}

bool Database::loadPreferences(
    const std::string& userId,
    const std::string& roomName,
    UserPreferenceRecord& preferences) const
{
    const char* sql = R"(
        SELECT ac_temperature, heater_temperature,
               fan_speed, light_on
        FROM user_preferences
        WHERE user_id = ? AND room_name = ?;
    )";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr)
        != SQLITE_OK)
    {
        std::cerr << "Prepare preference load failed: "
                  << sqlite3_errmsg(db) << '\n';
        return false;
    }

    sqlite3_bind_text(
        stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_text(
        stmt, 2, roomName.c_str(), -1, SQLITE_TRANSIENT);

    const int result = sqlite3_step(stmt);

    if (result == SQLITE_ROW)
    {
        preferences.userId = userId;
        preferences.roomName = roomName;

        preferences.acTemperature =
            sqlite3_column_int(stmt, 0);

        preferences.heaterTemperature =
            sqlite3_column_int(stmt, 1);

        preferences.fanSpeed =
            sqlite3_column_int(stmt, 2);

        preferences.lightOn =
            sqlite3_column_int(stmt, 3);
    }

    sqlite3_finalize(stmt);

    return result == SQLITE_ROW;
}
