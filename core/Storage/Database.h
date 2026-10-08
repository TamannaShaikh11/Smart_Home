#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <sqlite3.h>

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

    sqlite3* getConnection() const;
};

#endif
