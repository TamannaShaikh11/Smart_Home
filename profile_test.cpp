#include <iostream>
#include <string>
#include "core/storage/Database.h"

int main()
{
    const std::string dbFile = "profile_test.db";

    // First run: create a profile and save preferences.
    {
        Database db(dbFile);

        if (!db.initializeSchema())
        {
            std::cerr << "Schema initialization failed.\n";
            return 1;
        }

        UserProfile profile;
        profile.id = "user_001";
        profile.displayName = "Test User";

        if (!db.createProfile(profile))
        {
            std::cerr << "Profile creation failed.\n";
            return 1;
        }

        UserPreferenceRecord preferences;
        preferences.userId = "user_001";
        preferences.roomName = "Bedroom";
        preferences.acTemperature = 24;
        preferences.heaterTemperature = 22;
        preferences.fanSpeed = 3;
        preferences.lightOn = 1;

        if (!db.savePreferences(preferences))
        {
            std::cerr << "Saving preferences failed.\n";
            return 1;
        }

        std::cout << "Profile and preferences saved.\n";
    }

    // Second run: reopen the database and retrieve the data.
    {
        Database db(dbFile);

        if (!db.initializeSchema())
        {
            std::cerr << "Schema initialization failed.\n";
            return 1;
        }

        UserPreferenceRecord loaded;

        if (!db.loadPreferences(
                "user_001", "Bedroom", loaded))
        {
            std::cerr << "Loading preferences failed.\n";
            return 1;
        }

        std::cout << "\nPreferences restored after reopening:\n";
        std::cout << "AC temperature: "
                  << loaded.acTemperature << '\n';
        std::cout << "Heater temperature: "
                  << loaded.heaterTemperature << '\n';
        std::cout << "Fan speed: "
                  << loaded.fanSpeed << '\n';
        std::cout << "Light preference: "
                  << loaded.lightOn << '\n';

        if (loaded.acTemperature != 24 ||
            loaded.heaterTemperature != 22 ||
            loaded.fanSpeed != 3 ||
            loaded.lightOn != 1)
        {
            std::cerr << "Persistence test FAILED.\n";
            return 1;
        }

        std::cout << "\nPersistence test PASSED!\n";
    }

    return 0;
}
