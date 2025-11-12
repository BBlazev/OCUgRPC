#pragma once

#include "database.hpp"
#include "coupons.hpp"
#include "include/asio.hpp"
#include <memory>
#include <array>
#include <string>
#include <chrono>

using asio::ip::tcp;

class Session;

class Sender
{
public:
    explicit Sender(Database& db, int port = 8888);

    void run();
    void stop();

private:


    Database& db_;
    asio::io_context io_context_; 
    tcp::acceptor acceptor_;       
    std::atomic<bool> running_;

    void start_accept();
};

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(tcp::socket socket, Database& db);
    void start();
    void handle_fetch_articles();

private:
    std::chrono::steady_clock::time_point request_start_time;

    tcp::socket socket_;
    Database& db_;
    std::array<char, 1024> buffer_;
    
    void do_read();
    void do_write(std::string response);
    void process_request(std::string_view request);

    void handle_card_validation(std::string_view card_number);
    void handle_purchase(int article_id, std::string_view card_number, int quantity);
    void handle_QR(std::string token, int validator_id);
    [[nodiscard]] bool validate_QR(std::string token);
    [[nodiscard]] static std::optional<std::chrono::system_clock::time_point> parse_iso8601(std::string_view datetime_str);
    [[nodiscard]] bool handle_QR_activation(std::string token);

    [[nodiscard]] std::optional<int> find_coupon_by_card(std::string_view card_number);
    [[nodiscard]] bool log_purchase(int article_id, std::string_view card_number, int quantity, bool success);
};


