
#include <iostream>
#include "core/home/SmartHome.h"

int main()
{
    try
    {
        SmartHome home("Profile Integration Test");

        UserProfile profile;
        profile.id = "integration_user_001";
        profile.displayName = "Test User";

        if (home.createUserProfile(profile))
        {
            std::cout << "Profile created successfully.\n";
        }
        else
        {
            std::cout << "Profile already exists or creation failed.\n";
        }

        // Check that the profile can be retrieved.
        bool found = false;

        for (const UserProfile& user : home.getUserProfiles())
        {
            if (user.id == profile.id)
            {
                found = true;
                std::cout << "Profile retrieved: "
                          << user.displayName << '\n';
                break;
            }
        }

        if (!found)
        {
            std::cerr << "Profile retrieval failed.\n";
            return 1;
        }

        // Save preferences for the Bedroom.
        UserPreferenceRecord preferences;
        preferences.userId = profile.id;
        preferences.roomName = "Bedroom";
        preferences.acTemperature = 24;
        preferences.heaterTemperature = 22;
        preferences.fanSpeed = 3;
        preferences.lightOn = 1;

        if (!home.saveUserPreferences(preferences))
        {
            std::cerr << "Could not save preferences.\n";
            return 1;
        }

        std::cout << "Preferences saved successfully.\n";

        // Read the preferences back.
        UserPreferenceRecord loaded;

        if (!home.getUserPreferences(
                profile.id, "Bedroom", loaded))
        {
            std::cerr << "Could not load preferences.\n";
            return 1;
        }

        std::cout << "\nLoaded preferences:\n";
        std::cout << "AC temperature: "
                  << loaded.acTemperature << '\n';
        std::cout << "Heater temperature: "
                  << loaded.heaterTemperature << '\n';
        std::cout << "Fan speed: "
                  << loaded.fanSpeed << '\n';
        std::cout << "Light preference: "
                  << loaded.lightOn << '\n';

        std::cout << "\nIntegration test passed!\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
