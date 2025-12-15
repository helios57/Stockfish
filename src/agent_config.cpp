#include "agent_config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <map>

namespace Stockfish {

namespace {

std::string get_env_or(const std::string& key, const std::string& default_value) {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : default_value;
}

bool to_bool(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str == "1" || str == "true" || str == "yes" || str == "on";
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) {
        return str;
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

} // namespace

AgentConfig AgentConfig::load(int argc, char* argv[]) {
    std::map<std::string, std::string> file_values;
    
    // Determine env file path from first argument (if not a flag)
    std::string env_file;
    if (argc > 1 && argv[1][0] != '-') {
        env_file = argv[1];
    } else {
        env_file = get_env_or("ENV_FILE", "agent.env");
    }
    
    // Try to open file
    std::ifstream infile(env_file);
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                file_values[key] = value;
            }
        }
    }

    auto get = [&](const std::string& key, const std::string& def) {
        // ENV var takes precedence over file value
        const char* env_val = std::getenv(key.c_str());
        if (env_val) return std::string(env_val);
        if (file_values.count(key)) return file_values[key];
        return def;
    };

    AgentConfig config;
    config.api_key = get("API_KEY", "");
    
    if (config.api_key.empty()) {
        std::cerr << "Error: API_KEY missing in " << env_file << " or environment" << std::endl;
        exit(1);
    }

    config.agent_name = get("AGENT_NAME", "StockfishAgent");
    config.agent_group = get("AGENT_GROUP", "");
    config.server = get("SERVER", "localhost");
    
    std::string port_str = get("SERVER_PORT", "443");
    config.server_port = std::atoi(port_str.c_str());
    if (config.server_port <= 0) {
        config.server_port = 443;
    }

    config.use_tls = to_bool(get("USE_TLS", "true"));
    config.game_mode = get("GAME_MODE", "TRAINING");
    // Uppercase game mode
    std::transform(config.game_mode.begin(), config.game_mode.end(), config.game_mode.begin(), ::toupper);
    
    config.time_control = get("TIME_CONTROL", "300+0");
    config.wait_for_challenge = to_bool(get("WAIT_FOR_CHALLENGE", "false"));
    config.specific_opponent_agent_id = get("SPECIFIC_OPPONENT_AGENT_ID", "");
    config.auto_accept_draw = to_bool(get("AUTO_ACCEPT_DRAW", "false"));

    // Engine strength options
    config.skill_level = std::atoi(get("SKILL_LEVEL", "20").c_str());
    config.limit_strength = to_bool(get("LIMIT_STRENGTH", "false"));
    config.elo = std::atoi(get("ELO", "1350").c_str());

    // Engine performance options
    config.hash = std::atoi(get("HASH", "16").c_str());
    config.ponder = to_bool(get("PONDER", "false"));
    config.multi_pv = std::atoi(get("MULTI_PV", "1").c_str());
    config.threads = std::atoi(get("THREADS", "1").c_str());

    // Defensive Time Management
    // Default to 1.0 (100%) if not set. Recommended for Blitz 5+0: 0.90 or 0.95
    std::string usage_mult_str = get("TIME_USAGE_MULTIPLIER", "1.0");
    config.time_usage_multiplier = std::stod(usage_mult_str);

    // Default to 0ms. Recommended for Blitz 5+0: 100 or 500 to account for network/GC lags
    config.time_safety_margin_ms = std::atoi(get("TIME_SAFETY_MARGIN_MS", "0").c_str());

    // Initialize provisioner mode fields with defaults
    config.provisioner_mode = false;
    config.target_game_id = "";
    config.overridden_elo = -1;

    // Parse command-line arguments to override configuration
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--provisioner") {
            config.provisioner_mode = true;
        }
        else if (arg == "--game-id" && i + 1 < argc) {
            config.target_game_id = argv[++i];
        }
        else if (arg == "--elo" && i + 1 < argc) {
            config.overridden_elo = std::atoi(argv[++i]);
            config.elo = config.overridden_elo; // Also update the elo field
            config.agent_name = "Stockfish-" + std::to_string(config.elo); // Set agent name based on ELO
        }
        else if (arg == "--api-key" && i + 1 < argc) {
            config.api_key = argv[++i];
        }
    }

    return config;
}

} // namespace Stockfish
