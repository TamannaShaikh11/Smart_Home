#include <iostream>
#include <string>
#include <limits>
#include <exception>

#include "core/home/SmartHome.h"
#include "core/devices/Light.h"
#include "core/devices/Fan.h"
#include "core/devices/AC.h"
#include "core/devices/Heater.h"

using namespace std;

void clearInput()
{
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

string readLine(const string& prompt)
{
    string value;

    cout << prompt;
    getline(cin, value);

    return value;
}

void seedHome(SmartHome& home)
{
    // Create five rooms.
    home.addRoom("Living Room");
    home.addRoom("Bedroom");
    home.addRoom("Kitchen");
    home.addRoom("Study Room");
    home.addRoom("Bathroom");

    // LIVING ROOM: 5 devices
    home.addDevice(new Light(
        "LIVING_MAIN_LIGHT",
        "Main Light",
        "Living Room"
    ));

    home.addDevice(new Light(
        "LIVING_AMBIENT_LIGHT",
        "Ambient Light",
        "Living Room"
    ));

    home.addDevice(new Fan(
        "LIVING_FAN",
        "Ceiling Fan",
        "Living Room"
    ));

    home.addDevice(new AC(
        "LIVING_AC",
        "Living Room AC",
        "Living Room"
    ));

    home.addDevice(new Heater(
        "LIVING_HEATER",
        "Portable Heater",
        "Living Room"
    ));

    // BEDROOM: 5 devices
    home.addDevice(new Light(
        "BEDROOM_MAIN_LIGHT",
        "Bedroom Ceiling Light",
        "Bedroom"
    ));

    home.addDevice(new Light(
        "BEDROOM_BEDSIDE_LIGHT",
        "Bedside Light",
        "Bedroom"
    ));

    home.addDevice(new Fan(
        "BEDROOM_FAN",
        "Bedroom Fan",
        "Bedroom"
    ));

    home.addDevice(new AC(
        "BEDROOM_AC",
        "Bedroom AC",
        "Bedroom"
    ));

    home.addDevice(new Heater(
        "BEDROOM_HEATER",
        "Bedroom Heater",
        "Bedroom"
    ));

    // KITCHEN: 3 devices
    home.addDevice(new Light(
        "KITCHEN_MAIN_LIGHT",
        "Kitchen Main Light",
        "Kitchen"
    ));

    home.addDevice(new Light(
        "KITCHEN_COUNTER_LIGHT",
        "Counter Light",
        "Kitchen"
    ));

    home.addDevice(new Fan(
        "KITCHEN_EXHAUST_FAN",
        "Kitchen Exhaust Fan",
        "Kitchen"
    ));

    // STUDY ROOM: 4 devices
    home.addDevice(new Light(
        "STUDY_MAIN_LIGHT",
        "Study Ceiling Light",
        "Study Room"
    ));

    home.addDevice(new Light(
        "STUDY_DESK_LIGHT",
        "Study Desk Lamp",
        "Study Room"
    ));

    home.addDevice(new Fan(
        "STUDY_FAN",
        "Study Room Fan",
        "Study Room"
    ));

    home.addDevice(new AC(
        "STUDY_AC",
        "Study Room AC",
        "Study Room"
    ));

    // BATHROOM: 3 devices
    home.addDevice(new Light(
        "BATHROOM_MAIN_LIGHT",
        "Bathroom Light",
        "Bathroom"
    ));

    home.addDevice(new Fan(
        "BATHROOM_VENTILATION",
        "Ventilation Fan",
        "Bathroom"
    ));

    home.addDevice(new Heater(
        "BATHROOM_HEATER",
        "Water Heater",
        "Bathroom"
    ));
}

void showMenu()
{
    cout << "\n========== OOPS CP ==========\n";
    cout << "1. View all rooms and devices\n";
    cout << "2. View all device statuses\n";
    cout << "3. View one room\n";
    cout << "4. Turn ON a device\n";
    cout << "5. Turn OFF a device\n";
    cout << "6. Exit\n";
    cout << "Enter choice: ";
}

int main()
{
    try
    {
        SmartHome home("My Smart Home");

        seedHome(home);

        cout << "\nWelcome to OOPS CP!\n";
        cout << "Home: " << home.getHomeName() << '\n';
        cout << "Rooms registered: " << home.getRoomCount() << '\n';
        cout << "Devices registered: " << home.getDeviceCount() << '\n';

        int choice = 0;

        do
        {
            showMenu();

            if (!(cin >> choice))
            {
                cin.clear();
                clearInput();

                cout << "Please enter a valid number.\n";
                continue;
            }

            clearInput();

            try
            {
                switch (choice)
                {
                    case 1:
                        home.showRooms();
                        break;

                    case 2:
                        home.showAllDevices();
                        break;

                    case 3:
                    {
                        string roomName = readLine(
                            "Enter room name: "
                        );

                        home.showRoomDevices(roomName);
                        break;
                    }

                    case 4:
                    {
                        string id = readLine(
                            "Enter device ID to turn ON: "
                        );

                        home.turnOn(id);
                        cout << "Device turned ON.\n";
                        break;
                    }

                    case 5:
                    {
                        string id = readLine(
                            "Enter device ID to turn OFF: "
                        );

                        home.turnOff(id);
                        cout << "Device turned OFF.\n";
                        break;
                    }

                    case 6:
                        cout << "Exiting OOPS CP...\n";
                        break;

                    default:
                        cout << "Invalid choice. Try again.\n";
                }
            }
            catch (const exception& error)
            {
                cout << "Operation failed: "
                     << error.what() << '\n';
            }

        } while (choice != 6);
    }
    catch (const exception& error)
    {
        cerr << "Could not initialize the smart home: "
             << error.what() << '\n';

        return 1;
    }

    return 0;
}