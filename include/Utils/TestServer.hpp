#pragma once

#include <cstdint>

namespace TestServer {

struct Options {
    uint16_t port = 19550;
    bool allowWildcardCors = false;
};

bool IsCompiledIn();
bool Start(const Options& options = Options{});
void Stop();
bool IsRunning();

} // namespace TestServer
