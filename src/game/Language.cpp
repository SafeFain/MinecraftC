#include "game/Language.h"

Language parseLanguage(std::string_view code) {
    return code == "zh_cn" ? Language::SimplifiedChinese : Language::English;
}
