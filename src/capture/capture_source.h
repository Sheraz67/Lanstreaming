#pragma once

#include "core/types.h"
#include <cstdint>
#include <optional>

namespace lancast {

class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;

    virtual bool init(uint32_t target_width, uint32_t target_height) = 0;
    virtual std::optional<RawVideoFrame> capture_frame() = 0;
    virtual void shutdown() = 0;

    virtual uint32_t native_width() const = 0;
    virtual uint32_t native_height() const = 0;
    virtual uint32_t target_width() const = 0;
    virtual uint32_t target_height() const = 0;
};

} // namespace lancast
