#include "Heater.h"
#include <iostream>
#include <stdexcept>

Heater::Heater(
    const std::string& id,
    const std::string& name,
    const std::string& room,
    int powerWatts
)
    : Appliance(id, name, room, powerWatts) {

    temperature = 25;
    state.setting = temperature;
}

void Heater::setTemperature(int temperature) {
    if (temperature < 20 || temperature > 45) {
        throw std::invalid_argument(
            "Heater temperature must be between 20 and 45 degrees Celsius."
        );
    }

    this->temperature = temperature;
    state.setting = temperature;
}

int Heater::getTemperature() const {
    return temperature;
}

void Heater::showStatus() const {
    std::cout << "\n[HEATER]";
    Appliance::showStatus();
    std::cout << "Temperature: "
              << temperature
              << " C\n";
}