#include "Light.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <stdexcept>

Light::Light(
    const std::string& id,
    const std::string& name,
    const std::string& room,
    int powerWatts,
    const std::string& lightType
)
    : Appliance(id, name, room, powerWatts),
      lightType(lightType),
      brightness(100),
      color("#FFFFFF"),
      colorTemperature(4000)
{
    if (lightType != "standard" &&
        lightType != "warm-white" &&
        lightType != "cool-white" &&
        lightType != "dimmable" &&
        lightType != "rgb" &&
        lightType != "tunable-white" &&
        lightType != "smart-bulb")
    {
        throw std::invalid_argument("Unsupported light type: " + lightType);
    }
}

bool Light::supportsBrightness() const
{
    return lightType == "dimmable" || lightType == "rgb" ||
           lightType == "tunable-white" || lightType == "smart-bulb";
}

bool Light::supportsColor() const
{
    return lightType == "rgb" || lightType == "smart-bulb";
}

bool Light::supportsColorTemperature() const
{
    return lightType == "tunable-white" || lightType == "smart-bulb";
}

void Light::setBrightness(int value)
{
    if (!supportsBrightness())
        throw std::invalid_argument("This light does not support brightness control.");
    if (value < 0 || value > 100)
        throw std::invalid_argument("Brightness must be between 0 and 100 percent.");
    brightness = value;
}

void Light::setColor(const std::string& value)
{
    if (!supportsColor())
        throw std::invalid_argument("This light does not support RGB color control.");
    if (value.size() != 7 || value[0] != '#')
        throw std::invalid_argument("Color must be a hexadecimal value such as #A1B2C3.");
    if (!std::all_of(value.begin() + 1, value.end(), [](unsigned char character)
        { return std::isxdigit(character) != 0; }))
        throw std::invalid_argument("Color must be a hexadecimal value such as #A1B2C3.");

    color = value;
    std::transform(color.begin(), color.end(), color.begin(),
                   [](unsigned char character)
                   { return static_cast<char>(std::toupper(character)); });
}

void Light::setColorTemperature(int kelvin)
{
    if (!supportsColorTemperature())
        throw std::invalid_argument(
            "This light does not support color-temperature control.");
    if (kelvin < 2700 || kelvin > 6500)
        throw std::invalid_argument(
            "Light color temperature must be between 2700 and 6500 K.");
    colorTemperature = kelvin;
}

int Light::getBrightness() const
{
    return brightness;
}

std::string Light::getColor() const
{
    return color;
}

int Light::getColorTemperature() const
{
    return colorTemperature;
}

std::string Light::getLightType() const
{
    return lightType;
}

bool Light::hasBrightnessControl() const
{
    return supportsBrightness();
}

bool Light::hasColorControl() const
{
    return supportsColor();
}

bool Light::hasColorTemperatureControl() const
{
    return supportsColorTemperature();
}

std::string Light::getType() const
{
    return "Light";
}

void Light::showStatus() const {
    std::cout << "\n[LIGHT]";
    Appliance::showStatus();
    std::cout << "Light type: " << lightType << '\n';
    if (supportsBrightness())
        std::cout << "Brightness: " << brightness << "%\n";
    if (supportsColor())
        std::cout << "Color: " << color << '\n';
    if (supportsColorTemperature())
        std::cout << "Color temperature: " << colorTemperature << " K\n";
}