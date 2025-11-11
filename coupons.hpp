#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <chrono>

struct sqlite3;

namespace Coupons
{
    struct Coupon 
    {
        int coupon_id;
        int customer_id;
        std::optional<int> card_id;
        std::string card_number;
        std::string valid_from;
        std::string valid_to;
        std::string traffic_area_group;
    };

    class CouponManager
    {
    public:
        explicit CouponManager(sqlite3* db);

        [[nodiscard]] bool fetch_and_store(std::string_view endpoint);
        [[nodiscard]] int parse_and_insert(std::string_view json_content);
        [[nodiscard]] bool is_valid_card(std::string_view card_number) const;
        [[nodiscard]] std::vector<Coupon> get_coupons_by_card(std::string_view card_number) const;

    private:
        sqlite3* db_;
        [[nodiscard]] static std::optional<std::chrono::system_clock::time_point> parse_iso8601(std::string_view datetime_str);
        [[nodiscard]] bool insert_coupon(const Coupon& coupon);
    };

}