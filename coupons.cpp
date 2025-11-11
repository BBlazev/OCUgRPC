#include "coupons.hpp"
#include "fetcher.hpp"
#include "include/sqlite3.h"
#include "nlohmann/json.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

using json = nlohmann::json;


namespace Coupons
{
    CouponManager::CouponManager(sqlite3* db) : db_(db) {}

    bool CouponManager::fetch_and_store(std::string_view endpoint)
    {
        std::cout << "Fetching coupons from " << endpoint << '\n';

        auto json_content = Fetcher::fetch_json(endpoint);
        if(!json_content)
        {
            std::cerr << "Failed to fetch JSON from " << endpoint << '\n';
            return false;
        }

        int inserted = parse_and_insert(*json_content);
        if(!inserted)
        {  
            std::cerr << "Failed to parse and insert coupons\n";
            return false;
        }
        std::cout << "Successfully inserted " << inserted << " coupons\n";
        return inserted > 0;
        
    }

    int CouponManager::parse_and_insert(std::string_view json_content)
    {
        try
        {
            auto insert_start = std::chrono::steady_clock::now();

            auto json_array = json::parse(json_content);

            if(!json_array.is_array())
            {
                std::cerr << "Invalid JSON: expected array\n";
                return -1;
            }

            std::cout << "Found " << json_array.size() << " coupons\n";

            int inserted = 0;

            for(const auto& item : json_array)
            {
                Coupon coupon;
                
                coupon.coupon_id = item.contains("id") && item["id"].is_number() ? item["id"].get<int>() : 0;
                coupon.customer_id = item.contains("customerId") && item["customerId"].is_number() ? item["customerId"].get<int>() : 0;
                
                if(item.contains("cardId") && !item["cardId"].is_null() && item["cardId"].is_number())
                    coupon.card_id = item["cardId"].get<int>();
                
                coupon.card_number = item.contains("cardNumber") && item["cardNumber"].is_string() ? item["cardNumber"].get<std::string>() : "";
                coupon.valid_from = item.contains("validFrom") && item["validFrom"].is_string() ? item["validFrom"].get<std::string>() : "";
                coupon.valid_to = item.contains("validTo") && item["validTo"].is_string() ? item["validTo"].get<std::string>() : "";
                coupon.traffic_area_group = item.contains("trafficAreaGroup") && item["trafficAreaGroup"].is_string() ? item["trafficAreaGroup"].get<std::string>() : "";

                if(insert_coupon(coupon))
                    inserted++;
                auto insert_end = std::chrono::steady_clock::now();
                auto total = std::chrono::duration_cast<std::chrono::microseconds>(insert_end - insert_start).count();

                std::cout << "Total query: " << total << " μs\n"; 
           }
            std::cout << "Inserted " << inserted << " coupons\n";
            return inserted;
        }
        catch(const json::exception& e)
        {
            std::cerr << "JSON parse error: " << e.what() << '\n';
            return -1;
        }
    }

    bool CouponManager::insert_coupon(const Coupon& coupon)
    {
        
        const char* sql = 
                "INSERT INTO coupons (coupon_id, customer_id, card_id, card_number, "
                "valid_from, valid_to, traffic_area_group) "
                "VALUES (?, ?, ?, ?, ?, ?, ?);";
        
        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << '\n';
            return false;
        }

        sqlite3_bind_int(stmt, 1, coupon.coupon_id);
        sqlite3_bind_int(stmt, 2, coupon.customer_id);

        if(coupon.card_id) sqlite3_bind_int(stmt, 3, *coupon.card_id);
        else sqlite3_bind_null(stmt, 3);

        sqlite3_bind_text(stmt, 4, coupon.card_number.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, coupon.valid_from.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, coupon.valid_to.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, coupon.traffic_area_group.c_str(), -1, SQLITE_TRANSIENT);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);

        return success;
    }

    bool CouponManager::is_valid_card(std::string_view card_number) const
    {
        const char* sql =  "SELECT valid_from, valid_to FROM coupons WHERE card_number = ? LIMIT 1;";

        sqlite3_stmt* stmt;

        if(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            std::cerr << "Failed to prepare statement " << sqlite3_errmsg(db_) << '\n';
            return false;
        }

        sqlite3_bind_text(stmt, 1, std::string(card_number).c_str(), -1, SQLITE_TRANSIENT);

        bool is_valid = false;

        if(sqlite3_step(stmt) == SQLITE_ROW)
        {
           const char* valid_from_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
           const char* valid_to_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

           if(valid_from_str && valid_to_str)
           {
            // Copy strings to avoid dangling pointers
            std::string valid_from_copy(valid_from_str);
            std::string valid_to_copy(valid_to_str);

            auto time_from = parse_iso8601(valid_from_copy);
            auto time_to = parse_iso8601(valid_to_copy);
            auto now = std::chrono::system_clock::now();

            if(time_from && time_to)
            {
                is_valid = (now >= *time_from && now <= *time_to);
                if(!is_valid) std::cout << "Card expired or not yet valid for: " << card_number << '\n';
            }
           }

        }

        sqlite3_finalize(stmt);
        return is_valid;
    }


    std::vector<Coupon> CouponManager::get_coupons_by_card(std::string_view card_number) const
    {
        std::vector<Coupon> coupons;

        const char* sql = 
        "SELECT coupon_id, customer_id, card_id, card_number, valid_from, valid_to, traffic_area_group "
        "FROM coupons WHERE card_number = ?;";

        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            std::cerr << "Failed to prepare query: " << sqlite3_errmsg(db_) << '\n';
            return coupons;
        }

        sqlite3_bind_text(stmt, 1, std::string(card_number).c_str(), -1, SQLITE_TRANSIENT);

        while(sqlite3_step(stmt) == SQLITE_ROW)
        {
            Coupon coupon;
            coupon.coupon_id = sqlite3_column_int(stmt, 0);
            coupon.customer_id = sqlite3_column_int(stmt, 1);

            if(sqlite3_column_type(stmt,2) != SQLITE_NULL)
                coupon.card_id = sqlite3_column_int(stmt, 2);
            
            const char* card_num = reinterpret_cast<const char*>(sqlite3_column_text(stmt,3));
            const char* valid_from = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            const char* valid_to = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            const char* traffic_area = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            
            if (card_num) coupon.card_number = card_num;
            if (valid_from) coupon.valid_from = valid_from;
            if (valid_to) coupon.valid_to = valid_to;
            if (traffic_area) coupon.traffic_area_group = traffic_area;
            
            coupons.push_back(std::move(coupon));
        }
        
        sqlite3_finalize(stmt);
            return coupons;  
    }

    std::optional<std::chrono::system_clock::time_point>
    CouponManager::parse_iso8601(std::string_view datetime_str)
    {
        std::tm tm = {};
        
        // Manual parsing of ISO 8601 format: 2024-01-15T10:30:00
        int year, month, day, hour, min, sec;
        int parsed = std::sscanf(datetime_str.data(), "%d-%d-%dT%d:%d:%d",
                                &year, &month, &day, &hour, &min, &sec);
        
        if (parsed != 6) {
            return std::nullopt;
        }
        
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;
        tm.tm_isdst = -1;  
        
        std::time_t time = std::mktime(&tm);
        if (time == -1) {
            return std::nullopt;
        }
        
        return std::chrono::system_clock::from_time_t(time);

    }


}