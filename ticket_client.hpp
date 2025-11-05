#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

#include <grpcpp/grpcpp.h>
#include "ticket_sync.grpc.pb.h"

class TicketClient {
public:
    TicketClient(const std::string& server_address, int vehicle_id);
    ~TicketClient();
    
    void Run();
    void Stop();

private:
    void ConnectAndStream();
    void PrintTicket(const vehicle::Ticket& ticket);
    std::string TimestampToString(const google::protobuf::Timestamp& ts);
    
    std::string server_address_;
    int vehicle_id_;
    std::atomic<bool> running_;
    std::unique_ptr<vehicle::TicketSync::Stub> stub_;
    std::shared_ptr<grpc::Channel> channel_;
};
//s 