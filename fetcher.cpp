#include "fetcher.hpp"
#include "include/httplib.h"
#include <cstdio>
#include <array>
#include <sstream>
#include <string>

std::optional<std::string> Fetcher::fetch_json(std::string_view url)
{
    std::string url_str(url);
    std::string host;
    std::string path = "/";
    
    size_t protocol_pos = url_str.find("://");
    if (protocol_pos != std::string::npos)
        url_str = url_str.substr(protocol_pos + 3);

    
    size_t path_pos = url_str.find('/');
    if (path_pos != std::string::npos) 
    {
        host = url_str.substr(0, path_pos);
        path = url_str.substr(path_pos);
    } 
    else host = url_str;
    
    
    httplib::Client cli(host);
    cli.set_connection_timeout(5, 0); 
    
    auto res = cli.Get(path);
    
    if (res && res->status == 200) 
        return res->body;
    
    
    return std::nullopt;


}