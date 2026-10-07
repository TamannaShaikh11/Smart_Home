#ifndef HOMESTATE_H
#define HOMESTATE_H

#include <vector>
#include "../devices/Appliance.h"

class HomeState {
private:
    std::vector<Appliance*> devices;

public:
    void addDevice(Appliance* device) {
        devices.push_back(device);
    }

    const std::vector<Appliance*>& getDevices() const {
        return devices;
    }

    void showHomeState() const {
        std::cout << "\n================================";
        std::cout << "\n        CURRENT HOME STATE";
        std::cout << "\n================================\n";

        for (const auto& device : devices) {
            device->showStatus();
        }
    }
};

#endif