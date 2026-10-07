#include "Light.h"
#include <iostream>

Light::Light(
    const std::string& id,
    const std::string& name,
    const std::string& room,
    int powerWatts
)
    : Appliance(id, name, room, powerWatts) {
}

void Light::showStatus() const {
    std::cout << "\n[LIGHT]";
    Appliance::showStatus();
}