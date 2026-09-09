// Broker-resolved city names arrive in English (best-effort GeoIP); this
// table translates the ones seen in practice into Simplified Chinese for the
// 国家+地区+编号 row titles. Cities not listed here fall back to the raw
// broker string, so the table never blocks an unfamiliar location.
namespace OpenRung.WinUI.Models;

internal static class CityNames
{
    public static readonly Dictionary<string, string> Map =
        new(StringComparer.OrdinalIgnoreCase)
        {
            ["Tokyo"] = "东京",
            ["Osaka"] = "大阪",
            ["Seoul"] = "首尔",
            ["Incheon"] = "仁川",
            ["Busan"] = "釜山",
            ["Singapore"] = "新加坡",
            ["Hong Kong"] = "香港",
            ["Taipei"] = "台北",
            ["Shanghai"] = "上海",
            ["Beijing"] = "北京",
            ["Guangzhou"] = "广州",
            ["Shenzhen"] = "深圳",
            ["Bangkok"] = "曼谷",
            ["Kuala Lumpur"] = "吉隆坡",
            ["Jakarta"] = "雅加达",
            ["Manila"] = "马尼拉",
            ["Hanoi"] = "河内",
            ["Ho Chi Minh City"] = "胡志明市",
            ["Mumbai"] = "孟买",
            ["New Delhi"] = "新德里",
            ["Frankfurt"] = "法兰克福",
            ["Nuremberg"] = "纽伦堡",
            ["Berlin"] = "柏林",
            ["Amsterdam"] = "阿姆斯特丹",
            ["London"] = "伦敦",
            ["Paris"] = "巴黎",
            ["Zurich"] = "苏黎世",
            ["Vienna"] = "维也纳",
            ["Madrid"] = "马德里",
            ["Barcelona"] = "巴塞罗那",
            ["Milan"] = "米兰",
            ["Rome"] = "罗马",
            ["Stockholm"] = "斯德哥尔摩",
            ["Warsaw"] = "华沙",
            ["Dublin"] = "都柏林",
            ["Istanbul"] = "伊斯坦布尔",
            ["Moscow"] = "莫斯科",
            ["Dubai"] = "迪拜",
            ["Helsinki"] = "赫尔辛基",
            ["Johannesburg"] = "约翰内斯堡",
            ["Sao Paulo"] = "圣保罗",
            ["Santiago"] = "圣地亚哥",
            ["Mexico City"] = "墨西哥城",
            ["Sydney"] = "悉尼",
            ["Melbourne"] = "墨尔本",
            ["Auckland"] = "奥克兰",
            ["New York"] = "纽约",
            ["Los Angeles"] = "洛杉矶",
            ["San Jose"] = "圣何塞",
            ["Seattle"] = "西雅图",
            ["Dallas"] = "达拉斯",
            ["Chicago"] = "芝加哥",
            ["Atlanta"] = "亚特兰大",
            ["Miami"] = "迈阿密",
            ["Phoenix"] = "菲尼克斯",
            ["Vancouver"] = "温哥华",
            ["Toronto"] = "多伦多",
            ["Montreal"] = "蒙特利尔",
        };

    /// <summary>Chinese name for the broker city, or the raw string when unmapped.</summary>
    public static string Localize(string? city)
        => string.IsNullOrWhiteSpace(city) ? ""
           : Map.TryGetValue(city.Trim(), out var zh) ? zh : city.Trim();
}
