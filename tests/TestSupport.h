#pragma once

#include <exception>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace TestSupport {
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto parent=std::filesystem::temp_directory_path();
        static std::atomic<unsigned> sequence{0};
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned attempt=0;attempt<64;++attempt) {
            const auto candidate=parent/("mc-"+std::to_string(stamp)+"-"+std::to_string(sequence++));
            std::error_code error;
            if(std::filesystem::create_directory(candidate,error)) {m_path=candidate;return;}
            if(error)throw std::filesystem::filesystem_error("create test directory",candidate,error);
        }
        throw std::runtime_error("could not reserve a unique test directory");
    }
    ~TemporaryDirectory() {std::error_code error;std::filesystem::remove_all(m_path,error);}
    TemporaryDirectory(const TemporaryDirectory&)=delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&)=delete;
    const std::filesystem::path& path() const {return m_path;}
private:
    std::filesystem::path m_path;
};

// Called from a main-level catch so failed fixture setup cannot abort before a
// diagnostic identifies the test and the filesystem operation/paths involved.
inline int reportUnhandledException(const char* test) {
    try {
        throw;
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "INFRASTRUCTURE [" << test << "]: " << error.what()
                  << " (" << error.code().category().name() << ':'
                  << error.code().value() << ")\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "FAILED [" << test << "]: unexpected exception: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "FAILED [" << test << "]: unknown exception\n";
    }
    return 1;
}
}
