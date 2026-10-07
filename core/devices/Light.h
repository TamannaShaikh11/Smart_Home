#ifndef LIGHT_H
#define LIGHT_H

#include "Appliance.h"

class Light : public Appliance {
public:
    Light(
        const std::string& id,
        const std::string& name,
        const std::string& room,
        int powerWatts = 60
    );

    void showStatus() const override;
};

#endif