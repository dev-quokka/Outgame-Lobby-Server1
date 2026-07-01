#pragma once
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <string>
#include <chrono>

constexpr uint16_t MAX_IP_LEN = 32;
constexpr uint16_t MAX_SERVER_USERS = 128;
constexpr uint16_t MAX_JWT_TOKEN_LEN = 257;

struct DataPacket {
	uint32_t dataSize;
	uint16_t connObjNum;
	DataPacket(uint32_t dataSize_, uint16_t connObjNum_) : dataSize(dataSize_), connObjNum(connObjNum_) {}
	DataPacket() = default;
};

struct PacketInfo
{
	uint16_t packetId = 0;
	uint16_t dataSize = 0;
	uint16_t connObjNum = 0;
	char* pData = nullptr;
};

struct PACKET_HEADER
{
	uint16_t PacketLength;
	uint16_t PacketId;
};


// ======================= LOBBY SERVER =======================

struct USER_LOBBY_CONNECT_REQUEST : PACKET_HEADER {
	char token[MAX_JWT_TOKEN_LEN];   // JWT 토큰
	char userId[MAX_USER_ID_LEN];
};

struct USER_LOBBY_CONNECT_RESPONSE : PACKET_HEADER {
	bool isSuccess = false;
};

// 받은 친구 요청에 대한 응답 패킷
struct FRIEND_ACTION_REQUEST : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};  // 대상 유저 ID
	uint8_t action = 0;  // 0=수락, 1=거절/삭제
};

struct FRIEND_ACTION_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false;
};


struct USER_LOGOUT_REQUEST_PACKET : PACKET_HEADER {

};

struct SYNCRONIZE_LEVEL_REQUEST : PACKET_HEADER {
	uint16_t level;
	uint16_t userPk;
	unsigned int currentExp;
};

struct SYNCRONIZE_LOGOUT_REQUEST : PACKET_HEADER {
	uint16_t userPk;
};

struct SERVER_USER_COUNTS_REQUEST : PACKET_HEADER {

};

struct SERVER_USER_COUNTS_RESPONSE : PACKET_HEADER {
	uint16_t serverCount;
	uint16_t serverUserCnt[MAX_SERVER_USERS + 1];
};

struct SHOP_DATA_REQUEST : PACKET_HEADER {

};

struct SHOP_DATA_RESPONSE : PACKET_HEADER {
	uint16_t shopItemSize;
};

struct PASS_DATA_REQUEST : PACKET_HEADER {

};

struct PASS_DATA_RESPONSE : PACKET_HEADER {
	uint16_t passDataSize;
};

struct MOVE_SERVER_REQUEST : PACKET_HEADER {
	uint16_t serverNum;
};

struct MOVE_SERVER_RESPONSE : PACKET_HEADER {
	char serverToken[MAX_JWT_TOKEN_LEN + 1];
	char ip[MAX_IP_LEN + 1];
	uint16_t port;
};

struct RAID_READY_REQUEST : PACKET_HEADER {
	char serverToken[MAX_JWT_TOKEN_LEN + 1];
	char ip[MAX_IP_LEN + 1];
	uint16_t port;
	uint16_t roomNum;
};

struct RAID_READY_FAIL : PACKET_HEADER {
	uint16_t userCenterObjNum;
	uint16_t roomNum;
};

struct RAID_RANKING_REQUEST : PACKET_HEADER {
	uint16_t startRank;
};

struct RAID_RANKING_RESPONSE : PACKET_HEADER {
	uint16_t rkCount;
	char reqScore[MAX_SCORE_SIZE + 1];
};

struct SHOP_BUY_ITEM_REQUEST : PACKET_HEADER {
	uint16_t itemCode = 0;
	uint16_t daysOrCount = 0; // [장비: 유저가 원하는 아이템의 사용 기간, 소비: 유저가 원하는 아이템 개수 묶음] 
	uint16_t itemType; // 0: 장비, 1: 소비, 2: 재료
	uint16_t position;
};


// ======================= CHANNEL SERVER =======================

struct CHANNEL_SERVER_CONNECT_REQUEST : PACKET_HEADER {
	uint16_t channelServerNum;
};

struct CHANNEL_SERVER_CONNECT_RESPONSE : PACKET_HEADER {
	bool isSuccess = false;
};

struct USER_DISCONNECT_AT_CHANNEL_REQUEST : PACKET_HEADER {
	uint16_t channelServerNum;
};

struct SYNC_EQUIPMENT_ENHANCE_REQUEST : PACKET_HEADER {
	uint16_t itemPosition;
	uint16_t enhancement;
	uint16_t userPk;
};


// ======================= MATCHING SERVER =======================

struct MATCHING_SERVER_CONNECT_REQUEST : PACKET_HEADER {

};

struct MATCHING_SERVER_CONNECT_RESPONSE : PACKET_HEADER {
	bool isSuccess = false;
};

struct MATCHING_REQUEST_TO_MATCHING_SERVER : PACKET_HEADER {
	uint16_t userPk;
	uint16_t userCenterObjNum;
	uint16_t userGroupNum;
};

struct MATCHING_RESPONSE_FROM_MATCHING_SERVER : PACKET_HEADER {
	uint16_t userCenterObjNum;
	bool isSuccess = false;
};

struct MATCHING_SUCCESS_RESPONSE_TO_CENTER_SERVER : PACKET_HEADER {
	uint16_t userCenterObjNum;
	uint16_t roomNum;
};

struct RAID_START_FAIL_REQUEST_TO_MATCHING_SERVER : PACKET_HEADER {
	uint16_t roomNum;
};

struct MATCHING_CANCEL_REQUEST : PACKET_HEADER {

};

struct MATCHING_CANCEL_RESPONSE : PACKET_HEADER {
	bool isSuccess = false;
};

struct MATCHING_CANCEL_REQUEST_TO_MATCHING_SERVER : PACKET_HEADER {
	uint16_t userCenterObjNum;
	uint16_t userGroupNum;
};

struct MATCHING_CANCEL_RESPONSE_FROM_MATCHING_SERVER : PACKET_HEADER {
	uint16_t userCenterObjNum;
	bool isSuccess = false;
};


enum class PACKET_ID : uint16_t {

	// ======================= CENTER SERVER (1~ ) =======================

	// SYSTEM (1~)
	USER_LOBBY_CONNECT_REQUEST = 1,
	USER_LOBBY_CONNECT_RESPONSE = 2,



	FRIEND_ACTION_REQUEST = 31,
	FRIEND_ACTION_RESPONSE = 32,

};