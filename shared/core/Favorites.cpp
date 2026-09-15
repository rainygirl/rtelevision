#include "Favorites.h"

#include <sstream>
#include <utility>

#include "Paths.h"
#include "StringUtil.h"

namespace tv {

Favorites::Favorites(std::string dataDir) : path_(joinPath(dataDir, "favorites.txt")) {}

void Favorites::load() {
    urls_.clear();
    std::string text;
    if (!readFile(path_, text)) return;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty() && line[0] != '#') urls_.insert(line);
    }
}

bool Favorites::save() const {
    std::ostringstream out;
    for (const std::string& url : urls_) out << url << "\n";
    return writeFileAtomic(path_, out.str());
}

void Favorites::toggle(const std::string& url) {
    if (url.empty()) return;
    if (!urls_.erase(url)) urls_.insert(url);
    save();
}

}  // namespace tv
