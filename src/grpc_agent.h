#ifndef GRPC_AGENT_H
#define GRPC_AGENT_H

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <optional>

#include <grpcpp/grpcpp.h>
#include "chess_contest.grpc.pb.h"
#include "engine.h"
#include "agent_config.h"

namespace Stockfish {

class GrpcAgent {
public:
    GrpcAgent(const AgentConfig& config);
    ~GrpcAgent();

    void start();

private:
    void run_stream();
    void handle_server_message(const chess_contest::ServerToClientMessage& msg);
    
    void handle_game_started(const chess_contest::GameStarted& msg);
    void handle_move_request(const chess_contest::MoveRequest& msg);
    void handle_draw_offer(const chess_contest::DrawOfferEvent& msg);
    void handle_game_over(const chess_contest::GameOver& msg);
    void handle_error(const chess_contest::Error& msg);

    void on_bestmove(std::string_view bestmove, std::string_view ponder);

    AgentConfig config;
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<chess_contest::ChessGame::Stub> stub;
    
    // Engine instance
    std::optional<Engine> engine;
    
    // Stream for communication
    std::shared_ptr<grpc::ClientReaderWriter<chess_contest::ClientToServerMessage, chess_contest::ServerToClientMessage>> stream;
    std::mutex stream_mutex;
    
    // Game state protection
    std::mutex agent_mutex;
    
    // Game state
    std::string current_game_id;
    std::string active_search_game_id;
    std::string my_color; // "WHITE" or "BLACK"
    std::vector<std::string> game_moves;
    int increment_ms;
    bool should_exit_stream;
};

} // namespace Stockfish

#endif // GRPC_AGENT_H
