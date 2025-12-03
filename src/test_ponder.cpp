
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>

#include "grpc_agent.h"
#include "agent_config.h"
#include "misc.h"
#include "movegen.h"
#include "move_conversion.h"

namespace Stockfish {

// Defined locally if not found
const std::string StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

class PonderTest {
public:
    static void run() {
        std::cout << "Running PonderTest..." << std::endl;
        test_state_initialization();
        test_simulation_10_moves();
        std::cout << "PonderTest Passed!" << std::endl;
    }

private:
    static void test_state_initialization() {
        std::cout << "  [Test] State Initialization..." << std::endl;
        AgentConfig config;
        config.api_key = "test";
        config.server = "localhost";
        config.server_port = 50051;

        GrpcAgent agent(config);

        assert(agent.is_pondering == false);
        assert(agent.is_searching_main == false);
        assert(agent.predicted_ponder_move.empty());

        std::cout << "    Passed." << std::endl;
    }

    static void test_simulation_10_moves() {
        std::cout << "  [Test] Simulation 10 Moves (5 Miss, 5 Hit)..." << std::endl;

        AgentConfig config;
        config.api_key = "test";
        config.server = "localhost";
        config.server_port = 50051;
        config.time_control = "60+1";

        GrpcAgent agent(config);

        // Simulate GameStarted
        chess_contest::GameStarted started_msg;
        started_msg.set_game_id("test_game_1");
        started_msg.set_opponent_name("Opponent");
        started_msg.set_color("WHITE"); // We are white
        started_msg.set_increment_ms(100);
        started_msg.set_initial_time_ms(60000);

        std::cout << "    Handling GameStarted..." << std::endl;
        agent.handle_game_started(started_msg);

        // Loop for 10 moves
        for (int i = 1; i <= 10; ++i) {
            std::cout << "\n    --- Turn " << i << " ---" << std::endl;

            // 1. Determine Opponent Move to send
            std::string opponent_move_lan;

            if (i == 1) {
                // First move for White is response to empty
                opponent_move_lan = "";
            } else {
                std::string predicted = agent.predicted_ponder_move;
                std::cout << "    Agent predicted: '" << predicted << "'" << std::endl;

                // Moves 2-6: Miss
                // Moves 7-10: Hit (if available)

                bool force_hit = (i >= 7);

                if (force_hit && !predicted.empty()) {
                    std::cout << "    [Strategy] Ponder HIT: Sending " << predicted << std::endl;
                    opponent_move_lan = predicted;
                } else {
                    // Force Miss or no prediction available
                    // Reconstruct temporary position to generate legal moves
                    Position temp_pos;
                    StateInfo si;
                    temp_pos.set(StartFEN, false, &si);
                    StateListPtr states(new std::deque<StateInfo>(1));

                    // Replay confirmed moves
                    // agent.game_moves contains: OppMove1, MyMove1, OppMove2, MyMove2...
                    // We need position after LAST move.
                    for (const auto& m_str : agent.game_moves) {
                        Move m = to_move(temp_pos, m_str);
                        states->emplace_back();
                        temp_pos.do_move(m, states->back(), nullptr);
                    }

                    // Now generate moves for the opponent
                    MoveList<LEGAL> moves(temp_pos);

                    std::string chosen_move;
                    for (const auto& m : moves) {
                        std::string m_str = move_to_string(m, false);
                        if (m_str != predicted) {
                            chosen_move = m_str;
                            break;
                        }
                    }

                    if (chosen_move.empty() && moves.size() > 0) {
                        chosen_move = move_to_string(*(moves.begin()), false);
                    }

                    if (chosen_move.empty()) {
                         std::cout << "    [Error] No legal moves found!" << std::endl;
                         break;
                    }

                    opponent_move_lan = chosen_move;
                    std::cout << "    [Strategy] Ponder MISS: Sending " << opponent_move_lan << std::endl;
                }
            }

            // 2. Send MoveRequest
            chess_contest::MoveRequest req;
            req.set_opponent_move_lan(opponent_move_lan);
            req.set_your_remaining_time_ms(1000);
            req.set_opponent_remaining_time_ms(1000);

            if (i > 1) {
                if (agent.is_pondering) {
                    std::cout << "    [Check] Agent is pondering (Correct)." << std::endl;
                } else {
                    std::cout << "    [Check] Agent is NOT pondering." << std::endl;
                }
            }

            std::cout << "    Sending MoveRequest..." << std::endl;
            agent.handle_move_request(req);

            // 3. Wait for Agent to Move
            bool move_made = false;
            for (int k=0; k<50; k++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                std::lock_guard<std::mutex> lock(agent.agent_mutex);

                size_t current_size = agent.game_moves.size();
                size_t target_size = (i == 1) ? 1 : (i - 1) * 2 + 1;

                if (current_size >= target_size) {
                    move_made = true;
                    if (current_size > 0)
                        std::cout << "    Agent moved: " << agent.game_moves.back() << std::endl;
                    break;
                }
            }

            if (!move_made) {
                 std::cout << "    [Timeout] Agent did not move!" << std::endl;
                 break;
            }

            // 4. Wait for Ponder to Start
            bool ponder_started = false;
            for (int k=0; k<20; k++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> lock(agent.agent_mutex);
                if (agent.is_pondering) {
                    ponder_started = true;
                    std::cout << "    [Check] Pondering started on: " << agent.predicted_ponder_move << std::endl;
                    break;
                }
            }

            if (!ponder_started) {
                std::cout << "    [Info] Pondering did not start." << std::endl;
            }
        }

        std::cout << "  [Test] Simulation Finished." << std::endl;
    }
};

} // namespace Stockfish

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // Initialize Statics
    Stockfish::Bitboards::init();
    Stockfish::Position::init();

    Stockfish::PonderTest::run();
    return 0;
}
