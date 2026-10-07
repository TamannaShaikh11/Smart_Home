#include "DeviceManager.h"
#include <iostream>
#include <stdexcept>

DeviceManager::~DeviceManager() {
    for (Appliance* device : devices) {
        delete device;
    }

    devices.clear();
}

void DeviceManager::addDevice(Appliance* device) {
    if (device == nullptr) {
        throw std::invalid_argument(
            "Cannot add a null device."
        );
    }

    devices.push_back(device);
}

Appliance* DeviceManager::findDevice(const std::string& id) {
    for (Appliance* device : devices) {
        if (device->getId() == id) {
            return device;
        }
    }

    return nullptr;
}

void DeviceManager::turnOn(const std::string& id) {
    Appliance* device = findDevice(id);

    if (device == nullptr) {
        throw std::invalid_argument(
            "Device not found: " + id
        );
    }

    device->turnOn();
}

void DeviceManager::turnOff(const std::string& id) {
    Appliance* device = findDevice(id);

    if (device == nullptr) {
        throw std::invalid_argument(
            "Device not found: " + id
        );
    }

    device->turnOff();
}

void DeviceManager::showAllDevices() const {
    std::cout << "\n================================";
    std::cout << "\n       SMART HOME DEVICES";
    std::cout << "\n================================\n";

    for (const Appliance* device : devices) {
        device->showStatus();
    }
}

int DeviceManager::getDeviceCount() const {
    return static_cast<int>(devices.size());
}