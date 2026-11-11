#pragma once
#include "../core/activity_tracker.h"
#include <string>
namespace chronox {
class ActivityPreferences {
public:
    explicit ActivityPreferences(std::wstring key=L"Software\\Chronox\\Preferences");
    ActivitySettings load() const;
    void save(ActivitySettings settings) const;
private:
    std::wstring key_;
};
}
