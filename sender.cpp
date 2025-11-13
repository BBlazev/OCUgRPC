#include "sender.hpp"
#include <iostream>
#include <algorithm>
#include <sstream>
#include "nlohmann/json.hpp"

using json = nlohmann::json;


Sender::Sender(Database& db, int port) 
    : db_(db)
    , io_context_()           
    , acceptor_(io_context_) 
    , running_(true)
{
    try {
        std::cout << "[DEBUG] Opening acceptor...\n";
        acceptor_.open(tcp::v4());
        
        std::cout << "[DEBUG] Setting socket options...\n";
        acceptor_.set_option(asio::socket_base::reuse_address(true));
        
        std::cout << "[DEBUG] Binding to port " << port << "...\n";
        acceptor_.bind(tcp::endpoint(tcp::v4(), port));
        
        std::cout << "[DEBUG] Starting to listen...\n";
        acceptor_.listen();
        
        std::cout << "Server listening on 0.0.0.0:" << port << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to start server: " << e.what() << std::endl;
        throw;
    }
}

void Sender::run()
{

    std::cout << "[DEBUG] Entering run() method...\n";
    std::cout << "[DEBUG] Starting accept...\n";
    start_accept();
    
    std::cout << "[DEBUG] Starting io_context.run()...\n";
    std::cout << "Server running... (Press Ctrl+C to stop)\n";
    
    io_context_.run();  
    
    std::cout << "[DEBUG] io_context.run() finished\n";
}

void Sender::stop()
{
    std::cout << "[Sender] Stopping TCP server...\n";
    running_ = false;
    
    if (acceptor_.is_open()) {
        asio::error_code ec;
        acceptor_.close(ec);
        if (ec) {
            std::cerr << "[Sender] Error closing acceptor: " << ec.message() << "\n";
        }
    }
    
    io_context_.stop();
    
    std::cout << "[Sender] TCP server stopped\n";
}

void Sender::start_accept()
{
    if (!running_) {
        return;
    }
    
    acceptor_.async_accept
    (
        [this](asio::error_code ec, tcp::socket socket)
        {
            if(!ec)
            {
                std::cout << "New client connected\n";
                std::make_shared<Session>(std::move(socket), db_)->start();
            }
            else if (ec != asio::error::operation_aborted) {
                std::cerr << "Accept error: " << ec.message() << std::endl;
            }
            
            if (running_) {
                start_accept();
            }
        }
    );
}

Session::Session(tcp::socket socket, Database& db) : socket_(std::move(socket)), db_(db) {}

void Session::start()
{
    do_read();
}

void Session::do_read()
{
    auto self = shared_from_this();

    socket_.async_read_some
    (
        asio::buffer(buffer_), [this,self](asio::error_code ec, std::size_t bytes_transferred)
        {
            if(!ec)
            {
                request_start_time = std::chrono::steady_clock::now();
                std::string request(buffer_.data(), bytes_transferred);

                request.erase(
                    std::remove_if(request.begin(), request.end(), [](char c) { return c == '\n' || c == '\r';}), request.end()
                );

                std::cout << "Received: " << request << "\n";
                process_request(request);
            }
            else if (ec != asio::error::eof)
                std::cerr << "Read error: " << ec.message() << "\n";
        }
    );
}

void Session::do_write(std::string response)
{
    auto self = shared_from_this();
    response += "\n";

    asio::async_write(
        socket_,
        asio::buffer(response),
        [this, self](asio::error_code ec, std::size_t) {
            if(!ec)
            {
                auto end_time = std::chrono::steady_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::microseconds>( end_time - request_start_time).count();

                std::cout << "Request latency: " << latency << "μs (" << (latency / 1000.0) << " ms)\n";
            }
            if (ec) 
                std::cerr << "Write error: " << ec.message() << "\n";
    
            socket_.close();
        }
    );
}

void Session::process_request(std::string_view request)
{
    std::string trimmed(request);
    
    trimmed.erase(
        std::remove_if(trimmed.begin(), trimmed.end(), 
            [](unsigned char c) { return c == '\r' || c == '\n'; }),
        trimmed.end()
    );
    
    auto start = trimmed.find_first_not_of(" \t");
    auto end = trimmed.find_last_not_of(" \t");
    
    if (start == std::string::npos) {
        std::cout << "Empty request\n";
        do_write("FAIL Empty request");
        return;
    }
    
    trimmed = trimmed.substr(start, end - start + 1);
    
    std::cout << "Received: \"" << trimmed << "\"\n";
    
    if (trimmed == "FETCH_ARTICLES") {
        std::cout << "Command: Fetch articles\n";
        handle_fetch_articles();
        return;
    }
    
    if (trimmed.starts_with("PURCHASE ")) {
        auto args = trimmed.substr(9);  
        
        std::istringstream iss(args);
        std::string article_id_str, card_number;
        int quantity;

        if(!(iss >> article_id_str >> card_number >> quantity))
        {
            std::cout << "Invalid PURCHASE format \n";
            do_write("FAIL Invalid format");
            return;
        }



        /*
        std::cout << "[DEBUG] Parsing args: \"" << args << "\"\n";
        
        auto space_pos = args.find(' ');
        
        if (space_pos == std::string::npos) {
            std::cout << "Invalid PURCHASE format: no space between article_id and card_number or quantity\n";
            std::cout << "   Expected: PURCHASE <article_id> <card_number> <quantity>\n";
            std::cout << "   Received: PURCHASE " << args << "\n";
            do_write("FAIL Invalid format");
            return;
        }
        
        std::string article_id_str = args.substr(0, space_pos);
        std::string card_number = args.substr(space_pos + 1);
        int quantity = std::stoi(args.substr(space_pos + 2));
        */
        
        auto card_start = card_number.find_first_not_of(" \t");
        if (card_start != std::string::npos) {
            card_number = card_number.substr(card_start);
        }
        
        std::cout << "[DEBUG] Article ID string: \"" << article_id_str << "\"\n";
        std::cout << "[DEBUG] Card number: \"" << card_number << "\"\n";
        std::cout << "[DEBUG] Quantity: \"" << quantity << "\"\n";
        
        try 
        {
            int article_id = std::stoi(article_id_str);
            std::cout << "Command: Purchase article " << article_id 
                     << " with card \"" << card_number << "\"\n";
            handle_purchase(article_id, card_number, quantity);
        } 
        catch (const std::exception& e) 
        {
            std::cout << "Invalid article_id: \"" << article_id_str << "\" (" << e.what() << ")\n";
            do_write("FAIL Invalid article_id");
        }
        return;
    }
    
    if(trimmed.starts_with("QR"))
    {
        // 1. get QR string only

        //KsF-Zet|
        //1488bf99-9b7d-4c25-b00b-22065f12649b|
        //638974309111354660|
        //e279acf941396b65aec32aeca1c0e4bf4b2a9cddc500f99677a9ff7f800ebe75
        auto args = trimmed.substr(2);
        std::istringstream iss(args);
        std::string token;
        std::string uuid;
        std::string timestamp;
        std::string hash;
        int validator_id = 1;

        if (std::getline(iss, uuid, '|') && 
            std::getline(iss, token, '|') && 
            std::getline(iss, timestamp, '|') && 
            std::getline(iss, hash)) 
        {
            
 
            std::cout << "Parsed QR: uuid" << uuid  
                    << ", token: "<< token 
                    << ", timestamp=" << timestamp
                    << ", hash=" << hash 
                    << ", validator_id=" << validator_id << "\n";
            
        } 
        else 
        {
            std::cout << "Invalid QR format \n";
            do_write("Invalid QR format");
            return;
        }

        try
        {
            std::cout << "Handling QR token \n";
            validate_QR(token);
            //handle_QR(token, validator_id);
        }
        catch(const std::exception& e)
        {
            std::cout << "Error in QR handling \n";
            do_write("Error in QR handling");
        }

        return;

    }


    bool is_card = !trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), 
                       [](char c) { return std::isdigit(c) || std::isalpha(c); });

    if (is_card) {
        std::cout << "Legacy: Validate card \"" << trimmed << "\"\n";
        handle_card_validation(trimmed);
        return;
    }
    
    std::cout << "Unknown command: \"" << trimmed << "\"\n";
    do_write("FAIL Unknown command");
}

std::optional<int> Session::find_coupon_by_card(std::string_view card_number) {
    Coupons::CouponManager manager(db_.get());
    
    if (!manager.is_valid_card(card_number)) 
        return std::nullopt;
       
    auto coupons = manager.get_coupons_by_card(card_number);
    
    if (coupons.empty()) 
        return std::nullopt;
    
    return coupons[0].coupon_id;
}

void Session::handle_fetch_articles()
{
    auto query_start = std::chrono::steady_clock::now();
    try
    {
        const char* sql = 
            "SELECT article_id, article_name, article_price "
            "FROM articles "
            "WHERE "
            "  article_name LIKE '%Dnevna karta%' OR "
            "  article_name LIKE '%Pojedinačna karta%30%minuta%' OR "
            "  article_name LIKE '%Pojedinačna karta%60%minuta%' OR"
            "  article_name LIKE '%Karte II zone%' OR"
            "  article_name LIKE '%Karta I zona%'"
            "ORDER BY article_id "
            "LIMIT 5;";


        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_.get()) << std::endl;
            do_write("[]");
            return;
        }
        auto prepare_time = std::chrono::steady_clock::now();
        auto prepare_latency = std::chrono::duration_cast<std::chrono::microseconds>(prepare_time - query_start).count();
        
        struct StmtDeleter
        {
            void operator()(sqlite3_stmt* s) const noexcept
            {
                if(s) sqlite3_finalize(s);
            }
        };
        std::unique_ptr<sqlite3_stmt, StmtDeleter> stmt_guard(stmt);

        json articles_array = json::array();
        int count = 0;

        while(sqlite3_step(stmt) == SQLITE_ROW)
        {
            json article;
            article["article_id"] = sqlite3_column_int(stmt, 0);
            
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            article["article_name"] = name ? name : "";
            
            article["article_price"] = sqlite3_column_double(stmt, 2);
            
            articles_array.push_back(article);
            count++;
        }
        
        std::string response = articles_array.dump();
        
        if (count == 0) 
        {
            std::cout << "No matching articles found in database\n";
            std::cout << "  Run: ./OCU fetch article\n";
        } 
        else 
        {
            std::cout << "Found and sending " << count << " articles from database\n";
        }
        

        auto query_end = std::chrono::steady_clock::now();
        auto total_query = std::chrono::duration_cast<std::chrono::microseconds>( query_end - query_start).count();

        std::cout << "DB prepare: " << prepare_latency << " μs, "<< "Total query: " << total_query << " μs\n";
        
        do_write(response);
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Error fetching articles: " << e.what() << "\n";
        do_write("[]");
    }
}

void Session::handle_card_validation(std::string_view card_number)
{
    auto coupon_id = find_coupon_by_card(card_number);
    
    if (coupon_id) {
        std::cout << "Card valid: " << card_number 
                 << " Coupon ID: " << *coupon_id << "\n";
        do_write(std::to_string(*coupon_id));
    } else {
        std::cout << "Card invalid: " << card_number << "\n";
        do_write("0");
    }
}

void Session::handle_purchase(int article_id, std::string_view card_number, int quantity)
{
    try
    {
        auto coupon_id = find_coupon_by_card(card_number);

        if(!coupon_id)
        {
            std::cout << "Purchase failed: Invalid card\n";
            log_purchase(article_id, card_number, quantity, false);
            do_write("FAIL Invalid card");
            return;
        }

        const char* sql = "SELECT article_name, article_price FROM articles WHERE article_id = ?;";

        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            std::cerr << "Failed to query article\n";
            log_purchase(article_id, card_number, quantity, false);
            do_write("FAIL Database error");
            return;
        }

        struct StmtDeleter
        {
            void operator()(sqlite3_stmt* s) const noexcept
            {
                if(s) sqlite3_finalize(s);
            }
        };

        std::unique_ptr<sqlite3_stmt, StmtDeleter> stmt_guard(stmt);
        sqlite3_bind_int(stmt, 1, article_id);

        if(sqlite3_step(stmt) != SQLITE_ROW)
        {
            std::cout << "Purchase failed: Article not found\n";
            log_purchase(article_id, card_number, quantity, false);
            do_write("FAIL Article not found");
            return;
        }

        const char* article_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt,0));
        double article_price = sqlite3_column_double(stmt, 1);

        std::cout << "Article: " << article_name << ", Article price: " << article_price;
        if(log_purchase(article_id,card_number, quantity, true))
        {
            std::cout << "Purchase successful!\n";
            std::cout << "  Card: " << card_number << "\n";
            std::cout << "  Article: " << article_name << "\n";
            std::cout << "  Coupon ID: " << *coupon_id << "\n";
            std::cout << "  Quantity: " << quantity << "\n";
            do_write("SUCCESS");
        }
        else
        {
            std::cout << "Failed to log purchase\n";
            do_write("FAIL Logging error");
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Purchase error: " << e.what() << std::endl;
        log_purchase(article_id, card_number, quantity, false);
        do_write("FAIL Internal error");
    }
}

bool Session::log_purchase(int article_id, std::string_view card_number, int quantity, bool success)
{
    const char* sql = 
        "INSERT INTO purchases (article_id, card_number, quantity, success, timestamp) "
        "VALUES (?, ?, ?, ?, datetime('now', 'localtime'));";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Failed to prepare purchase log: " << sqlite3_errmsg(db_.get()) << "\n";
        return false;
    }
    
    struct StmtDeleter 
    {
        void operator()(sqlite3_stmt* s) const noexcept 
        {
            if (s) sqlite3_finalize(s);
        }
    };
    std::unique_ptr<sqlite3_stmt, StmtDeleter> stmt_guard(stmt);
    
    sqlite3_bind_int(stmt, 1, article_id);
    sqlite3_bind_text(stmt, 2, card_number.data(), card_number.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, quantity);
    sqlite3_bind_int(stmt, 4, success ? 1 : 0);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) 
    {
        std::cerr << "Failed to log purchase: " << sqlite3_errmsg(db_.get()) << "\n";
        return false;
    }
    
    return true;
}   

void Session::handle_QR(std::string token, int validator_id)
{
    bool success = validate_QR(token);
    if(!success) 
    {
        do_write("Failed to validate QR token");
        return;
    }

    const char* sql =
        "INSERT INTO qr_validated(qr_code, validator_id, valid) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt* stmt;
    if(sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Failed to prepare QR_validate\n", sqlite3_errmsg(db_.get());
        return;
    }

    struct StmtDeleter
    {
        void operator()(sqlite3_stmt* s) const noexcept
        {
            if(s) sqlite3_finalize(s);
        }
    };
    std::unique_ptr<sqlite3_stmt, StmtDeleter> stmt_guard(stmt);

    sqlite3_bind_text(stmt, 1, token.data(), token.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, validator_id);
    sqlite3_bind_int(stmt, 3, success);

    if(sqlite3_step(stmt) != SQLITE_DONE)
    {
        std::cerr << "Failed to log qr validation: " << sqlite3_errmsg(db_.get()) << "\n";
        return;
    }

    return;
}

bool Session::validate_QR(std::string token)
{
    const char* sql = 
        "SELECT token, valid_from, valid_to from tickets where Token = ?;";

    sqlite3_stmt* stmt;

    if(sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr))
    {
        std::cerr << "Failed to prepare query for validate_QR\n";
        return false;
    }

    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);

    bool is_valid = false;
    bool is_activated = false;

    if(sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* valid_from_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* valid_to_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

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
                if(!is_valid) std::cout << "QR not valid for: " << token << '\n';
            }
        }
        else
        {
            const char* sql = "UPDATE tickets SET valid_from = ?, valid_to = ? WHERE token = ?;";

            sqlite3_stmt* stmt;
            
            if(!sqlite3_prepare_v2(db_.get(),sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                std::cout << "Failed to prepare activating ticket" << sqlite3_errmsg(db_.get());
                return -1;
            }
            sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);

            struct StmtDeleter
                {
                    void operator()(sqlite3_stmt* s) const noexcept
                    {
                        if(s) sqlite3_finalize(s);
                    }
                };
            std::unique_ptr<sqlite3_stmt, StmtDeleter> stmt_guard(stmt);
            
            auto now = std::chrono::system_clock::now();
            auto expires = now + std::chrono::minutes(30);
                
                // Convert to ISO8601 strings
            std::string valid_from_str = format_iso8601(now);
            std::string valid_to_str = format_iso8601(expires);
                
            sqlite3_bind_text(stmt, 1, valid_from_str.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, valid_to_str.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, token.c_str(), -1, SQLITE_TRANSIENT);

            if(!sqlite3_step(stmt) != SQLITE_DONE)
            {
                std::cout << "Failed to insert/activate ticket" << sqlite3_errmsg(db_.get());
                return -1;
            }
            std::cout << "Ticket ACTIVATED\n";
            do_write(R"({"status":"TICKET_ACTIVATED","isValid":true})");
            is_valid = true;
            return is_valid;
        }
    }
    sqlite3_finalize(stmt);
    if(is_valid) 
    {
        std::cout << "Valid QR token: " << token << "\n";
        do_write(R"({"isValid":true})");
        return is_valid;

    }
    else
    {
        std::cout << "Invalid QR token: " << token << "\n";
        do_write(R"({"isValid":false})");
        return is_valid;
    }
    
}

std::optional<std::chrono::system_clock::time_point>
    Session::parse_iso8601(std::string_view datetime_str)
    {
        std::tm tm = {};
        
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
std::string Session::format_iso8601(const std::chrono::system_clock::time_point& tp)
{
    std::time_t time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&time);
    
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
    
    return std::string(buffer);
}

bool Session::handle_QR_activation(std::string token)
{
    // pogledaj da li je aktivirano
    const char* sql =
    "SELECT active from tickets where Token = ?;";

    sqlite3_stmt* stmt;

    if(sqlite3_prepare_v2(db_.get(),sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cout << "Failed to prepare QR activation ";
    }
    


    // ako nije aktiviraj

    
    
}