#ifndef AC_H
#define AC_H

#include "Appliance.h"

class AC : public Appliance {
private:
    int temperature;

public:
    AC(
        const std::string& id,
        const std::string& name,
        const std::string& room,
        int powerWatts = 1500
    );

    void setTemperature(int temperature);
    int getTemperature() const;

    void showStatus() const override;
};

#endif