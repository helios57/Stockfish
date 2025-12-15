#include "grpc_agent.h"
#include <iostream>
#include <chrono>
#include <thread>

// Stockfish headers
#include "search.h"
#include "types.h"
#include "misc.h"
#include "option.h"

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
    
    engine.emplace();
    // Set engine options from config
    engine->get_options()["Skill Level"] = std::to_string(config.skill_level);
    engine->get_options()["LimitStrength"] = config.limit_strength ? std::string("true") : std::string("false");
    engine->get_options()["Elo"] = std::to_string(config.elo);
    engine->get_options()["Hash"] = std::to_string(config.hash);
    engine->get_options()["Ponder"] = config.ponder ? std::string("true") : std::string("false");
    engine->get_options()["MultiPV"] = std::to_string(config.multi_pv);
    engine->get_options()["Threads"] = std::to_string(config.threads);

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
        std::cout << "Connecting to " << config.server << ":" << config.server_port << " (group: " << config.agent_group << ")..." << std::endl;
        
        grpc::ClientContext context;
        stream = stub->PlayGame(&context);
        
        // Send JoinRequest
        chess_contest::ClientToServerMessage req;
        auto join = req.mutable_join_request();
        join->set_api_key(config.api_key);
        join->set_game_mode(config.game_mode);
        join->set_time_control(config.time_control);
        join->set_agent_name(config.agent_name);
        if (!config.agent_group.empty()) {
            join->set_agent_group(config.agent_group);
        }
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
        } else {
            std::cout << "Joined. Waiting for server messages..." << std::endl;
            should_exit_stream = false;
            run_stream();

            grpc::Status status = stream->Finish();
            if (!status.ok()) {
                std::cout << "RPC failed: " << status.error_code() << ": " << status.error_message() << std::endl;
            } else {
                std::cout << "RPC finished cleanly." << std::endl;
            }
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
        is_pondering = false;
        is_searching_main = false;
    }

    std::cout << "Setting position..." << std::endl;
    engine->set_position(StartFEN, {});
    
    // Preheat engine: warm up neural networks and search structures
    std::cout << "Preheating engine..." << std::endl;
    Search::LimitsType preheat_limits;
    preheat_limits.depth = 6;  // Quick shallow search to initialize caches
    preheat_limits.startTime = now();
    engine->go(preheat_limits);
    engine->wait_for_search_finished();
    std::cout << "Engine preheated and ready." << std::endl;
    
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
        std::unique_lock<std::mutex> lock(agent_mutex);

        // STOP PONDERING if active
        if (is_pondering) {
            // Unlock to allow engine methods (which might block) to run
            lock.unlock();
            engine->stop();
            engine->wait_for_search_finished();
            lock.lock();
            is_pondering = false;
        }

        if (!opp_move.empty()) {
            game_moves.push_back(opp_move);
        }
        game_id = current_game_id;
        color = my_color;
        inc_ms = increment_ms;

        // We set this here to allow on_bestmove to proceed when it fires
        active_search_game_id = current_game_id;
        is_searching_main = true;

        // Update position safely inside lock
        // This ensures 'states' is recreated and valid for the next search
        engine->set_position(StartFEN, game_moves);
    }

    Search::LimitsType limits;

    // Color string is "WHITE" or "BLACK"
    // Stockfish Color enum: WHITE=0, BLACK=1
    Color us = (color == "WHITE") ? WHITE : BLACK;
    Color them = ~us;

    // --- IMPLEMENTATION OF DEFENSIVE TIME MANAGEMENT ---
    
    // 1. Get the actual time reported by the server
    int64_t actual_time_ms = msg.your_remaining_time_ms();
    
    // 2. Apply static safety margin (reserve X ms)
    int64_t defensive_time = actual_time_ms - config.time_safety_margin_ms;
    
    // 3. Apply usage multiplier (scale down by percentage)
    if (config.time_usage_multiplier > 0.0) {
        defensive_time = static_cast<int64_t>(defensive_time * config.time_usage_multiplier);
    }

    // 4. Hard floor to prevent passing 0 or negative time which might confuse the engine
    //    (Minimum 100ms or actual time if very low)
    if (defensive_time < 100) {
        defensive_time = std::max<int64_t>(10, actual_time_ms - 50); // Last resort panic time
    }

    std::cout << "Time Management: Server=" << actual_time_ms 
              << "ms, Defensive=" << defensive_time 
              << "ms (Margin=" << config.time_safety_margin_ms 
              << ", Mult=" << config.time_usage_multiplier << ")" << std::endl;

    // 5. Pass the defensive time to the engine
    limits.time[us] = defensive_time;
    limits.time[them] = msg.opponent_remaining_time_ms(); // Opponent time doesn't strictly matter for our allocation
    limits.inc[us] = inc_ms;
    limits.inc[them] = inc_ms;

    limits.startTime = now();

    engine->go(limits);
}

void GrpcAgent::on_bestmove(std::string_view bestmove, std::string_view ponder) {
    std::string move_str(bestmove);
    std::string ponder_str(ponder);

    {
        std::lock_guard<std::mutex> lock(agent_mutex);

        // If we are not searching for the main move, this bestmove is likely
        // the result of stopping a ponder search or an aborted search.
        if (!is_searching_main) {
            return;
        }

        is_searching_main = false;

        if (active_search_game_id != current_game_id || current_game_id.empty()) {
             return;
        }

        game_moves.push_back(move_str);

        // Apply our move to the engine state incrementally
        // Use set_position to ensure states is recreated (it was moved to threads during search)
        engine->set_position(StartFEN, game_moves);
    }

    chess_contest::ClientToServerMessage req;
    auto resp = req.mutable_move_response();
    resp->set_game_id(current_game_id);
    resp->set_move_lan(move_str);

    {
        std::lock_guard<std::mutex> stream_lock(stream_mutex);
        if (stream) {
            if (!stream->Write(req)) {
                // handle error
            }
        }
    }

    std::cout << "Bestmove: " << move_str<< " Ponder:" << ponder_str << std::endl;

    if (!ponder_str.empty() && !should_exit_stream) {
        std::thread([this, ponder_str]() {
            this->start_ponder(ponder_str);
        }).detach();
    }
}

void GrpcAgent::start_ponder(std::string ponder_move) {
    engine->wait_for_search_finished();

    std::lock_guard<std::mutex> lock(agent_mutex);

    if (is_searching_main) return;

    if (active_search_game_id != current_game_id || current_game_id.empty()) return;
    if (is_pondering) return;

    std::cout << "Starting ponder on: " << ponder_move << std::endl;

    is_pondering = true;
    predicted_ponder_move = ponder_move;

    std::vector<std::string> speculative_moves = game_moves;
    speculative_moves.push_back(ponder_move);

    engine->set_position(StartFEN, speculative_moves);

    Search::LimitsType limits;
    limits.ponderMode = true;
    limits.infinite = 1;

    engine->go(limits);
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

    // Stop engine outside lock to prevent deadlocks with on_bestmove
    engine->stop();

    {
        std::lock_guard<std::mutex> lock(agent_mutex);
        active_search_game_id.clear();
        is_pondering = false;
        is_searching_main = false;
        should_exit_stream = true;
    }

    // Signal that we are done writing. This should cause the server to close
    // the stream, which in turn will make stream->Read() return false,
    // unblocking the message loop.
    {
        std::lock_guard<std::mutex> lock(stream_mutex);
        if (stream) {
            stream->WritesDone();
        }
    }
}

void GrpcAgent::handle_error(const chess_contest::Error& msg) {
}

} // namespace Stockfish
