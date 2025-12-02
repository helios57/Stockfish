#include "grpc_agent.h"
#include <iostream>
#include <chrono>
#include <thread>

// Stockfish headers
#include "search.h"
#include "types.h"
#include "misc.h"

namespace Stockfish {

namespace {
const std::string StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
}

GrpcAgent::GrpcAgent(const AgentConfig& cfg) : config(cfg) {
    std::string target = config.server + ":" + std::to_string(config.server_port);
    std::shared_ptr<grpc::ChannelCredentials> creds;
    
    if (config.use_tls) {
        grpc::SslCredentialsOptions ssl_opts;
        // Use system roots by default if pem_root_certs is empty
        creds = grpc::SslCredentials(ssl_opts);
    } else {
        creds = grpc::InsecureChannelCredentials();
    }
    
    channel = grpc::CreateChannel(target, creds);
    stub = chess_contest::ChessGame::NewStub(channel);
    
    engine = std::make_unique<Engine>();
    
    // Set callbacks
    engine->set_on_bestmove([this](std::string_view bestmove, std::string_view ponder) {
        this->on_bestmove(bestmove, ponder);
    });
    
    engine->set_on_update_no_moves([](const Engine::InfoShort&) {});
    engine->set_on_update_full([](const Engine::InfoFull&) {});
    engine->set_on_iter([](const Engine::InfoIter&) {});
    engine->set_on_verify_networks([](std::string_view msg) { 
        std::cout << "Network verify: " << msg << std::endl; 
    });
}

GrpcAgent::~GrpcAgent() {
    if (engine) {
        engine->stop();
    }
}

void GrpcAgent::start() {
    // Loop for reconnection
    while (true) {
        std::cout << "Connecting to " << config.server << ":" << config.server_port << "..." << std::endl;
        
        grpc::ClientContext context;
        stream = stub->PlayGame(&context);
        
        // Send JoinRequest
        chess_contest::ClientToServerMessage req;
        auto join = req.mutable_join_request();
        join->set_api_key(config.api_key);
        join->set_game_mode(config.game_mode);
        join->set_time_control(config.time_control);
        join->set_agent_name(config.agent_name);
        join->set_wait_for_challenge(config.wait_for_challenge);
        if (!config.specific_opponent_agent_id.empty()) {
            join->set_specific_opponent_agent_id(config.specific_opponent_agent_id);
        }
        
        bool success = false;
        {
            std::lock_guard<std::mutex> lock(stream_mutex);
            if (stream->Write(req)) {
                success = true;
            }
        }

        if (!success) {
            std::cerr << "Failed to send JoinRequest" << std::endl;
        } else {
            std::cout << "Joined. Waiting for server messages..." << std::endl;
            should_exit_stream = false;
            run_stream();
        }
        
        // If run_stream returns, stream is closed.
        std::cout << "Disconnected. Retrying in 5 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Reset stream
        {
            std::lock_guard<std::mutex> lock(stream_mutex);
            stream.reset();
        }
    }
}

void GrpcAgent::run_stream() {
    chess_contest::ServerToClientMessage msg;
    while (stream->Read(&msg)) {
        handle_server_message(msg);
        if (should_exit_stream) break;
    }
}

void GrpcAgent::handle_server_message(const chess_contest::ServerToClientMessage& msg) {
    switch (msg.message_case()) {
        case chess_contest::ServerToClientMessage::kGameStarted:
            handle_game_started(msg.game_started());
            break;
        case chess_contest::ServerToClientMessage::kMoveRequest:
            handle_move_request(msg.move_request());
            break;
        case chess_contest::ServerToClientMessage::kDrawOffer:
            handle_draw_offer(msg.draw_offer());
            break;
        case chess_contest::ServerToClientMessage::kGameOver:
            handle_game_over(msg.game_over());
            break;
        case chess_contest::ServerToClientMessage::kError:
            handle_error(msg.error());
            break;
        default:
            std::cerr << "Unknown message type received" << std::endl;
            break;
    }
}

void GrpcAgent::handle_game_started(const chess_contest::GameStarted& msg) {
    std::cout << "Game started: " << msg.game_id() 
              << " vs " << msg.opponent_name() 
              << " (Color: " << msg.color() << ")" << std::endl;
    
    // Stop and clear engine outside the lock to avoid deadlock
    std::cout << "Stopping engine..." << std::endl;
    engine->stop();
    std::cout << "Clearing search..." << std::endl;
    engine->search_clear();
    
    {
        std::lock_guard<std::mutex> lock(agent_mutex);
        current_game_id = msg.game_id();
        my_color = msg.color(); // "WHITE" or "BLACK"
        increment_ms = msg.increment_ms();
        game_moves.clear();
        active_search_game_id.clear();
    }
    
    std::cout << "Setting position..." << std::endl;
    engine->reset();
    std::cout << "Game setup complete." << std::endl;
}

void GrpcAgent::handle_move_request(const chess_contest::MoveRequest& msg) {
    std::string opp_move = msg.opponent_move_lan();
    std::cout << "Received MoveRequest. Opponent move: " << (opp_move.empty() ? "none" : opp_move) 
              << " Time left: " << msg.your_remaining_time_ms() << "ms" << std::endl;
    
    std::string game_id;
    std::string color;
    int inc_ms;
    
    {
        std::lock_guard<std::mutex> lock(agent_mutex);
        if (!opp_move.empty()) {
            game_moves.push_back(opp_move);
        }
        game_id = current_game_id;
        color = my_color;
        inc_ms = increment_ms;
        
        // We set this here to allow on_bestmove to proceed when it fires
        active_search_game_id = current_game_id;
    }
    
    // Engine operations outside lock to avoid deadlock if engine->go() blocks waiting for previous search
    if (!opp_move.empty()) {
        engine->apply_move(opp_move);
    }
    
    Search::LimitsType limits;
    
    // Color string is "WHITE" or "BLACK"
    // Stockfish Color enum: WHITE=0, BLACK=1
    Color us = (color == "WHITE") ? WHITE : BLACK;
    Color them = ~us;
    
    limits.time[us] = msg.your_remaining_time_ms();
    limits.time[them] = msg.opponent_remaining_time_ms();
    limits.inc[us] = inc_ms;
    limits.inc[them] = inc_ms;
    
    limits.startTime = now(); 
    
    engine->go(limits);
}

void GrpcAgent::on_bestmove(std::string_view bestmove, std::string_view ponder) {
    std::string move_str(bestmove);
    
    std::lock_guard<std::mutex> lock(agent_mutex);
    
    if (active_search_game_id != current_game_id || current_game_id.empty()) {
         return;
    }
    
    game_moves.push_back(move_str);
    
    // Apply our move to the engine state incrementally
    engine->apply_move(move_str);

    chess_contest::ClientToServerMessage req;
    auto resp = req.mutable_move_response();
    resp->set_game_id(current_game_id);
    resp->set_move_lan(move_str);
    
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    if (stream) {
        if (!stream->Write(req)) {
             std::cerr << "Failed to write MoveResponse" << std::endl;
        }
    }
    
    std::cout << "Bestmove: " << move_str << std::endl;
}

void GrpcAgent::handle_draw_offer(const chess_contest::DrawOfferEvent& msg) {
    if (config.auto_accept_draw) {
        chess_contest::ClientToServerMessage req;
        auto resp = req.mutable_draw_offer_response();
        resp->set_game_id(msg.game_id());
        resp->set_accepted(true);
        
        std::lock_guard<std::mutex> lock(stream_mutex);
        if (stream) stream->Write(req);
        std::cout << "Auto-accepted draw offer." << std::endl;
    }
}

void GrpcAgent::handle_game_over(const chess_contest::GameOver& msg) {
    std::cout << "Game Over: " << msg.result() << " Reason: " << msg.reason() << std::endl;
    std::lock_guard<std::mutex> lock(agent_mutex);
    engine->stop();
    active_search_game_id.clear();
    should_exit_stream = true;
}

void GrpcAgent::handle_error(const chess_contest::Error& msg) {
    std::cerr << "Server Error: " << msg.message() << std::endl;
}

} // namespace Stockfish
