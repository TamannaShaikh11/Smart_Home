#include "core/storage/Database.h"

#include <iostream>
#include <exception>

int main()
{
    try
    {
        Database database("smarthome_test.db");

        if (!database.initializeSchema())
        {
            std::cerr << "Table initialization failed.\n";
            return 1;
        }

        if (!database.saveRoom("Test Room"))
        {
            std::cerr << "Room save failed.\n";
            return 1;
        }

        DeviceRecord device;
        device.id = "TEST_LIGHT_01";
        device.name = "Test Light";
        device.type = "Light";
        device.roomName = "Test Room";
        device.status = 1;
        device.powerWatts = 60;
        device.setting = 0;
        device.runtimeMinutes = 0;
        device.lifetime = 0;
        device.expiryTime = 0;

        if (!database.saveDevice(device))
        {
            std::cerr << "Device save failed.\n";
            return 1;
        }

        std::cout << "\nSaved devices:\n";

        std::vector<DeviceRecord> devices =
            database.loadDevices();

        for (const DeviceRecord& item : devices)
        {
            std::cout << "ID: " << item.id << '\n';
            std::cout << "Name: " << item.name << '\n';
            std::cout << "Type: " << item.type << '\n';
            std::cout << "Room: " << item.roomName << '\n';
            std::cout << "Status: "
                      << (item.status == 1 ? "ON" : "OFF")
                      << '\n';
            std::cout << "Power: "
                      << item.powerWatts << " W\n";
            std::cout << "----------------------\n";
        }

        std::cout << "\nDatabase save/load test completed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
