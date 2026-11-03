#include "monitor_identity.h"
#include "input_models.h"

QString MonitorIdentity(const QByteArray &edid)
{
    if (edid.size() < 128 || edid.left(8) != QByteArray::fromHex("00ffffffffffff00"))
        return {};
    unsigned int sum = 0;
    for (int i = 0; i < 128; ++i)
        sum += static_cast<unsigned char>(edid[i]);
    if ((sum & 0xff) != 0)
        return {};
    return QString::fromLatin1(QCryptographicHash::hash(edid.left(128), QCryptographicHash::Sha256).toHex());
}
QString MonitorNameFromEdid(const QByteArray &edid)
{
    if (MonitorIdentity(edid).isEmpty())
        return {};
    for (int offset = 54; offset <= 108; offset += 18)
        if (edid.mid(offset, 5) == QByteArray::fromHex("000000fc00"))
            return QString::fromLatin1(edid.mid(offset + 5, 13).split('\n').front().split('\0').front()).trimmed();
    return {};
}
QJsonObject MonitorEdidHardware(const QByteArray &edid)
{
    if (MonitorIdentity(edid).isEmpty())
        return {{"edid_valid", false}};
    const auto byte = [&](int offset) { return static_cast<unsigned char>(edid[offset]); };
    const unsigned int manufacturer = (byte(8) << 8) | byte(9);
    QString name;
    for (int shift : {10, 5, 0})
    {
        const auto letter = (manufacturer >> shift) & 31;
        if (letter < 1 || letter > 26)
            return {{"edid_valid", true}};
        name += QChar('A' + letter - 1);
    }
    const int product = byte(10) | (byte(11) << 8);
    QJsonObject result{{"edid_valid", true}, {"manufacturer", name}, {"product_code", product}};
    if (name == "GSM" || name == "LGD")
    {
        // 40U990A-W reports the generic EDID name LG ULTRAFINE. Its public
        // hardware dump identifies GSM / 0x9EBA; never match all UltraFine panels.
        const auto model = KnownMonitorInputProfile(MonitorNameFromEdid(edid));
        if ((name == "GSM" && product == 0x9eba) || !model.isEmpty())
        {
            result["input_profile"] = Lg40U990AInputProfile;
            result["profile_match"] = name == "GSM" && product == 0x9eba ? "edid-product" : "edid-model";
        }
    }
    return result;
}
QString MonitorInputProfileFromEdid(const QByteArray &edid)
{
    return MonitorEdidHardware(edid)["input_profile"].toString();
}
QJsonObject SafeMonitorHardware(const QJsonObject &hardware)
{
    QJsonObject result{{"edid_valid", hardware["edid_valid"].toBool()}};
    const auto manufacturer = hardware["manufacturer"].toString();
    if (QRegularExpression("^[A-Z]{3}$").match(manufacturer).hasMatch())
        result["manufacturer"] = manufacturer;
    const auto product = hardware["product_code"].toDouble(-1);
    if (product >= 0 && product <= 65535 && product == int(product))
        result["product_code"] = QString("0x%1").arg(int(product), 4, 16, QChar('0'));
    if (hardware["input_profile"] == Lg40U990AInputProfile)
        result["input_profile"] = Lg40U990AInputProfile;
    const auto match = hardware["profile_match"].toString();
    if (QStringList{"edid-product", "edid-model"}.contains(match))
        result["profile_match"] = match;
    const auto adapter = hardware["adapter_kind"].toString();
    if (QStringList{"amd", "displaylink", "other"}.contains(adapter))
        result["adapter_kind"] = adapter;
    return result;
}
