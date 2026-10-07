#include "Fan.h"
#include <iostream>
#include <stdexcept>

Fan::Fan(
    const std::string& id,
    const std::string& name,
    const std::string& room,
    int powerWatts
)
    : Appliance(id, name, room, powerWatts) {

    speed = 1;
    state.setting = speed;
}

void Fan::setSpeed(int speed) {
    if (speed < 1 || speed > 5) {
        throw std::invalid_argument(
            "Fan speed must be between 1 and 5."
        );
    }

    this->speed = speed;
    state.setting = speed;
}

int Fan::getSpeed() const {
    return speed;
}

void Fan::showStatus() const {
    std::cout << "\n[FAN]";
    Appliance::showStatus();
    std::cout << "Speed: " << speed << "/5\n";
}