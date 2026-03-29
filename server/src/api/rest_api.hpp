// server/src/api/rest_api.hpp
#pragma once

#include <string>
#include <memory>

namespace novaMP {

class MasterServer;

class RestAPI {
public:
    RestAPI(MasterServer& master, const std::string& host, int port);
    ~RestAPI();

    void run();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace novaMP
