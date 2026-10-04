#pragma once
#include "game/Command.h"
#include <fstream>
#include <sstream>
#include <set>
#include <stdexcept>

inline void verifyGameRuleBaseline(const char* path) {
    auto check = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
    std::ifstream input(path);
    check(input.good(), "official baseline fixture missing");
    std::set<std::string> names;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        std::string name, type;
        int64_t initial, minimum, maximum;
        check(static_cast<bool>(row >> name >> type >> initial >> minimum >> maximum), "bad baseline row");
        check(names.insert(name).second, "duplicate baseline rule");
        const auto ref = findGameRule(name);
        check(ref.has_value(), "missing official rule");
        const auto& definition = gameRuleDefinition(ref->id);
        check(definition.defaultValue == initial && definition.minimum == minimum && definition.maximum == maximum &&
            definition.type == (type == "bool" ? GameRuleType::Boolean : GameRuleType::Integer), "official type/default/range differs");
        check(parseCommand("/gamerule " + name).command.has_value(), "rule query missing");
        const auto value = gameRuleValueText({definition.type, initial});
        for (const auto spelling : {definition.name, definition.fullName, definition.legacyName}) {
            if (spelling.empty()) continue;
            const auto alias = findGameRule(spelling);
            check(alias && alias->id == ref->id, "alias maps to different rule");
            const auto aliasValue = gameRuleValueText(gameRuleQueryValue(*alias, {definition.type,initial}));
            const auto parsed = parseCommand("/gamerule " + std::string(spelling) + " " + aliasValue);
            check(parsed.command && parsed.command->gameRuleValue && parsed.command->gameRuleValue->number == initial,
                "alias round trip differs");
        }
        check(parseCommand("/gamerule " + name + " " + value + " extra").error.has_value(), "extra argument accepted");
        if (type == "bool") {
            for (const char* wrong : {"1", "0", "True", "FALSE", "yes"})
                check(parseCommand("/gamerule " + name + " " + wrong).error.has_value(), "bad bool accepted");
        } else {
            for (const auto boundary : {minimum, maximum})
                check(parseCommand("/gamerule " + name + " " + std::to_string(boundary)).command.has_value(), "integer boundary rejected");
            for (const auto boundary : {minimum-1, maximum+1})
                check(parseCommand("/gamerule " + name + " " + std::to_string(boundary)).error.has_value(), "out of range integer accepted");
            for (const char* wrong : {"1.5", "+1", "NaN", "999999999999999999999999999999"})
                check(parseCommand("/gamerule " + name + " " + wrong).error.has_value(), "bad integer accepted");
        }
    }
    check(names.size() == 59 && GAME_RULES.size() == names.size()+1, "incomplete baseline or unexpected rule");
    check(parseCommand("/gamerule DayNightDuration").command.has_value(), "custom duration query missing");
    check(parseCommand("/gamerule doFireTick false").command->gameRuleValue->number == 0, "legacy fire disable differs");
    check(parseCommand("/gamerule doFireTick true").command->gameRuleValue->number == 128, "legacy fire enable differs");
    check(parseCommand("/gamerule allowFireTicksAwayFromPlayer true").command->gameRuleValue->number == -1, "legacy unlimited fire differs");
    check(parseCommand("/gamerule allowFireTicksAwayFromPlayer false").command->gameRuleValue->number == 128, "legacy local fire differs");
    check(parseCommand("/gamerule disableRaids true").command->gameRuleValue->number == 0, "inverted rule not converted");
    const auto wrongName = parseCommand("/gamerule KEEP_INVENTORY true");
    check(wrongName.error && wrongName.error->position == 10, "wrong name diagnostic position");
    const auto wrongValue = parseCommand("/gamerule keep_inventory 1");
    check(wrongValue.error && wrongValue.error->position == 25, "wrong value diagnostic position");
    const std::string prefix = "/gamerule ";
    check(commandSuggestions(prefix, prefix.size()).size() == 60, "bare completion must list standard names once");
    check(commandSuggestions("/gamerule keepI", 15)[0].text == "keepInventory", "old name completion missing");
    const auto namespaced = commandSuggestions("/gamerule minecraft:keep", 24);
    check(namespaced.size() == 1 && namespaced[0].text == "minecraft:keep_inventory", "namespaced completion missing");
    const auto middle = commandSuggestions("/gamerule keep_inventory tr extra", 27);
    check(middle.size() == 1 && middle[0].text == "true" && middle[0].start == 25 && middle[0].end == 27,
        "bool cursor replacement lost later arguments");
    check(parseCommand("/help gamerule").command->gameRuleHelp, "rule-list help missing");
    check(parseCommand("/help gamerule keepInventory").command->gameRuleHelpSingle, "single-rule help missing");
    check(parseCommand("/help gamerule nope").error.has_value(), "unknown help rule accepted");
}
