#pragma once
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <string>
#include <chrono>

#include "FailCode.h"
#include "UserTypes.h"

constexpr uint16_t MAX_IP_LEN = 32;
constexpr uint16_t MAX_SERVER_USERS = 128;
constexpr uint16_t MAX_JWT_TOKEN_LEN = 257;

constexpr uint16_t MAX_DATA_PACKET_SIZE = 256;

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


#pragma pack(push, 1)

struct PACKET_HEADER
{
	uint16_t PacketLength;
	uint16_t PacketId;
};


// ======================= LOBBY SERVER =======================

struct USER_LOBBY_CONNECT_REQUEST : PACKET_HEADER {
	char token[MAX_JWT_TOKEN_LEN] = {};   // JWT 토큰
	char userId[MAX_USER_ID_LEN] = {};
};

struct USER_LOBBY_CONNECT_RESPONSE : PACKET_HEADER {
	bool isSuccess = false;
};



// ************* FRIEND *************

// 유저 검색 패킷
struct USER_SEARCH_REQUEST : PACKET_HEADER {
	char userId[MAX_USER_ID_LEN] = {};
};

struct USER_SEARCH_RESPONSE : PACKET_HEADER {
	char     userId[MAX_USER_ID_LEN] = {};
	uint16_t userLevel = 0;
	uint8_t  onlineStatus = 0;  // 0=오프라인, 1=로비, 2=게임중
	bool     isSuccess = false;  // 유저 없으면 false
};


// 친구 요청 패킷
struct FRIEND_REQUEST_REQUEST : PACKET_HEADER {
	char targetId[MAX_USER_ID_LEN] = {};  // 요청 보낼 유저 ID
};

struct FRIEND_REQUEST_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false;
	uint8_t failCode = 0;
	// 0=성공, 1=이미 친구, 2=이미 요청중, 3=유저 없음, 4=서버오류
};


// 친구 요청 알림 패킷 (상대방이 받는 패킷)
struct FRIEND_REQUEST_NOTIFY : PACKET_HEADER {
	char     senderId[MAX_USER_ID_LEN] = {};
	uint32_t senderPk = 0; // 상태가 바뀐 친구 pk
	uint16_t senderLevel = 0;
	uint8_t  onlineStatus = 1;  // 0=오프라인, 1=로비, 2=게임중
};

// 친구 요청 수락/거부 알림 패킷
struct FRIEND_ACCEPT_NOTIFY : PACKET_HEADER {
	char     senderId[MAX_USER_ID_LEN] = {};
	uint8_t  accept = 0;  // 0=수락, 1=거절
};


// 친구 온/오프/게임중 상태 변경 알림 패킷
struct FRIEND_STATUS_NOTIFY : PACKET_HEADER {
	char    senderId[MAX_USER_ID_LEN] = {}; 
	uint8_t onlineStatus = 0;
};

// 받은 친구 요청에 대한 응답 패킷 (수락했는지 거절했는지)
struct FRIEND_ACTION_REQUEST : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};  // 대상 유저 ID
	uint8_t action = 0;  // 0=수락, 1=거절
};

struct FRIEND_ACTION_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false;
};

struct COSTUME_CHANGE_NOTIFY_TO_USER : PACKET_HEADER {
	char     userId[MAX_USER_ID_LEN] = {};
	uint32_t userPk = 0;
	uint32_t itemCode = 0;
	uint8_t  slot = 0;
};



// ************* COSTUME *************

// 코스튬 변경 요청
struct COSTUME_CHANGE_REQUEST : PACKET_HEADER {
	uint32_t itemCode = 0;  // 변경할 아이템 코드
	uint8_t  slot = 0;  // 1=head, 2=body, 3=legs, 4=feet
};

// 코스튬 변경 응답
struct COSTUME_CHANGE_RESPONSE : PACKET_HEADER {
	uint32_t itemCode = 0;
	uint8_t  slot = 0;
	uint8_t  failCode = 0; 	// 0=성공, 1=인벤에 없음, 2=잘못된 슬롯, 3=서버오류
	bool     isSuccess = false;
};

// 다른 파티원 옷 변경 전달 받는 패킷 (모든 파티원에게 전달)
struct COSTUME_CHANGE_NOTIFY : PACKET_HEADER {
	char     userId[MAX_USER_ID_LEN] = {};
	uint32_t userPk = 0;
	uint32_t itemCode = 0;
	uint8_t  slot = 0;
};



// ************* PARTY *************

// 유저 따라가기 (따라갈 유저가 피티장이되며 자동 파티 생성)
struct PARTY_FOLLOW_REQUEST : PACKET_HEADER {
	char targetId[MAX_USER_ID_LEN] = {};  // 따라갈 유저 ID
};

struct PARTY_FOLLOW_RESPONSE : PACKET_HEADER {
	uint32_t partyId = 0;
	bool     isSuccess = false; // 따라가기 성공 OR 실패 여부 반환
	uint8_t  failCode = 0; // 0=성공, 1=파티 꽉참, 2=유저 없음, 3=서버오류
};


// 친구 초대 (초대한 유저가 파티장이되며 자동 파티 생성)
struct PARTY_INVITE_REQUEST : PACKET_HEADER {
	char targetId[MAX_USER_ID_LEN] = {};  // 초대할 유저 ID
};

struct PARTY_INVITE_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false; // 초대한 유저가 초대 수락 OR 거부 반환
	uint8_t failCode = 0; // 0=성공, 1=파티 꽉참, 2=유저 없음, 3=서버오류
};


// 초대 알림 (초대 받은 유저에게 알림)
struct PARTY_INVITE_NOTIFY : PACKET_HEADER {
	char     senderId[MAX_USER_ID_LEN] = {};  
	uint16_t senderLevel = 0;        
	uint32_t partyId = 0;     
	uint8_t  memberCount = 0;  
};


// 초대 수락/거절
struct PARTY_INVITE_ACCEPT_REQUEST : PACKET_HEADER {
	char    senderId[MAX_USER_ID_LEN] = {};  // 초대한 유저 ID
	uint8_t accept = 0;  // 0=수락, 1=거절
};

struct PARTY_INVITE_ACCEPT_RESPONSE : PACKET_HEADER {
	uint32_t partyId = 0;
	bool     isSuccess = false;
	uint8_t  failCode = 0;
};

// 파티 초대 거절했다는 알림
struct PARTY_INVITE_REJECT_NOTIFY : PACKET_HEADER {
	char senderId[MAX_USER_ID_LEN] = {};  // 거절한 유저 ID
};

// 새로 들어오는 파티 유저에게 기존 파티 정보 전달
struct PARTY_INFO_PACKET : PACKET_HEADER {
	uint32_t partyId = 0;
	uint32_t leaderPk = 0;
	uint8_t  memberCount = 0;

	struct PartyMember {
		char     userId[MAX_USER_ID_LEN] = {};
		uint32_t userPk = 0;
		uint16_t userLevel = 0;
		
		// 코스튬
		uint32_t head = 0;
		uint32_t body = 0;
		uint32_t legs = 0;
		uint32_t feet = 0;
	} members[4];
};

// 새 멤버 입장 알림 (기존 파티원들에게)
struct PARTY_JOIN_NOTIFY : PACKET_HEADER {
	char     userId[MAX_USER_ID_LEN] = {};
	uint32_t partyId = 0; 
	uint32_t userPk = 0;
	uint16_t userLevel = 0; 
	uint32_t head = 0;
	uint32_t body = 0;
	uint32_t legs = 0;
	uint32_t feet = 0;
};


struct PARTY_LEAVE_REQUEST : PACKET_HEADER {
	// 별도 데이터 없음 (본인이 나가는 요청)
};

struct PARTY_LEAVE_RESPONSE : PACKET_HEADER {
	bool    isSuccess = false;
	uint8_t failCode = 0;
};


// 파티원들에게 알림
struct PARTY_LEAVE_NOTIFY : PACKET_HEADER {
	uint32_t userPk = 0;  // 나간 유저 pk
	uint32_t newLeaderPk = 0;  // 새 파티장 pk (파티장이 나갔을 때만, 아니면 0)
};


// 파티 강퇴 요청 (파티장만 가능)
struct PARTY_KICK_REQUEST : PACKET_HEADER {
	char targetId[MAX_USER_ID_LEN] = {};  // 강퇴할 유저 ID
};

// 강퇴 결과
struct PARTY_KICK_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false;
	uint8_t failCode = 0;
};

struct PARTY_KICK_NOTIFY : PACKET_HEADER {
	char kickedUserId[MAX_USER_ID_LEN] = {};
};


// 파티장 위임 요청
struct PARTY_DELEGATE_REQUEST : PACKET_HEADER {
	char targetId[MAX_USER_ID_LEN] = {};  // 새 파티장이 될 유저 ID
};

// 파티장 위임 결과
struct PARTY_DELEGATE_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false;
	uint8_t failCode = 0;
};

// 파티원들에게 새 파티장 알림
struct PARTY_DELEGATE_NOTIFY : PACKET_HEADER {
	uint32_t newLeaderPk = 0;  // 새 파티장 pk
};


// 파티원 온라인/오프라인 상태 알림
struct PARTY_MEMBER_STATUS_NOTIFY : PACKET_HEADER {
	uint32_t userPk = 0;
	uint32_t partyId = 0;
	uint8_t  onlineStatus = 0;  // 0=오프라인, 1=온라인
};





struct MATCH_START_REQUEST : PACKET_HEADER {
	// 별도 데이터 없음
};

struct MATCH_START_RESPONSE : PACKET_HEADER {
	bool    isSuccess = false;
	uint8_t failCode = 0;
	// 0=성공, 1=파티장 아님, 2=서버오류
};

#pragma pack(pop)


enum class PACKET_ID : uint16_t {

	// ======================= LOBBY SERVER (21~ ) =======================

	USER_LOBBY_CONNECT_REQUEST = 21,
	USER_LOBBY_CONNECT_RESPONSE = 22,


	USER_SEARCH_REQUEST = 25,
	USER_SEARCH_RESPONSE = 26,


	// ************* FRIEND *************

	FRIEND_REQUEST_REQUEST = 30,
	FRIEND_REQUEST_RESPONSE = 31,
	FRIEND_REQUEST_NOTIFY = 32,

	FRIEND_STATUS_NOTIFY = 33,
	FRIEND_ACCEPT_NOTIFY = 34,

	FRIEND_ACTION_REQUEST = 35,
	FRIEND_ACTION_RESPONSE = 36,


	// ************* COSTUME *************

	COSTUME_CHANGE_REQUEST = 51,
	COSTUME_CHANGE_RESPONSE = 52,

	COSTUME_CHANGE_NOTIFY = 55,


	// ************* PARTY *************

	PARTY_FOLLOW_REQUEST = 101,
	PARTY_FOLLOW_RESPONSE = 102,

	PARTY_INVITE_REQUEST = 105,
	PARTY_INVITE_RESPONSE = 106,

	PARTY_INVITE_NOTIFY = 110,

	PARTY_INVITE_ACCEPT_REQUEST = 111,
	PARTY_INVITE_ACCEPT_RESPONSE = 112,
	PARTY_INVITE_REJECT_NOTIFY = 113,

	PARTY_JOIN_NOTIFY = 114,
	PARTY_INFO_PACKET = 115,

	PARTY_LEAVE_REQUEST = 121,
	PARTY_LEAVE_RESPONSE = 122,
	
	PARTY_LEAVE_NOTIFY = 124,

	PARTY_KICK_REQUEST = 131,
	PARTY_KICK_RESPONSE = 132,
	PARTY_KICK_NOTIFY = 134,

	PARTY_DELEGATE_REQUEST = 141,
	PARTY_DELEGATE_RESPONSE = 142,
	PARTY_DELEGATE_NOTIFY = 143,

	PARTY_MEMBER_STATUS_NOTIFY = 145,


	MATCH_START_REQUEST = 201,
	MATCH_START_RESPONSE = 202,
};