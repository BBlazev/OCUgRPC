#pragma once
#include <string_view>

namespace config
{
    inline constexpr std::string_view DB_PATH = "database.db";
    
    // Legacy REST API endpoints
    inline constexpr std::string_view API_BASE_URL = "192.168.0.101:11006/api/v1";
    inline constexpr std::string_view COUPON_ENDPOINT = "192.168.0.101:11006/api/v1/Coupon";
    inline constexpr std::string_view ARTICLES_ENDPOINT = "192.168.0.101:11006/api/v1/Article";
    
    inline constexpr std::string_view GRPC_TICKET_SERVER = "localhost:5109";
    
    inline constexpr int DEFAULT_TCP_PORT = 8888;
}