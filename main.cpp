#include <iostream>

#include "core/devices/Light.h"
#include "core/devices/Fan.h"
#include "core/devices/AC.h"
#include "core/devices/Heater.h"

#include "core/control/DeviceManager.h"

int main() {

    try {
        DeviceManager manager;

        // Create virtual smart-home devices
        manager.addDevice(
            new Light(
                "LIVING_LIGHT",
                "Living Room Light",
                "Living Room"
            )
        );

        manager.addDevice(
            new Fan(
                "LIVING_FAN",
                "Living Room Fan",
                "Living Room"
            )
        );

        manager.addDevice(
            new AC(
                "BEDROOM_AC",
                "Bedroom AC",
                "Bedroom"
            )
        );

        manager.addDevice(
            new Heater(
                "BEDROOM_HEATER",
                "Bedroom Heater",
                "Bedroom"
            )
        );

        std::cout << "====================================\n";
        std::cout << "          SmartHomeOS\n";
        std::cout << "====================================\n";

        std::cout << "\nDevices registered: "
                  << manager.getDeviceCount()
                  << "\n";

        // Test device control
        manager.turnOn("LIVING_LIGHT");
        manager.turnOn("LIVING_FAN");
        manager.turnOn("BEDROOM_AC");

        // Configure devices
        Fan* fan =
            dynamic_cast<Fan*>(
                manager.findDevice("LIVING_FAN")
            );

        if (fan != nullptr) {
            fan->setSpeed(3);
        }

        AC* ac =
            dynamic_cast<AC*>(
                manager.findDevice("BEDROOM_AC")
            );

        if (ac != nullptr) {
            ac->setTemperature(24);
        }

        // Display current state
        manager.showAllDevices();

    }
    catch (const std::exception& e) {

        std::cout << "\nERROR: "
                  << e.what()
                  << "\n";
    }

    return 0;
}