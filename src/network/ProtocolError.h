#pragma once

#include <stdexcept>

namespace Lan {
class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
} // namespace Lan
