#include "core/storage/Database.h"
#include <iostream>
#include <exception>

int main()
{
    try
    {
        Database database("smarthome_test.db");

        std::cout << "Starting database initialization...\n";

        bool success = database.initializeSchema();

        if (success)
        {
            std::cout << "Database tables initialized successfully.\n";
        }
        else
        {
            std::cout << "Database initialization failed.\n";
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
