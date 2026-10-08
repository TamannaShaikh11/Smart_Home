#include "Database.h"

#include <iostream>
#include <stdexcept>

Database::Database(const std::string& filename)
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
            "Failed to open database: " + error
        );
    }

    std::cout << "SQLite database connected successfully.\n";
}

Database::~Database()
{
    if (db != nullptr)
        sqlite3_close(db);
}

bool Database::execute(const std::string& sql)
{
    char* errorMessage = nullptr;

    int result = sqlite3_exec(
        db,
        sql.c_str(),
        nullptr,
        nullptr,
        &errorMessage
    );

    if (result != SQLITE_OK)
    {
        std::cerr << "Database error: "
                  << (errorMessage != nullptr
                          ? errorMessage
                          : "Unknown error")
                  << '\n';

        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

bool Database::initializeSchema()
{
    const char* createRoomsTable = R"(
        CREATE TABLE IF NOT EXISTS rooms (
            name TEXT PRIMARY KEY
        );
    )";

    const char* createDevicesTable = R"(
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

    if (!execute("PRAGMA foreign_keys = ON;"))
        return false;

    if (!execute(createRoomsTable))
        return false;

    if (!execute(createDevicesTable))
        return false;

    std::cout << "Database tables initialized successfully.\n";
    return true;
}

sqlite3* Database::getConnection() const
{
    return db;
}
