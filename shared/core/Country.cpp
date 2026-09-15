#include "Country.h"

#include <algorithm>

#include "StringUtil.h"

namespace tv {
namespace {

// Codes that appear in tvg-id but are not the ISO alpha-2 of the flag.
struct Alias {
    const char* from;
    const char* to;
};
const Alias kAliases[] = {
    {"uk", "gb"},  // United Kingdom
    {"el", "gr"},  // Greece
    {"su", "ru"},  // former Soviet feeds
};

struct CountryEntry {
    const char* code;  // lowercase alpha-2
    const char* name;
};

// ISO 3166-1 alpha-2, sorted by code so it can be searched with lower_bound.
const CountryEntry kCountries[] = {
    {"ad", "Andorra"},
    {"ae", "United Arab Emirates"},
    {"af", "Afghanistan"},
    {"ag", "Antigua and Barbuda"},
    {"ai", "Anguilla"},
    {"al", "Albania"},
    {"am", "Armenia"},
    {"ao", "Angola"},
    {"aq", "Antarctica"},
    {"ar", "Argentina"},
    {"as", "American Samoa"},
    {"at", "Austria"},
    {"au", "Australia"},
    {"aw", "Aruba"},
    {"ax", "Aland Islands"},
    {"az", "Azerbaijan"},
    {"ba", "Bosnia and Herzegovina"},
    {"bb", "Barbados"},
    {"bd", "Bangladesh"},
    {"be", "Belgium"},
    {"bf", "Burkina Faso"},
    {"bg", "Bulgaria"},
    {"bh", "Bahrain"},
    {"bi", "Burundi"},
    {"bj", "Benin"},
    {"bl", "Saint Barthelemy"},
    {"bm", "Bermuda"},
    {"bn", "Brunei"},
    {"bo", "Bolivia"},
    {"bq", "Caribbean Netherlands"},
    {"br", "Brazil"},
    {"bs", "Bahamas"},
    {"bt", "Bhutan"},
    {"bv", "Bouvet Island"},
    {"bw", "Botswana"},
    {"by", "Belarus"},
    {"bz", "Belize"},
    {"ca", "Canada"},
    {"cc", "Cocos Islands"},
    {"cd", "DR Congo"},
    {"cf", "Central African Republic"},
    {"cg", "Congo"},
    {"ch", "Switzerland"},
    {"ci", "Cote d'Ivoire"},
    {"ck", "Cook Islands"},
    {"cl", "Chile"},
    {"cm", "Cameroon"},
    {"cn", "China"},
    {"co", "Colombia"},
    {"cr", "Costa Rica"},
    {"cu", "Cuba"},
    {"cv", "Cape Verde"},
    {"cw", "Curacao"},
    {"cx", "Christmas Island"},
    {"cy", "Cyprus"},
    {"cz", "Czechia"},
    {"de", "Germany"},
    {"dj", "Djibouti"},
    {"dk", "Denmark"},
    {"dm", "Dominica"},
    {"do", "Dominican Republic"},
    {"dz", "Algeria"},
    {"ec", "Ecuador"},
    {"ee", "Estonia"},
    {"eg", "Egypt"},
    {"eh", "Western Sahara"},
    {"er", "Eritrea"},
    {"es", "Spain"},
    {"et", "Ethiopia"},
    {"fi", "Finland"},
    {"fj", "Fiji"},
    {"fk", "Falkland Islands"},
    {"fm", "Micronesia"},
    {"fo", "Faroe Islands"},
    {"fr", "France"},
    {"ga", "Gabon"},
    {"gb", "United Kingdom"},
    {"gd", "Grenada"},
    {"ge", "Georgia"},
    {"gf", "French Guiana"},
    {"gg", "Guernsey"},
    {"gh", "Ghana"},
    {"gi", "Gibraltar"},
    {"gl", "Greenland"},
    {"gm", "Gambia"},
    {"gn", "Guinea"},
    {"gp", "Guadeloupe"},
    {"gq", "Equatorial Guinea"},
    {"gr", "Greece"},
    {"gs", "South Georgia"},
    {"gt", "Guatemala"},
    {"gu", "Guam"},
    {"gw", "Guinea-Bissau"},
    {"gy", "Guyana"},
    {"hk", "Hong Kong"},
    {"hm", "Heard and McDonald Islands"},
    {"hn", "Honduras"},
    {"hr", "Croatia"},
    {"ht", "Haiti"},
    {"hu", "Hungary"},
    {"id", "Indonesia"},
    {"ie", "Ireland"},
    {"il", "Israel"},
    {"im", "Isle of Man"},
    {"in", "India"},
    {"io", "British Indian Ocean Territory"},
    {"iq", "Iraq"},
    {"ir", "Iran"},
    {"is", "Iceland"},
    {"it", "Italy"},
    {"je", "Jersey"},
    {"jm", "Jamaica"},
    {"jo", "Jordan"},
    {"jp", "Japan"},
    {"ke", "Kenya"},
    {"kg", "Kyrgyzstan"},
    {"kh", "Cambodia"},
    {"ki", "Kiribati"},
    {"km", "Comoros"},
    {"kn", "Saint Kitts and Nevis"},
    {"kp", "North Korea"},
    {"kr", "South Korea"},
    {"kw", "Kuwait"},
    {"ky", "Cayman Islands"},
    {"kz", "Kazakhstan"},
    {"la", "Laos"},
    {"lb", "Lebanon"},
    {"lc", "Saint Lucia"},
    {"li", "Liechtenstein"},
    {"lk", "Sri Lanka"},
    {"lr", "Liberia"},
    {"ls", "Lesotho"},
    {"lt", "Lithuania"},
    {"lu", "Luxembourg"},
    {"lv", "Latvia"},
    {"ly", "Libya"},
    {"ma", "Morocco"},
    {"mc", "Monaco"},
    {"md", "Moldova"},
    {"me", "Montenegro"},
    {"mf", "Saint Martin"},
    {"mg", "Madagascar"},
    {"mh", "Marshall Islands"},
    {"mk", "North Macedonia"},
    {"ml", "Mali"},
    {"mm", "Myanmar"},
    {"mn", "Mongolia"},
    {"mo", "Macao"},
    {"mp", "Northern Mariana Islands"},
    {"mq", "Martinique"},
    {"mr", "Mauritania"},
    {"ms", "Montserrat"},
    {"mt", "Malta"},
    {"mu", "Mauritius"},
    {"mv", "Maldives"},
    {"mw", "Malawi"},
    {"mx", "Mexico"},
    {"my", "Malaysia"},
    {"mz", "Mozambique"},
    {"na", "Namibia"},
    {"nc", "New Caledonia"},
    {"ne", "Niger"},
    {"nf", "Norfolk Island"},
    {"ng", "Nigeria"},
    {"ni", "Nicaragua"},
    {"nl", "Netherlands"},
    {"no", "Norway"},
    {"np", "Nepal"},
    {"nr", "Nauru"},
    {"nu", "Niue"},
    {"nz", "New Zealand"},
    {"om", "Oman"},
    {"pa", "Panama"},
    {"pe", "Peru"},
    {"pf", "French Polynesia"},
    {"pg", "Papua New Guinea"},
    {"ph", "Philippines"},
    {"pk", "Pakistan"},
    {"pl", "Poland"},
    {"pm", "Saint Pierre and Miquelon"},
    {"pn", "Pitcairn Islands"},
    {"pr", "Puerto Rico"},
    {"ps", "Palestine"},
    {"pt", "Portugal"},
    {"pw", "Palau"},
    {"py", "Paraguay"},
    {"qa", "Qatar"},
    {"re", "Reunion"},
    {"ro", "Romania"},
    {"rs", "Serbia"},
    {"ru", "Russia"},
    {"rw", "Rwanda"},
    {"sa", "Saudi Arabia"},
    {"sb", "Solomon Islands"},
    {"sc", "Seychelles"},
    {"sd", "Sudan"},
    {"se", "Sweden"},
    {"sg", "Singapore"},
    {"sh", "Saint Helena"},
    {"si", "Slovenia"},
    {"sj", "Svalbard and Jan Mayen"},
    {"sk", "Slovakia"},
    {"sl", "Sierra Leone"},
    {"sm", "San Marino"},
    {"sn", "Senegal"},
    {"so", "Somalia"},
    {"sr", "Suriname"},
    {"ss", "South Sudan"},
    {"st", "Sao Tome and Principe"},
    {"sv", "El Salvador"},
    {"sx", "Sint Maarten"},
    {"sy", "Syria"},
    {"sz", "Eswatini"},
    {"tc", "Turks and Caicos Islands"},
    {"td", "Chad"},
    {"tf", "French Southern Territories"},
    {"tg", "Togo"},
    {"th", "Thailand"},
    {"tj", "Tajikistan"},
    {"tk", "Tokelau"},
    {"tl", "Timor-Leste"},
    {"tm", "Turkmenistan"},
    {"tn", "Tunisia"},
    {"to", "Tonga"},
    {"tr", "Turkiye"},
    {"tt", "Trinidad and Tobago"},
    {"tv", "Tuvalu"},
    {"tw", "Taiwan"},
    {"tz", "Tanzania"},
    {"ua", "Ukraine"},
    {"ug", "Uganda"},
    {"um", "U.S. Minor Outlying Islands"},
    {"us", "United States"},
    {"uy", "Uruguay"},
    {"uz", "Uzbekistan"},
    {"va", "Vatican City"},
    {"vc", "Saint Vincent and the Grenadines"},
    {"ve", "Venezuela"},
    {"vg", "British Virgin Islands"},
    {"vi", "U.S. Virgin Islands"},
    {"vn", "Vietnam"},
    {"vu", "Vanuatu"},
    {"wf", "Wallis and Futuna"},
    {"ws", "Samoa"},
    {"ye", "Yemen"},
    {"yt", "Mayotte"},
    {"za", "South Africa"},
    {"zm", "Zambia"},
    {"zw", "Zimbabwe"},
};

std::string canonicalCode(const std::string& code) {
    std::string lower = toLower(trim(code));
    for (const Alias& alias : kAliases)
        if (lower == alias.from) lower = alias.to;
    if (lower.size() != 2) return std::string();
    if (lower[0] < 'a' || lower[0] > 'z' || lower[1] < 'a' || lower[1] > 'z')
        return std::string();
    return lower;
}

// Appends U+1F1E6 + offset as UTF-8 (regional indicator symbol letter).
void appendRegionalIndicator(std::string& out, char letter) {
    const unsigned int point = 0x1F1E6u + static_cast<unsigned int>(letter - 'a');
    out.push_back(static_cast<char>(0xF0 | (point >> 18)));
    out.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (point & 0x3F)));
}

}  // namespace

std::string countryFlagEmoji(const std::string& code) {
    const std::string lower = canonicalCode(code);
    if (lower.empty()) return std::string();

    std::string out;
    out.reserve(8);
    appendRegionalIndicator(out, lower[0]);
    appendRegionalIndicator(out, lower[1]);
    return out;
}

std::string countryName(const std::string& code) {
    const std::string lower = canonicalCode(code);
    if (!lower.empty()) {
        const CountryEntry* end = kCountries + sizeof(kCountries) / sizeof(kCountries[0]);
        const CountryEntry* hit =
            std::lower_bound(kCountries, end, lower,
                             [](const CountryEntry& entry, const std::string& want) {
                                 return std::string(entry.code) < want;
                             });
        if (hit != end && lower == hit->code) return hit->name;
    }
    // Unknown or malformed: show whatever the playlist gave us, uppercased.
    std::string out = trim(code);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

}  // namespace tv
