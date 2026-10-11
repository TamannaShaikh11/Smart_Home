#include <iostream>
#include <string>
#include <limits>
#include <memory>
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

string readLine(const string &prompt)
{
    string value;
    cout << prompt;
    getline(cin, value);
    return value;
}

unique_ptr<Appliance> createDevice(
    int type,
    const string &id,
    const string &name,
    const string &room)
{
    switch (type)
    {
    case 1:
        return make_unique<Light>(id, name, room);
    case 2:
        return make_unique<Fan>(id, name, room);
    case 3:
        return make_unique<AC>(id, name, room);
    case 4:
        return make_unique<Heater>(id, name, room);
    default:
        return nullptr;
    }
}

void seedHome(SmartHome &home)
{
    home.addRoom("Living Room");
    home.addRoom("Bedroom");
    home.addRoom("Kitchen");
    home.addRoom("Study Room");
    home.addRoom("Bathroom");

    // Living Room: 5
    home.addDevice(new Light(
        "LIVING_MAIN_LIGHT", "Main Light", "Living Room"));
    home.addDevice(new Light(
        "LIVING_AMBIENT_LIGHT", "Ambient Light", "Living Room"));
    home.addDevice(new Fan(
        "LIVING_FAN", "Ceiling Fan", "Living Room"));
    home.addDevice(new AC(
        "LIVING_AC", "Living Room AC", "Living Room"));
    home.addDevice(new Heater(
        "LIVING_HEATER", "Portable Heater", "Living Room"));

    // Bedroom: 5
    home.addDevice(new Light(
        "BEDROOM_MAIN_LIGHT", "Bedroom Ceiling Light", "Bedroom"));
    home.addDevice(new Light(
        "BEDROOM_BEDSIDE_LIGHT", "Bedside Light", "Bedroom"));
    home.addDevice(new Fan(
        "BEDROOM_FAN", "Bedroom Fan", "Bedroom"));
    home.addDevice(new AC(
        "BEDROOM_AC", "Bedroom AC", "Bedroom"));
    home.addDevice(new Heater(
        "BEDROOM_HEATER", "Bedroom Heater", "Bedroom"));

    // Kitchen: 3
    home.addDevice(new Light(
        "KITCHEN_MAIN_LIGHT", "Kitchen Main Light", "Kitchen"));
    home.addDevice(new Light(
        "KITCHEN_COUNTER_LIGHT", "Counter Light", "Kitchen"));
    home.addDevice(new Fan(
        "KITCHEN_EXHAUST_FAN", "Kitchen Exhaust Fan", "Kitchen"));

    // Study Room: 4
    home.addDevice(new Light(
        "STUDY_MAIN_LIGHT", "Study Ceiling Light", "Study Room"));
    home.addDevice(new Light(
        "STUDY_DESK_LIGHT", "Study Desk Lamp", "Study Room"));
    home.addDevice(new Fan(
        "STUDY_FAN", "Study Room Fan", "Study Room"));
    home.addDevice(new AC(
        "STUDY_AC", "Study Room AC", "Study Room"));

    // Bathroom: 3
    home.addDevice(new Light(
        "BATHROOM_MAIN_LIGHT", "Bathroom Light", "Bathroom"));
    home.addDevice(new Fan(
        "BATHROOM_VENTILATION", "Ventilation Fan", "Bathroom"));
    home.addDevice(new Heater(
        "BATHROOM_HEATER", "Water Heater", "Bathroom"));
}

void addRoomFromMenu(SmartHome &home)
{
    string roomName = readLine("Enter new room name: ");

    if (roomName.empty())
    {
        cout << "Room name cannot be empty.\n";
        return;
    }

    try
    {
        if (home.addRoom(roomName))
        {
            cout << "Room added successfully: "
                 << roomName << '\n';
        }
        else
        {
            cout << "Room already exists or could not be added.\n";
        }
    }
    catch (const exception &error)
    {
        cout << "Could not add room: "
             << error.what() << '\n';
    }
}

void addDeviceFromMenu(SmartHome &home, bool temporary)
{
    cout << "\nDevice types:\n";
    cout << "1. Light\n";
    cout << "2. Fan\n";
    cout << "3. Air Conditioner\n";
    cout << "4. Heater\n";
    cout << "Choice: ";

    int type;

    if (!(cin >> type))
    {
        cin.clear();
        clearInput();
        cout << "Invalid input.\n";
        return;
    }

    clearInput();

    if (type < 1 || type > 4)
    {
        cout << "Invalid device type.\n";
        return;
    }

    string id = readLine("Unique device ID: ");
    string name = readLine("Device name: ");
    string room = readLine("Room name: ");

    if (id.empty() || name.empty() || room.empty())
    {
        cout << "ID, name, and room are required.\n";
        return;
    }

    unique_ptr<Appliance> device =
        createDevice(type, id, name, room);

    if (!device)
    {
        cout << "Could not create device.\n";
        return;
    }

    int seconds = 0;

    if (temporary)
    {
        cout << "Expiry duration in seconds "
             << "(0 = manual removal only): ";

        if (!(cin >> seconds))
        {
            cin.clear();
            clearInput();
            cout << "Invalid duration.\n";
            return;
        }

        clearInput();

        if (seconds < 0)
        {
            cout << "Duration cannot be negative.\n";
            return;
        }
    }

    try
    {
        if (temporary)
        {
            home.addTemporaryDevice(device.get(), seconds);
        }
        else
        {
            home.addDevice(device.get());
        }

        // Ownership transfers only after registration succeeds.
        device.release();

        cout << (temporary
                     ? "Temporary device added.\n"
                     : "Permanent device added.\n");
    }
    catch (const exception &error)
    {
        cout << "Could not add device: "
             << error.what() << '\n';
    }
}

void showMenu()
{
    cout << "\n========== OOPS CP ==========\n";
    cout << "1. View all rooms and devices\n";
    cout << "2. View all device statuses\n";
    cout << "3. View one room\n";
    cout << "4. Turn ON a device\n";
    cout << "5. Turn OFF a device\n";
    cout << "6. Add room\n";
    cout << "7. Add permanent device\n";
    cout << "8. Add temporary device\n";
    cout << "9. Remove device manually\n";
    cout << "10. Exit\n";
    cout << "Choice: ";
}

int main()
{
    try
    {
        SmartHome home("My Smart Home");

        if (home.getDeviceCount() == 0)
        {
            seedHome(home);
        }

        cout << "\nWelcome to OOPS CP!\n";
        cout << "Rooms: " << home.getRoomCount() << '\n';
        cout << "Devices: " << home.getDeviceCount() << '\n';

        int choice = 0;

        do
        {
            showMenu();

            if (!(cin >> choice))
            {
                cin.clear();
                clearInput();
                cout << "Enter a valid number.\n";
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
                    home.showRoomDevices(readLine("Room name: "));
                    break;

                case 4:
                    home.turnOn(readLine("Device ID to turn ON: "));
                    cout << "Device turned ON.\n";
                    break;

                case 5:
                    home.turnOff(readLine("Device ID to turn OFF: "));
                    cout << "Device turned OFF.\n";
                    break;

                case 6:
                    addRoomFromMenu(home);
                    break;

                case 7:
                    addDeviceFromMenu(home, false);
                    break;

                case 8:
                    addDeviceFromMenu(home, true);
                    break;

                case 9:
                {
                    string id = readLine("Device ID to remove: ");

                    if (home.removeDevice(id))
                    {
                        cout << "Device removed successfully.\n";
                    }
                    else
                    {
                        cout << "Device not found.\n";
                    }

                    break;
                }

                case 10:
                    cout << "Shutting down OOPS CP...\n";
                    break;

                default:
                    cout << "Invalid choice.\n";
                }
            }
            catch (const exception &error)
            {
                cout << "Operation failed: "
                     << error.what() << '\n';
            }

        } while (choice != 10);
    }
    catch (const exception &error)
    {
        cerr << "Startup failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}