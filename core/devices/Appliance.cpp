#include "Appliance.h"
#include <iostream>

Appliance::Appliance(
    const std::string& id,
    const std::string& name,
    const std::string& room,
    int powerWatts
) {
    this->id = id;
    this->name = name;
    this->room = room;

    state.status = DeviceStatus::OFF;
    state.powerWatts = powerWatts;
    state.setting = 0;
    state.runtimeMinutes = 0;
}

Appliance::~Appliance() {}

void Appliance::turnOn() {
    state.status = DeviceStatus::ON;
}

void Appliance::turnOff() {
    state.status = DeviceStatus::OFF;
}

void Appliance::showStatus() const {
    std::cout << "\nDevice ID: " << id;
    std::cout << "\nName: " << name;
    std::cout << "\nRoom: " << room;

    std::cout << "\nStatus: ";

    if (state.status == DeviceStatus::ON)
        std::cout << "ON";
    else
        std::cout << "OFF";

    std::cout << "\nPower: " << state.powerWatts << " W";
    std::cout << "\nRuntime: " << state.runtimeMinutes << " minutes\n";
}

const std::string& Appliance::getId() const {
    return id;
}

const std::string& Appliance::getName() const {
    return name;
}

const std::string& Appliance::getRoom() const {
    return room;
}

DeviceStatus Appliance::getStatus() const {
    return state.status;
}

int Appliance::getPowerWatts() const {
    return state.powerWatts;
}

DeviceState Appliance::getState() const {
    return state;
}