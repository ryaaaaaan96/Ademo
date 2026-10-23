#include "parameter_read_scope.h"
#include <algorithm>
#include <tuple>

namespace debugger {
QList<QJsonObject> parameterReadRanges(QList<ParameterAddress> parameters)
{
    std::sort(parameters.begin(), parameters.end(), [](const auto &a, const auto &b) {
        return std::tie(a.unit, a.area, a.address) < std::tie(b.unit, b.area, b.address);
    });
    QList<QJsonObject> ranges;
    for (const auto &parameter : parameters) {
        if (!ranges.isEmpty()) {
            auto &last = ranges.last();
            if (last["unit"].toInt() == parameter.unit && last["area"].toString() == parameter.area) {
                const int start = last["start"].toInt(), count = last["count"].toInt();
                if (parameter.address < start + count) continue;
                const int maximum = parameter.area == "coils" || parameter.area == "discrete" ? 2000 : 125;
                if (parameter.address == start + count && count < maximum) { last["count"] = count + 1; continue; }
            }
        }
        ranges.append({{"unit", parameter.unit}, {"area", parameter.area},
            {"action", "read-" + parameter.area}, {"start", parameter.address}, {"count", 1}});
    }
    return ranges;
}
}
