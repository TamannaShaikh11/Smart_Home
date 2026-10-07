#ifndef DEVICESTATE_H
#define DEVICESTATE_H

#include <string>

enum class DeviceStatus {
    OFF,
    ON
};

struct DeviceState {
    DeviceStatus status = DeviceStatus::OFF;

    int powerWatts = 0;

    // Generic value for devices that have adjustable settings.
    // Example:
    // Fan  -> speed
    // AC   -> temperature
    // Heater -> temperature
    int setting = 0;

    int runtimeMinutes = 0;
};

#endif