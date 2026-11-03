#pragma once
#include <QString>

inline constexpr auto Lg40U990AInputProfile = "lg-40u990a-w";

// Match reported hardware model names, never a user-editable saved monitor name.
// Only this allowlisted identifier may enter diagnostics; raw model text stays private.
inline QString KnownMonitorInputProfile(QString model)
{
    model = model.trimmed().toUpper();
    for (const auto *prefix : {"LG ULTRAFINE ", "LG "})
        if (model.startsWith(QLatin1String(prefix)))
            model.remove(0, int(qstrlen(prefix)));
    if (model == "40U990A" || model == "40U990A-W")
        return QLatin1String(Lg40U990AInputProfile);
    return {};
}
