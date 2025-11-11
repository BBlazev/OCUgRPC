#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <grpcpp/grpcpp.h>
#include "ticket_sync.grpc.pb.h"
#include "database.hpp"

namespace Tickets 
{
    struct Ticket 
    {
        int64_t ticket_id;
        bool active;
        std::string date_created;
        std::optional<int> account_id;
        std::string caption;
        std::string valid_from;
        std::string valid_to;
        std::string traffic_area;
        std::optional<int> traffic_zone;
        std::optional<int> article_id;
        std::optional<int> invoice_item_id;
        std::string token;
    };

    class TicketManager 
    {
    public:
        TicketManager(Database& db, const std::string& grpc_server_address);
        ~TicketManager();
        
        void Start();
        void Stop();
        
    private:
        void StreamingThread();
        bool InsertTicket(const Ticket& ticket);
        Ticket ConvertFromProto(const vehicle::Ticket& proto_ticket);
        std::string TimestampToString(const google::protobuf::Timestamp& ts);
        
        Database& db_;
        std::string server_address_;
        std::atomic<bool> running_;
        std::unique_ptr<vehicle::TicketSync::Stub> stub_;
        std::shared_ptr<grpc::Channel> channel_;
        std::thread streaming_thread_;
    };
}