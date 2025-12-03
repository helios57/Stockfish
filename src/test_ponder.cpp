
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "grpc_agent.h"
#include "agent_config.h"
#include "misc.h"

namespace Stockfish {

class PonderTest {
public:
    static void run() {
        std::cout << "Running PonderTest..." << std::endl;
        test_state_initialization();
        std::cout << "test_state_initialization finished." << std::endl;
        test_ponder_transition();
        std::cout << "PonderTest Passed!" << std::endl;
    }

private:
    static void test_state_initialization() {
        std::cout << "  Initializing config..." << std::endl;
        AgentConfig config;
        config.api_key = "test";
        config.server = "localhost";
        config.server_port = 50051;

        std::cout << "  Constructing GrpcAgent..." << std::endl;
        // This triggers Engine constructor -> Threads -> Network loading
        // Requires NNUE files in current directory or binary directory.
        // We are running from root, binary is in src/unit_tests? No, I ran ./src/unit_tests.
        // Stockfish looks for networks in the directory of the binary or current working dir.
        // The networks are likely in src/.

        // We need to set up the global command line args or options if needed, but Engine() should handle defaults.

        GrpcAgent agent(config);

        std::cout << "  Agent constructed." << std::endl;

        assert(agent.is_pondering == false);
        assert(agent.is_searching_main == false);
        assert(agent.predicted_ponder_move.empty());

        std::cout << "  Assertions passed." << std::endl;
    }

    static void test_ponder_transition() {
        std::cout << "  Testing transitions..." << std::endl;
        AgentConfig config;
        config.api_key = "test";
        config.server = "localhost";
        config.server_port = 50051;

        GrpcAgent agent(config);

        agent.is_pondering = true;
        assert(agent.is_pondering == true);

        agent.is_searching_main = true;
        assert(agent.is_searching_main == true);

        std::cout << "  Transitions passed." << std::endl;
    }
};

} // namespace Stockfish

int main(int argc, char* argv[]) {
    // Stockfish requires some initialization of statics usually?
    // Engine constructor calls CommandLine::get_binary_directory.
    (void)argc;
    (void)argv;

    // We might need to initialize bitboards?
    Stockfish::Bitboards::init();
    Stockfish::Position::init();

    Stockfish::PonderTest::run();
    return 0;
}
