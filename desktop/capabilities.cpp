#include "diagnostics.h"
#include <algorithm>

namespace
{
constexpr int MaximumCapabilitiesBytes = 65536;
bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool Identifier(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}
void SkipSpace(const QByteArray &text, int &position)
{
    while (position < text.size() && Space(text[position])) ++position;
}
int ClosingParen(const QByteArray &text, int open)
{
    int depth = 0;
    for (int i = open; i < text.size(); ++i)
    {
        if (text[i] == '(' && ++depth > 16) return -1;
        if (text[i] == ')' && --depth == 0) return i;
    }
    return -1;
}
QString Hex(unsigned int value) { return QString("0x%1").arg(value, 2, 16, QChar('0')); }
bool ReadHex(const QByteArray &text, int &position, int digits, unsigned int &value)
{
    const int start = position;
    while (position < text.size() && !Space(text[position]) && text[position] != '(' && text[position] != ')')
        ++position;
    auto token = text.mid(start, position - start);
    if (token.startsWith("0x") || token.startsWith("0X")) token.remove(0, 2);
    if (token.isEmpty() || token.size() > digits) return false;
    for (char c : token)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    bool ok = false;
    value = token.toUInt(&ok, 16);
    return ok;
}
QString StandardInputName(unsigned int value)
{
    switch (value)
    {
    case 0x01: return "VGA 1";
    case 0x03: return "DVI 1";
    case 0x04: return "DVI 2";
    case 0x0f: return "DisplayPort 1";
    case 0x10: return "DisplayPort 2";
    case 0x11: return "HDMI 1";
    case 0x12: return "HDMI 2";
    default: return {};
    }
}
QJsonObject Invalid(const QString &reason) { return {{"parse_status", reason}}; }
}

QJsonObject ParseDiagnosticCapabilities(const QByteArray &response)
{
    if (response.size() > MaximumCapabilitiesBytes) return Invalid("response too large");
    auto text = response.trimmed();
    const QByteArray prefix("Unparsed capabilities string:");
    if (text.startsWith(prefix)) text = text.mid(prefix.size()).trimmed();
    if (text.isEmpty()) return Invalid("empty response");
    if (text.contains('\0')) return Invalid("malformed response");
    if (text.startsWith('('))
    {
        if (ClosingParen(text, 0) != text.size() - 1) return Invalid("malformed response");
        text = text.mid(1, text.size() - 2);
    }
    QByteArray vcp;
    bool hasVcp = false;
    QString version;
    // Walk only top-level fields. A string inside model()/serial() cannot inject
    // a fake vcp() section, and none of those free-text fields are exported.
    for (int position = 0; position < text.size();)
    {
        SkipSpace(text, position);
        if (position == text.size()) break;
        const int start = position;
        while (position < text.size() && Identifier(text[position])) ++position;
        if (position == start) return Invalid("malformed response");
        const auto key = text.mid(start, position - start).toLower();
        SkipSpace(text, position);
        if (position >= text.size() || text[position] != '(') return Invalid("malformed response");
        const int close = ClosingParen(text, position);
        if (close < 0) return Invalid("malformed response");
        const auto value = text.mid(position + 1, close - position - 1);
        if (key == "vcp")
        {
            if (hasVcp) return Invalid("duplicate vcp sections");
            hasVcp = true;
            vcp = value;
        }
        if (key == "mccs_ver" && QRegularExpression("^[0-9]{1,2}\\.[0-9]{1,2}$")
                .match(QString::fromLatin1(value.trimmed())).hasMatch())
            version = QString::fromLatin1(value.trimmed());
        position = close + 1;
    }
    if (!hasVcp) return Invalid("vcp section not advertised");
    QSet<unsigned int> features;
    QMap<unsigned int, QSet<unsigned int>> inputValues;
    for (int position = 0; position < vcp.size();)
    {
        SkipSpace(vcp, position);
        if (position == vcp.size()) break;
        unsigned int code;
        if (!ReadHex(vcp, position, 2, code)) return Invalid("malformed vcp section");
        features.insert(code);
        SkipSpace(vcp, position);
        if (position < vcp.size() && vcp[position] == '(')
        {
            ++position;
            const bool input = code == 0x60 || code == 0xf4;
            if (input && !inputValues.contains(code)) inputValues.insert(code, {});
            int values = 0;
            while (true)
            {
                SkipSpace(vcp, position);
                if (position == vcp.size()) return Invalid("malformed vcp section");
                if (vcp[position] == ')') { ++position; break; }
                unsigned int value;
                if (!ReadHex(vcp, position, 4, value)) return Invalid("malformed vcp section");
                if (++values > 4096) return Invalid("vcp value list too large");
                if (input)
                {
                    inputValues[code].insert(value);
                    if (inputValues[code].size() > 256) return Invalid("input value list too large");
                }
            }
        }
    }
    auto codes = features.values();
    std::sort(codes.begin(), codes.end());
    QJsonArray advertised;
    for (auto code : codes) advertised.append(Hex(code));
    QJsonObject inputs;
    for (unsigned int code : {0x60u, 0xf4u})
    {
        QJsonArray values;
        auto ordered = inputValues.value(code).values();
        std::sort(ordered.begin(), ordered.end());
        for (auto value : ordered)
        {
            QJsonObject item{{"code", Hex(value)}};
            const auto name = code == 0x60 ? StandardInputName(value) : QString{};
            if (!name.isEmpty()) item["standard_name"] = name;
            values.append(item);
        }
        inputs[Hex(code)] = QJsonObject{{"control_advertised", features.contains(code)},
            {"values_listed", inputValues.contains(code)}, {"values", values}};
    }
    QJsonObject result{{"parse_status", "parsed"}, {"advertised_vcp_codes", advertised},
        {"advertised_input_values", inputs},
        {"interpretation", "Advertised capabilities can be incomplete or inaccurate. Missing controls/values "
                           "do not prove lack of support. No listed value is automatically written or applied."}};
    if (!version.isEmpty()) result["mccs_version"] = version;
    return result;
}
