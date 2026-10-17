#ifndef LIGHT_H
#define LIGHT_H

#include "Appliance.h"
#include <string>

class Light : public Appliance {
private:
    std::string lightType;
    int brightness;
    std::string color;
    int colorTemperature;

    bool supportsBrightness() const;
    bool supportsColor() const;
    bool supportsColorTemperature() const;

public:
    Light(
        const std::string& id,
        const std::string& name,
        const std::string& room,
        int powerWatts = 60,
        const std::string& lightType = "standard"
    );

    void setBrightness(int value);
    void setColor(const std::string& value);
    void setColorTemperature(int kelvin);

    int getBrightness() const;
    std::string getColor() const;
    int getColorTemperature() const;
    std::string getLightType() const;
    bool hasBrightnessControl() const;
    bool hasColorControl() const;
    bool hasColorTemperatureControl() const;

    void showStatus() const override;
    std::string getType() const override;
};

#endif