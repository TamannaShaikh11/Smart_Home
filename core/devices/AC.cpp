#include "AC.h"
#include <iostream>
#include <stdexcept>

AC::AC(
    const std::string& id,
    const std::string& name,
    const std::string& room,
    int powerWatts
)
    : Appliance(id, name, room, powerWatts) {

    temperature = 24;
    state.setting = temperature;
}

void AC::setTemperature(int temperature) {
    if (temperature < 16 || temperature > 30) {
        throw std::invalid_argument(
            "AC temperature must be between 16 and 30 degrees Celsius."
        );
    }

    this->temperature = temperature;
    state.setting = temperature;
}

int AC::getTemperature() const {
    return temperature;
}

void AC::showStatus() const {
    std::cout << "\n[AIR CONDITIONER]";
    Appliance::showStatus();
    std::cout << "Temperature: "
              << temperature
              << " C\n";
}