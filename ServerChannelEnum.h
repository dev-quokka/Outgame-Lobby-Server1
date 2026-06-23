#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

constexpr uint16_t CHANNEL_SERVER_START_NUMBER = 0;
constexpr uint16_t GAME_SERVER_START_NUMBER = 2;

//  =========================== SERVER INFO  ===========================

enum class ServerType : uint16_t {
	// Login Server (0)
	LoginServer = 0,

	// Lobby Server (1~)
	LobbyServer01 = 1,
	LobbyServer02 = 2
};

struct ServerAddress {
	std::string ip;
	uint16_t port;
};

inline std::unordered_map<ServerType, ServerAddress> ServerAddressMap = {
	{ ServerType::LoginServer,   { "127.0.0.1", 9001 } },
	{ ServerType::LobbyServer01, { "127.0.0.1", 9011 } },
	{ ServerType::LobbyServer02, { "127.0.0.1", 9012 } }
};