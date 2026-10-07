#ifndef HEATER_H
#define HEATER_H

#include "Appliance.h"

class Heater : public Appliance {
private:
    int temperature;

public:
    Heater(
        const std::string& id,
        const std::string& name,
        const std::string& room,
        int powerWatts = 2000
    );

    void setTemperature(int temperature);
    int getTemperature() const;

    void showStatus() const override;
};

#endif