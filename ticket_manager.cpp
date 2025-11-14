#include "ticket_manager.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace Tickets 
{
    TicketManager::TicketManager(Database& db, const std::string& grpc_server_address)
        : db_(db)
        , server_address_(grpc_server_address)
        , running_(false)
    {
        std::cout << "=== Ticket Manager (gRPC Client) ===\n";
        std::cout << "Server: " << server_address_ << "\n";
        std::cout << "=====================================\n\n";
        
        // Enable WAL checkpoint on every transaction for immediate persistence
        char* err_msg = nullptr;
        if (sqlite3_exec(db_.get(), "PRAGMA synchronous = NORMAL;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "[TicketManager] Warning: Could not set synchronous mode: " << err_msg << '\n';
            sqlite3_free(err_msg);
        }
    }

    TicketManager::~TicketManager()
    {
        Stop();
    }

    void TicketManager::Start()
    {
        if (running_.exchange(true)) {
            return;
        }
        
        streaming_thread_ = std::thread(&TicketManager::StreamingThread, this);
        std::cout << "[TicketManager] Started streaming thread\n";
    }

    void TicketManager::Stop()
    {
        if (!running_.exchange(false)) {
            return;
        }
        
        std::cout << "[TicketManager] Stopping...\n";
        
        {
            std::lock_guard<std::mutex> lock(context_mutex_);
            if (current_context_) {
                std::cout << "[TicketManager] Cancelling gRPC stream...\n";
                current_context_->TryCancel();
            }
        }
        
        if (streaming_thread_.joinable()) {
            streaming_thread_.join();
        }
        
        std::cout << "[TicketManager] Stopped\n";
    }

    void TicketManager::StreamingThread()
    {
        while (running_) {
            try {
                std::cout << "[TicketManager] Connecting to gRPC server at " << server_address_ << "...\n";
                
                
                channel_ = grpc::CreateChannel(
                    server_address_,
                    grpc::InsecureChannelCredentials()
                );
                
                auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
                if (!channel_->WaitForConnected(deadline)) {
                    std::cerr << "[TicketManager] Failed to connect to server. Retrying in 5 seconds...\n";
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }
                
                stub_ = vehicle::TicketSync::NewStub(channel_);
                
                // Create request (empty, matching Python version)
                vehicle::SubscribeForNewTicketsRequest request;
                
                std::cout << "[TicketManager] Connected. Waiting for new tickets...\n";
                
                // Start streaming - store context so Stop() can cancel it
                grpc::ClientContext context;
                {
                    std::lock_guard<std::mutex> lock(context_mutex_);
                    current_context_ = &context;
                }
                
                std::unique_ptr<grpc::ClientReader<vehicle::SubscribeForNewTicketsResponse>> reader(
                    stub_->SubscribeForNewTickets(&context, request)
                );
                
                // Read stream
                vehicle::SubscribeForNewTicketsResponse response;
                while (running_ && reader->Read(&response)) {
                    if (response.has_new_ticket_created()) {
                        std::cout << "[TicketManager] New ticket received\n";
                        
                        // Convert and insert ticket
                        Ticket ticket = ConvertFromProto(response.new_ticket_created());
                        
                        if (InsertTicket(ticket)) {
                            std::cout << "[TicketManager] Successfully stored ticket ID: " 
                                     << ticket.ticket_id << "\n";
                        } else {
                            std::cerr << "[TicketManager] Failed to store ticket ID: " 
                                     << ticket.ticket_id << "\n";
                        }
                    }
                }
                
                // Clear context pointer
                {
                    std::lock_guard<std::mutex> lock(context_mutex_);
                    current_context_ = nullptr;
                }

                // Check status
                grpc::Status status = reader->Finish();
                if (!status.ok() && status.error_code() != grpc::StatusCode::CANCELLED) {
                    std::cerr << "[TicketManager] Stream ended: " << status.error_code() 
                            << " - " << status.error_message() << "\n";
                    
                    if (running_) {
                        std::cout << "[TicketManager] Reconnecting in 5 seconds...\n";
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                    }
                } else if (status.error_code() == grpc::StatusCode::CANCELLED) {
                    std::cout << "[TicketManager] Stream cancelled (shutdown requested)\n";
                }
                
            } catch (const std::exception& e) {
                std::cerr << "[TicketManager] Error: " << e.what() << "\n";
                if (running_) {
                    std::cout << "[TicketManager] Retrying in 5 seconds...\n";
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
        }
    }

    Ticket TicketManager::ConvertFromProto(const vehicle::Ticket& proto_ticket)
    {
        Ticket ticket;
        
        ticket.ticket_id = proto_ticket.id();
        ticket.active = proto_ticket.active();
        
        if (proto_ticket.has_date_created()) {
            ticket.date_created = TimestampToString(proto_ticket.date_created());
        }
        
        if (proto_ticket.has_account_id()) {
            ticket.account_id = proto_ticket.account_id().value();
        }
        
        ticket.caption = proto_ticket.caption();
        
        if (proto_ticket.has_valid_from()) {
            ticket.valid_from = TimestampToString(proto_ticket.valid_from());
        }
        
        if (proto_ticket.has_valid_to()) {
            ticket.valid_to = TimestampToString(proto_ticket.valid_to());
        }
        
        if (proto_ticket.has_traffic_zone()) {
            ticket.traffic_zone = proto_ticket.traffic_zone().value();
        }
        
        if (proto_ticket.has_article_id()) {
            ticket.article_id = proto_ticket.article_id().value();
        }
        
        if (proto_ticket.has_invoice_item_id()) {
            ticket.invoice_item_id = proto_ticket.invoice_item_id().value();
        }
        
        if (proto_ticket.has_token()) {
            ticket.token = proto_ticket.token().value();
        }
        
        return ticket;
    }

bool Session::validate_QR(std::string token)
{
    const char* sql = 
        "SELECT token, valid_from, valid_to from tickets where token = ?;";

    sqlite3_stmt* stmt;

    if(sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "Failed to prepare query for validate_QR: " << sqlite3_errmsg(db_.get()) << "\n";
        return false;
    }

    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);

    bool is_valid = false;

    if(sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* valid_from_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* valid_to_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        if(valid_from_str && valid_to_str)
        {
            std::string valid_from_copy(valid_from_str);
            std::string valid_to_copy(valid_to_str);

            auto time_from = parse_iso8601(valid_from_copy);
            auto time_to = parse_iso8601(valid_to_copy);
            auto now = std::chrono::system_clock::now();

            if(time_from && time_to)
            {
                is_valid = (now >= *time_from && now <= *time_to);
                if(is_valid) 
                {
                    std::cout << "Ticket is VALID (within time range)\n";
                }
                else 
                {
                    std::cout << "Ticket EXPIRED or not yet valid\n";
                }
            }
            else
            {
                std::cout << "Failed to parse times\n";
            }
        }
        else
        {
            std::cout << "Ticket times are NULL - activating ticket\n";
            
            sqlite3_finalize(stmt);
            
            char* err_msg = nullptr;
            if (sqlite3_exec(db_.get(), "BEGIN IMMEDIATE;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
                std::cerr << "Failed to begin transaction: " << err_msg << '\n';
                sqlite3_free(err_msg);
                return false;
            }
            
            const char* update_sql = "UPDATE tickets SET valid_from = ?, valid_to = ? WHERE token = ?;";
            sqlite3_stmt* stmt_update;
            
            if(sqlite3_prepare_v2(db_.get(), update_sql, -1, &stmt_update, nullptr) != SQLITE_OK)
            {
                std::cout << "Failed to prepare activating ticket: " << sqlite3_errmsg(db_.get()) << '\n';
                sqlite3_exec(db_.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
                return false;
            }

            struct StmtDeleter
            {
                void operator()(sqlite3_stmt* s) const noexcept
                {
                    if(s) sqlite3_finalize(s);
                }
            };
            std::unique_ptr<sqlite3_stmt, StmtDeleter> stmt_guard(stmt_update);
            
            auto now = std::chrono::system_clock::now();
            auto expires = now + std::chrono::minutes(30);
                
            std::string valid_from_new = format_iso8601(now);
            std::string valid_to_new = format_iso8601(expires);
                
            sqlite3_bind_text(stmt_update, 1, valid_from_new.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_update, 2, valid_to_new.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_update, 3, token.c_str(), -1, SQLITE_TRANSIENT);

            if(sqlite3_step(stmt_update) != SQLITE_DONE)
            {
                std::cout << "Failed to activate ticket: " << sqlite3_errmsg(db_.get()) << '\n';
                sqlite3_exec(db_.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
                return false;
            }
            
            // Commit transaction
            if (sqlite3_exec(db_.get(), "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
                std::cerr << "Failed to commit transaction: " << err_msg << '\n';
                sqlite3_free(err_msg);
                return false;
            }
            
            int log_size, checkpointed;
            int rc = sqlite3_wal_checkpoint_v2(
                db_.get(),
                nullptr,
                SQLITE_CHECKPOINT_FULL,
                &log_size,
                &checkpointed
            );
            
            if (rc != SQLITE_OK) {
                std::cerr << "Warning: WAL checkpoint failed: " 
                         << sqlite3_errmsg(db_.get()) << '\n';
            } else {
                std::cout << "WAL checkpoint: " << checkpointed 
                         << "/" << log_size << " frames checkpointed\n";
            }
            
            std::cout << "Ticket ACTIVATED\n";
            do_write(R"({"status":"TICKET_ACTIVATED","isValid":true})");
            return true;
        }
    }
    else
    {
        std::cout << "Ticket NOT FOUND in database\n";
    }
    
    sqlite3_finalize(stmt);
    
    if(is_valid) 
    {
        std::cout << "Valid QR token: " << token << "\n";
        do_write(R"({"isValid":true})");
    }
    else
    {
        std::cout << "Invalid QR token: " << token << "\n";
        do_write(R"({"isValid":false})");
    }
    
    return is_valid;
}

    std::string TicketManager::TimestampToString(const google::protobuf::Timestamp& ts)
    {
        time_t seconds = ts.seconds();
        struct tm* tm_info = localtime(&seconds);
        char buffer[26];
        strftime(buffer, 26, "%Y-%m-%dT%H:%M:%S", tm_info);
        return std::string(buffer);
    }
}