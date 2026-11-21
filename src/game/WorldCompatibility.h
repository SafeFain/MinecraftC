#pragma once

#include <string>
#include <tuple>
#include <vector>

struct PluginCompatibilityIssue {
    std::string kind, key, saved, current;
    bool operator<(const PluginCompatibilityIssue& other) const {
        return std::tie(key,kind,saved,current)<std::tie(other.key,other.kind,other.saved,other.current);
    }
    bool operator==(const PluginCompatibilityIssue& other) const {
        return kind==other.kind && key==other.key && saved==other.saved && current==other.current;
    }
};
enum class WorldCompatibilityState { Compatible, PluginMismatch, Blocked };
struct WorldCompatibilityReport {
    std::vector<PluginCompatibilityIssue> issues;
    std::string blocker, token;
    WorldCompatibilityState state() const {
        return !blocker.empty()?WorldCompatibilityState::Blocked:issues.empty()?WorldCompatibilityState::Compatible:WorldCompatibilityState::PluginMismatch;
    }
    bool compatible() const { return state()==WorldCompatibilityState::Compatible; }
    bool recoverable() const { return state()==WorldCompatibilityState::PluginMismatch; }
};
