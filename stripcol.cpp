#include "pch.h"
#include "EuroScopePlugIn.h"
#include <string>
#include <cstdio>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <set>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <random>
#include <fstream>
#include <queue>
#include <variant>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "EuroScopePlugInDll.lib")

using namespace EuroScopePlugIn;
static CRadarScreen* g_pScreen = nullptr;

// ==================== HELPER CLASSES ====================

class JsonBuilder {
private:
	std::ostringstream ss;
	bool first = true;

public:
	JsonBuilder() { ss << "{"; }

	static std::string Escape(const std::string& str) {
		std::string result;
		result.reserve(static_cast<size_t>(str.length() * 1.1));
		for (char c : str) {
			switch (c) {
			case '"':  result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\b': result += "\\b"; break;
			case '\f': result += "\\f"; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
					char buf[7];
					snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
					result += buf;
				}
				else result += c;
			}
		}
		return result;
	}

	void Add(const std::string& key, const std::string& value) {
		if (!first) ss << ",";
		ss << "\"" << key << "\":\"" << Escape(value) << "\"";
		first = false;
	}

	void Add(const std::string& key, int value) {
		if (!first) ss << ",";
		ss << "\"" << key << "\":" << value;
		first = false;
	}

	void Add(const std::string& key, double value, int precision = 2) {
		if (!first) ss << ",";
		ss << "\"" << key << "\":" << std::fixed << std::setprecision(precision) << value;
		ss.unsetf(std::ios_base::fixed);
		ss << std::setprecision(6);
		first = false;
	}

	void AddRaw(const std::string& key, const std::string& rawJson) {
		if (!first) ss << ",";
		ss << "\"" << key << "\":" << rawJson;
		first = false;
	}

	std::string Build() {
		ss << "}";
		return ss.str();
	}
};

// ==================== MAIN PLUGIN CLASS ====================

class StripCol : public CPlugIn {
private:
	// WebSocket client state
	SOCKET wsSocket = INVALID_SOCKET;
	std::atomic<bool> wsConnected{ false };
	std::atomic<bool> running{ false };
	std::thread wsThread;
	std::mutex wsMutex;
	std::mutex gatewayMutex;
	std::string gatewayAddress = "127.0.0.1";

	// Pairing code
	std::string pairingCode;
	// Thread-safe storage for registration message
	std::string registrationMessage;
	std::string lastRegistrationMessage;
	std::mutex codeMutex;


	// Aircraft tracking
	std::unordered_set<std::string> assumedAircraft;
	std::map<std::string, std::string> lastAircraftData;
	std::string lastAtcListJson;
	std::string lastRegistrationJson;
	std::mutex aircraftMutex;

	std::unordered_set<std::string> transferToMeAircraft;
	std::mutex transferMutex;

	// Command Queue for Main Thread (Euroscope API is not thread-safe)
	struct PendingTask {
		std::string type;
		std::string json;
	};
	std::queue<PendingTask> taskQueue;
	std::mutex queueMutex;

public:
	StripCol()
		: CPlugIn(
			EuroScopePlugIn::COMPATIBILITY_CODE,
			"StripCol",
			"2.3",
			"Simplezes",
			"Copyright 2026") {
		CheckGatewayAvailability();
	}

	~StripCol() override {
		DisconnectFromGateway();
	}

private:
	std::string GetGatewayAddress() {
		std::lock_guard<std::mutex> lock(gatewayMutex);
		return gatewayAddress;
	}
	// ==================== WEBSOCKET CLIENT ====================

	void CheckGatewayAvailability() {
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			LogMessage("WSAStartup failed");
			return;
		}

		struct addrinfo hints {}, * res = nullptr;
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		std::string addr = GetGatewayAddress();
		if (getaddrinfo(addr.c_str(), "3000", &hints, &res) != 0) {
			LogMessage("DNS resolution failed for " + addr);
			WSACleanup();
			return;
		}

		SOCKET testSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (testSocket == INVALID_SOCKET) {
			LogMessage("Socket creation failed");
			freeaddrinfo(res);
			WSACleanup();
			return;
		}

		// Set non-blocking for quick timeout
		u_long mode = 1;
		ioctlsocket(testSocket, FIONBIO, &mode);

		connect(testSocket, res->ai_addr, (int)res->ai_addrlen);
		freeaddrinfo(res);

		fd_set writefds;
		FD_ZERO(&writefds);
		FD_SET(testSocket, &writefds);

		timeval timeout{ 1, 0 }; // 1 second timeout
		int result = select(0, NULL, &writefds, NULL, &timeout);

		closesocket(testSocket);
		WSACleanup();

		if (result > 0) {
			LogMessage("Gateway available at " + addr);
		}
		else {
			LogMessage("Gateway not available - will retry on position connect");
		}
	}

	std::string GeneratePairingCode() {
		const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
		std::string code;

		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> dis(0, 35);

		for (int i = 0; i < 5; i++) {
			code += chars[dis(gen)];
		}

		return code;
	}

	void ConnectToGateway() {
		LogMessage("ConnectToGateway: Preparing fresh connection...");
		DisconnectFromGateway();

		wsConnected = false;
		lastRegistrationMessage.clear();

		std::string addr = GetGatewayAddress();
		LogMessage("Attempting connection to: " + addr + ":3000");

		CController myself = ControllerMyself();
		if (myself.IsValid()) {

			JsonBuilder json;
			json.Add("type", "register");

			{
				std::lock_guard<std::mutex> lock(codeMutex);
				if (pairingCode.empty()) {
					pairingCode = GeneratePairingCode();
					std::string message = "StripCol Pairing Code: " + pairingCode;
					DisplayUserMessage("StripCol", "Pairing", message.c_str(), true, true, false, false, false);
				}
				json.Add("code", pairingCode);
			}

			json.Add("callsign", myself.GetCallsign());
			json.Add("name", myself.GetFullName());
			json.Add("facility", myself.GetFacility());
			json.Add("rating", myself.GetRating());
			json.Add("positionId", myself.GetPositionId());

			char freqStr[16];
			snprintf(freqStr, sizeof(freqStr), "%.3f", myself.GetPrimaryFrequency());
			json.Add("frequency", freqStr);

			registrationMessage = json.Build();
		}

		running = true;
		wsThread = std::thread(&StripCol::WebSocketClientThread, this);

		LogMessage("WebSocket thread started.");
	}

	void DisconnectFromGateway() {
		LogMessage("DisconnectFromGateway: Starting shutdown...");

		running = false;
		wsConnected = false;

		{
			std::lock_guard<std::mutex> lock(wsMutex);
			if (wsSocket != INVALID_SOCKET) {
				shutdown(wsSocket, SD_BOTH);
				closesocket(wsSocket);
				wsSocket = INVALID_SOCKET;
			}
		}

		if (wsThread.joinable()) {
			wsThread.join();
		}

		LogMessage("Gateway shutdown complete.");
	}

	void WebSocketClientThread() {
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			LogMessage("WSAStartup failed");
			running = false;
			return;
		}

		auto lastPing = std::chrono::steady_clock::now();

		while (running) {
			if (!AttemptWebSocketConnection()) {
				std::this_thread::sleep_for(std::chrono::seconds(5));
				continue;
			}

			SendRegistration();

			while (running && wsConnected) {
				ReceiveMessages();

				auto now = std::chrono::steady_clock::now();
				if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPing).count() >= 30) {
					SendWebSocketMessage("{\"type\":\"ping\"}");
					lastPing = now;
				}

				if (running && wsConnected) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			}

			wsConnected = false;
			{
				std::lock_guard<std::mutex> lock(wsMutex);
				if (wsSocket != INVALID_SOCKET) {
					closesocket(wsSocket);
					wsSocket = INVALID_SOCKET;
				}
			}

			if (running) {
				LogMessage("Connection lost, retrying...");
				std::this_thread::sleep_for(std::chrono::seconds(5));
			}
		}

		WSACleanup();
	}

	std::string Base64Encode(const std::vector<unsigned char>& data) {
		static const char error_str[] = "Error";
		static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		std::string out;
		out.reserve(((data.size() + 2) / 3) * 4);

		int val = 0;
		int valb = -6;

		for (unsigned char c : data) {
			val = (val << 8) + c;
			valb += 8;
			while (valb >= 0) {
				out.push_back(lookup[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}

		if (valb > -6) out.push_back(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
		while (out.size() % 4) out.push_back('=');

		return out;
	}

	std::string GenerateWebSocketKey() {
		std::vector<unsigned char> nonce(16);
		for (int i = 0; i < 16; i++) {
			nonce[i] = rand() % 256;
		}
		return Base64Encode(nonce);
	}

	bool AttemptWebSocketConnection() {
		LogMessage("AttemptWebSocketConnection: Starting...");
		struct addrinfo hints {}, * res = nullptr;
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		std::string addr = GetGatewayAddress();
		if (getaddrinfo(addr.c_str(), "3000", &hints, &res) != 0) {
			LogMessage("DNS resolution failed for " + addr);
			return false;
		}

		SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sock == INVALID_SOCKET) {
			LogMessage("Socket creation failed");
			freeaddrinfo(res);
			return false;
		}

		LogMessage("Connecting to " + addr + ":3000...");
		if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
			LogMessage("Connect failed: " + std::to_string(WSAGetLastError()));
			freeaddrinfo(res);
			closesocket(sock);
			return false;
		}
		freeaddrinfo(res);
		LogMessage("Socket connected to server.");

		std::string key = GenerateWebSocketKey();

		std::string handshake =
			"GET /api HTTP/1.1\r\n"
			"Host: " + addr + ":3000\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Origin: http://" + addr + "\r\n"
			"Sec-WebSocket-Key: " + key + "\r\n"
			"Sec-WebSocket-Version: 13\r\n\r\n";

		LogMessage("Sending handshake request for path /api...");
		if (send(sock, handshake.c_str(), (int)handshake.size(), 0) == SOCKET_ERROR) {
			LogMessage("Send failed");
			closesocket(sock);
			return false;
		}

		LogMessage("Waiting for handshake response...");

		DWORD timeout = 5000; // 5 seconds
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		char buffer[2048];
		int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
		if (bytesRead <= 0) {
			LogMessage("Handshake failed: recv returned " + std::to_string(bytesRead) + " (Error: " + std::to_string(WSAGetLastError()) + ")");
			closesocket(sock);
			return false;
		}
		LogMessage("Handshake recv successful, bytes: " + std::to_string(bytesRead));

		buffer[bytesRead] = '\0';
		std::string response(buffer);

		std::istringstream iss(response);
		std::string line;
		int lineCount = 0;
		while (std::getline(iss, line) && lineCount < 5) {
			if (!line.empty() && line[line.length() - 1] == '\r') line.erase(line.length() - 1);
			LogMessage("RX: " + line);
			lineCount++;
		}

		if (response.find("101") == std::string::npos) {
			LogMessage("Handshake failed: 101 not found.");
			closesocket(sock);
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(wsMutex);
			wsSocket = sock;
		}

		int flag = 1;
		setsockopt(wsSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

		wsConnected = true;

		SendWebSocketMessage("{\"type\":\"ping\"}");

		LogMessage("WebSocket connected successfully.");
		return true;
	}

	void SendRegistration() {
		if (registrationMessage.empty()) {
			return;
		}

		SendWebSocketMessage(registrationMessage);
		lastRegistrationMessage = registrationMessage;
	}

	void SendWebSocketMessage(const std::string& message) {
		std::lock_guard<std::mutex> lock(wsMutex);
		if (wsSocket == INVALID_SOCKET || !wsConnected) return;

		size_t msgLen = message.size();
		std::vector<unsigned char> frame;

		frame.push_back(0x81);

		// Generate mask key
		unsigned char maskKey[4];
		for (int i = 0; i < 4; i++) {
			maskKey[i] = rand() % 256;
		}

		// Payload length
		if (msgLen < 126) {
			frame.push_back(0x80 | (unsigned char)msgLen);
		}
		else if (msgLen < 65536) {
			frame.push_back(0x80 | 126);
			frame.push_back((msgLen >> 8) & 0xFF);
			frame.push_back(msgLen & 0xFF);
		}
		else {
			return;
		}

		for (int i = 0; i < 4; i++) {
			frame.push_back(maskKey[i]);
		}

		for (size_t i = 0; i < msgLen; i++) {
			frame.push_back(message[i] ^ maskKey[i % 4]);
		}

		if (send(wsSocket, (const char*)frame.data(), (int)frame.size(), 0) == SOCKET_ERROR) {
			LogMessage("SendWebSocketMessage failed: " + std::to_string(WSAGetLastError()));
			wsConnected = false;
		}
	}

	void ReceiveMessages() {
		char buffer[4096];

		fd_set readfds;
		FD_ZERO(&readfds);

		{
			std::lock_guard<std::mutex> lock(wsMutex);
			if (wsSocket == INVALID_SOCKET) return;
			FD_SET(wsSocket, &readfds);
		}

		timeval timeout{ 1, 0 };
		int activity = select(0, &readfds, NULL, NULL, &timeout);

		if (activity > 0) {
			int bytesRead;
			{
				std::lock_guard<std::mutex> lock(wsMutex);
				if (wsSocket == INVALID_SOCKET) return;
				bytesRead = recv(wsSocket, buffer, sizeof(buffer) - 1, 0);
			}

			if (bytesRead <= 0) {
				wsConnected = false;
				LogMessage("Connection closed by server");
				return;
			}

			if (bytesRead >= 2) {
				unsigned char opcode = buffer[0] & 0x0F;

				if (opcode == 0x08) {
					wsConnected = false;
					return;
				}
				else if (opcode == 0x01) {
					unsigned char payloadLen = buffer[1] & 0x7F;
					int payloadStart = 2;

					if (payloadLen == 126) {
						payloadLen = (buffer[2] << 8) | buffer[3];
						payloadStart = 4;
					}

					if (payloadStart + payloadLen <= bytesRead) {
						std::string message(buffer + payloadStart, payloadLen);
						HandleCommand(message);
					}
				}
			}
		}
	}

	void HandleCommand(const std::string& message) {
		try {
			size_t typePos = message.find("\"type\"");
			if (typePos == std::string::npos) return;

			size_t colonPos = message.find(':', typePos);
			if (colonPos == std::string::npos) return;

			size_t quoteStart = message.find('"', colonPos);
			if (quoteStart == std::string::npos) return;

			size_t quoteEnd = message.find('"', quoteStart + 1);
			if (quoteEnd == std::string::npos) return;

			std::string type = message.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

			// Queue the task for the main thread
			{
				std::lock_guard<std::mutex> lock(queueMutex);
				taskQueue.push({ type, message });
			}
		}
		catch (...) {
			LogMessage("Error parsing command");
		}
	}

	void ProcessPendingTasks() {
		std::lock_guard<std::mutex> lock(queueMutex);
		while (!taskQueue.empty()) {
			PendingTask task = taskQueue.front();
			taskQueue.pop();

			static const std::unordered_map<std::string, void(StripCol::*)(const std::string&)> handlers = {
				{"set-cleared-alt", &StripCol::HandleSetClearedAltitude},
				{"set-assigned-heading", &StripCol::HandleSetAssignedHeading},
				{"set-assigned-speed", &StripCol::HandleSetAssignedSpeed},
				{"set-final-alt", &StripCol::HandleSetFinalAltitude},
				{"accept-handoff", &StripCol::HandleAcceptHandoff},
				{"end-tracking", &StripCol::HandleEndTracking},
				{"set-squawk", &StripCol::HandleSetSquawk},
				{"set-departureTime", &StripCol::HandleSetDepartureTime},
				{"set-direct-point", &StripCol::HandleSetDirectPoint},
				{"set-sid", &StripCol::HandleSetSid},
				{"set-star", &StripCol::HandleSetStar},
				{"set-assigned-mach", &StripCol::HandleSetAssignedMach},
				{"refuse-handoff", &StripCol::HandleRefuseHandoff},
				{"ATC-transfer", &StripCol::HandleAtcTransfer},
				{"sync", &StripCol::HandleSync},
				{"assume-aircraft", &StripCol::HandleAssumeAircraft},
				{"get-nearby-aircraft", &StripCol::HandleGetNearbyAircraft}
			};

			auto it = handlers.find(task.type);
			if (it != handlers.end()) {
				try {
					(this->*(it->second))(task.json);
				}
				catch (...) {
					LogMessage("Exception in handler: " + task.type);
				}
			}
		}
	}

	std::string GetJsonValue(const std::string& json, const std::string& key) {
		size_t keyPos = json.find("\"" + key + "\"");
		if (keyPos == std::string::npos) return "";

		size_t colonPos = json.find(':', keyPos);
		if (colonPos == std::string::npos) return "";

		size_t valueStart = json.find('"', colonPos);
		if (valueStart == std::string::npos) return "";

		size_t valueEnd = json.find('"', valueStart + 1);
		if (valueEnd == std::string::npos) return "";

		return json.substr(valueStart + 1, valueEnd - valueStart - 1);
	}

	// ==================== COMMAND HANDLERS ====================

	void HandleSetClearedAltitude(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string altStr = GetJsonValue(json, "clearedAltitude");

		if (callsign.empty() || altStr.empty()) return;

		try {
			int altitude = std::stoi(altStr);
			CFlightPlan fp = FlightPlanSelect(callsign.c_str());
			if (fp.IsValid()) {
				fp.GetControllerAssignedData().SetClearedAltitude(altitude);
			}
		}
		catch (...) {}
	}

	void HandleSetAssignedHeading(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string headingStr = GetJsonValue(json, "assignedHeading");

		if (callsign.empty() || headingStr.empty()) return;

		try {
			int heading = std::stoi(headingStr);
			CFlightPlan fp = FlightPlanSelect(callsign.c_str());
			if (fp.IsValid()) {
				fp.GetControllerAssignedData().SetAssignedHeading(heading);
			}
		}
		catch (...) {}
	}

	void HandleSetAssignedSpeed(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string speedStr = GetJsonValue(json, "assignedSpeed");

		if (callsign.empty() || speedStr.empty()) return;

		try {
			int speed = std::stoi(speedStr);
			CFlightPlan fp = FlightPlanSelect(callsign.c_str());
			if (fp.IsValid()) {
				if (speed >= 0 && speed <= 500) {
					fp.GetControllerAssignedData().SetAssignedSpeed(speed);
				}
			}
		}
		catch (...) {}
	}

	void HandleSetFinalAltitude(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string altStr = GetJsonValue(json, "finalAltitude");

		if (callsign.empty() || altStr.empty()) return;

		try {
			int altitude = std::stoi(altStr);
			CFlightPlan fp = FlightPlanSelect(callsign.c_str());
			if (fp.IsValid()) {
				fp.GetControllerAssignedData().SetFinalAltitude(altitude);
			}
		}
		catch (...) {}
	}

	void HandleAcceptHandoff(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		if (callsign.empty()) return;

		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (fp.IsValid()) {
			fp.AcceptHandoff();
		}
	}

	void HandleEndTracking(const std::string& jsonStr) {
		std::string callsign = GetJsonValue(jsonStr, "callsign");
		if (callsign.empty()) return;

		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (fp.IsValid()) {
			fp.EndTracking();
		}

		{
			std::lock_guard<std::mutex> lock(aircraftMutex);
			assumedAircraft.erase(callsign);
			lastAircraftData.erase(callsign);
		}

		JsonBuilder json;
		json.Add("type", "release");
		json.Add("callsign", callsign);
		SendWebSocketMessage(json.Build());
	}

	void HandleSetSquawk(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string squawkStr = GetJsonValue(json, "squawk");

		if (callsign.empty() || squawkStr.empty()) return;

		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (fp.IsValid()) {
			fp.GetControllerAssignedData().SetSquawk(squawkStr.c_str());
		}
	}

	void HandleSetDepartureTime(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string dTimeStr = GetJsonValue(json, "Dtime");

		if (callsign.empty() || dTimeStr.empty()) return;

		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (fp.IsValid()) {
			fp.GetFlightPlanData().SetActualDepartureTime(dTimeStr.c_str());
		}
	}

	void HandleSetAssignedMach(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string machStr = GetJsonValue(json, "assignedMach");

		if (callsign.empty() || machStr.empty()) return;

		try {
			double mach = std::stod(machStr);
			int machInt = static_cast<int>(mach * 100.0);
			CFlightPlan fp = FlightPlanSelect(callsign.c_str());
			if (fp.IsValid()) {
				fp.GetControllerAssignedData().SetAssignedMach(machInt);
			}
		}
		catch (...) {}
	}

	void HandleSetDirectPoint(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string pointName = GetJsonValue(json, "pointName");

		if (callsign.empty() || pointName.empty()) return;

		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (fp.IsValid()) {
			int pointsCount = fp.GetExtractedRoute().GetPointsNumber();
			for (int i = 0; i < pointsCount; i++) {
				const char* currentPointName = fp.GetExtractedRoute().GetPointName(i);
				if (currentPointName && strcmp(currentPointName, pointName.c_str()) == 0) {
					fp.GetControllerAssignedData().SetDirectToPointName(pointName.c_str());
					break;
				}
			}
		}
	}

	void HandleSetSid(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string sid = GetJsonValue(json, "sid");
		std::string runway = GetJsonValue(json, "runway");

		if (callsign.empty() || sid.empty()) return;

		std::string msg;
		SetSidForFlight(callsign, sid, runway, msg);
	}

	void HandleSetStar(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string star = GetJsonValue(json, "star");
		std::string runway = GetJsonValue(json, "runway");

		if (callsign.empty() || star.empty()) return;

		std::string msg;
		SetStarForFlight(callsign, star, runway, msg);
	}

	void HandleRefuseHandoff(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		if (callsign.empty()) return;

		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (fp.IsValid()) {
			fp.RefuseHandoff();
		}
	}

	void HandleAtcTransfer(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		std::string targetATC = GetJsonValue(json, "targetATC");

		if (callsign.empty() || targetATC.empty()) return;

		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (fp.IsValid()) {
			fp.InitiateHandoff(targetATC.c_str());
		}
	}

	void HandleAssumeAircraft(const std::string& json) {
		std::string callsign = GetJsonValue(json, "callsign");
		if (callsign.empty()) return;

		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (fp.IsValid()) {
			if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) {
				fp.StartTracking();
			}
		}
	}

	void HandleGetNearbyAircraft(const std::string& json) {
		std::vector<std::string> callsigns;
		CController myself = ControllerMyself();
		if (!myself.IsValid()) return;

		CPosition myPos = myself.GetPosition();

		CFlightPlan fp = FlightPlanSelectFirst();
		while (fp.IsValid()) {
			if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED &&
				fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_REDUNDANT) {

				CRadarTarget rt = RadarTargetSelect(fp.GetCallsign());
				if (rt.IsValid()) {
					double dist = myPos.DistanceTo(rt.GetPosition().GetPosition());
					if (dist <= 50.0) {
						callsigns.push_back(fp.GetCallsign());
					}
				}
			}
			fp = FlightPlanSelectNext(fp);
		}

		std::ostringstream ss;
		ss << "{\"type\":\"nearby-aircraft\",\"data\":[";
		for (size_t i = 0; i < callsigns.size(); ++i) {
			ss << "\"" << JsonBuilder::Escape(callsigns[i]) << "\"";
			if (i < callsigns.size() - 1) ss << ",";
		}
		ss << "]}";
		SendWebSocketMessage(ss.str());
	}



	void HandleSync(const std::string& json) {
		SendRegistration();

		lastAtcListJson = "";
		SendAtcList(true);

		std::lock_guard<std::mutex> lock(aircraftMutex);
		lastAircraftData.clear();
		for (const auto& callsign : assumedAircraft) {
			CFlightPlan fp = FlightPlanSelect(callsign.c_str());
			if (fp.IsValid()) {
				std::string acJson = BuildAircraftJson(fp);
				JsonBuilder update;
				update.Add("type", "aircraft");
				update.AddRaw("data", acJson);
				SendWebSocketMessage(update.Build());
				lastAircraftData[callsign] = acJson;
			}
		}
	}

	void SendAtcList(bool force = false) {
		std::ostringstream ss;
		ss << "{\"type\":\"atclist\",\"data\":[";
		bool first = true;

		CController ctrl = ControllerSelectFirst();
		while (ctrl.IsValid()) {
			if (!first) ss << ",";
			first = false;

			double freq = ctrl.GetPrimaryFrequency();
			char freqBuf[16];
			snprintf(freqBuf, sizeof(freqBuf), "%.3f", freq);

			JsonBuilder j;
			j.Add("callsign", ctrl.GetCallsign());
			j.Add("positionId", ctrl.GetPositionId());
			j.Add("fullName", ctrl.GetFullName());
			j.Add("rating", ctrl.GetRating());
			j.Add("frequency", freqBuf);
			ss << j.Build();

			ctrl = ControllerSelectNext(ctrl);
		}
		ss << "]}";
		std::string finalJson = ss.str();

		if (force || finalJson != lastAtcListJson) {
			SendWebSocketMessage(finalJson);
			lastAtcListJson = finalJson;
		}
	}

private:
	bool IsProcedurePattern(const std::string& segment) {
		bool hasNumbers = segment.find_first_of("0123456789") != std::string::npos;
		bool hasLetters = segment.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos;
		return hasNumbers && hasLetters;
	}

	bool SetSidForFlight(const std::string& callsign, const std::string& sidName, const std::string& runway, std::string& messageOut) {
		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (!fp.IsValid()) return false;
		if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) return false;

		CFlightPlanData fpd = fp.GetFlightPlanData();
		std::string route = fpd.GetRoute() ? fpd.GetRoute() : "";

		size_t firstSpace = route.find(' ');
		if (firstSpace != std::string::npos) {
			std::string firstSegment = route.substr(0, firstSpace);
			if (IsProcedurePattern(firstSegment)) {
				size_t slashPos = firstSegment.find('/');
				if (slashPos != std::string::npos) {
					route = route.substr(firstSpace + 1);
				}
				else {
					size_t secondSpace = route.find(' ', firstSpace + 1);
					if (secondSpace != std::string::npos) {
						std::string secondSegment = route.substr(firstSpace + 1, secondSpace - firstSpace - 1);
						if (IsProcedurePattern(secondSegment)) {
							route = route.substr(secondSpace + 1);
						}
						else {
							route = route.substr(firstSpace + 1);
						}
					}
					else {
						route = route.substr(firstSpace + 1);
					}
				}
				size_t firstValidChar = route.find_first_not_of(" ,");
				if (firstValidChar != std::string::npos) route = route.substr(firstValidChar);
				else route.clear();
			}
		}

		std::string fullSid = sidName + "/" + runway;
		route = route.empty() ? fullSid : fullSid + " " + route;

		if (fpd.SetRoute(route.c_str())) {
			return fpd.AmendFlightPlan();
		}
		return false;
	}

	bool SetStarForFlight(const std::string& callsign, const std::string& starName, const std::string& runway, std::string& messageOut) {
		CFlightPlan fp = FlightPlanSelect(callsign.c_str());
		if (!fp.IsValid()) return false;
		if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) return false;

		CFlightPlanData fpd = fp.GetFlightPlanData();
		std::string route = fpd.GetRoute() ? fpd.GetRoute() : "";

		size_t lastSpace = route.find_last_of(' ');
		if (lastSpace != std::string::npos) {
			std::string lastSegment = route.substr(lastSpace + 1);
			if (IsProcedurePattern(lastSegment)) {
				size_t slashPos = lastSegment.find('/');
				if (slashPos != std::string::npos) {
					route = route.substr(0, lastSpace);
				}
				else {
					size_t prevSpace = route.find_last_of(' ', lastSpace - 1);
					if (prevSpace != std::string::npos) {
						std::string prevSegment = route.substr(prevSpace + 1, lastSpace - prevSpace - 1);
						if (IsProcedurePattern(prevSegment)) route = route.substr(0, prevSpace);
						else route = route.substr(0, lastSpace);
					}
					else route = route.substr(0, lastSpace);
				}
				size_t lastValidChar = route.find_last_not_of(" ,");
				if (lastValidChar != std::string::npos) route = route.substr(0, lastValidChar + 1);
				else route.clear();
			}
		}

		std::string fullStar = starName + "/" + runway;
		route = route.empty() ? fullStar : route + " " + fullStar;

		if (fpd.SetRoute(route.c_str())) {
			return fpd.AmendFlightPlan();
		}
		return false;
	}

	// ==================== FLIGHT PLAN MONITORING ====================

	void CheckAllFlightPlans() {
		CFlightPlan fp = FlightPlanSelectFirst();
		while (fp.IsValid()) {
			std::string callsign = fp.GetCallsign();
			int state = fp.GetState();

			HandleTransferState(fp, callsign, state);
			HandleAssumedState(fp, callsign, state);

			fp = FlightPlanSelectNext(fp);
		}
	}

	void HandleTransferState(CFlightPlan& fp, const std::string& callsign, int state) {
		std::lock_guard<std::mutex> lock(transferMutex);
		bool inSet = (transferToMeAircraft.find(callsign) != transferToMeAircraft.end());
		bool isTransferring = (state == EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED);

		if (isTransferring && !inSet) {
			transferToMeAircraft.insert(callsign);

			JsonBuilder json;
			json.Add("type", "transfer");
			json.AddRaw("data", BuildAircraftJson(fp));
			SendWebSocketMessage(json.Build());
		}
		else if (!isTransferring && inSet) {
			transferToMeAircraft.erase(callsign);
		}
	}

	void HandleAssumedState(CFlightPlan& fp, const std::string& callsign, int state) {
		std::lock_guard<std::mutex> lock(aircraftMutex);
		bool inSet = (assumedAircraft.find(callsign) != assumedAircraft.end());
		bool isAssumed = (state == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED);

		if (isAssumed && !inSet) {
			assumedAircraft.insert(callsign);

			JsonBuilder json;
			json.Add("type", "aircraft");
			json.AddRaw("data", BuildAircraftJson(fp));
			SendWebSocketMessage(json.Build());
		}
		else if (!isAssumed && inSet) {
			assumedAircraft.erase(callsign);

			JsonBuilder json;
			json.Add("type", "release");
			json.Add("callsign", callsign);
			SendWebSocketMessage(json.Build());

			lastAircraftData.erase(callsign);
		}
	}

	// ==================== UTILITY FUNCTIONS ====================

	void LogMessage(const std::string& message) {
		OutputDebugStringA(("[StripCol] " + message + "\n").c_str());
	}

	std::string BuildAircraftJson(const CFlightPlan& fp) {
		if (!fp.IsValid()) return "{}";

		auto data = fp.GetFlightPlanData();
		auto assignedData = fp.GetControllerAssignedData();
		auto route = fp.GetExtractedRoute();

		JsonBuilder json;
		json.Add("callsign", fp.GetCallsign());
		json.Add("aircraftType", data.GetAircraftFPType());
		json.Add("groundSpeed", data.GetTrueAirspeed());
		json.Add("departure", data.GetOrigin());
		json.Add("arrival", data.GetDestination());
		json.Add("squawk", assignedData.GetSquawk());
		json.Add("atd", data.GetActualDepartureTime());
		json.Add("sid", data.GetSidName());
		json.Add("star", data.GetStarName());
		json.Add("departureRwy", data.GetDepartureRwy());
		json.Add("arrivalRwy", data.GetArrivalRwy());
		json.Add("finalAltitude", fp.GetFinalAltitude());
		json.Add("directTo", assignedData.GetDirectToPointName());
		json.Add("route", data.GetRoute());
		json.Add("remarks", data.GetRemarks());
		json.Add("groundState", fp.GetGroundState());
		json.Add("clearedFlag", fp.GetClearenceFlag() ? 1 : 0);

		auto isNotificationPoint = [](const char* name) {
			if (!name) return false;
			for (const char* p = name; *p; ++p) if (isdigit(*p)) return false;
			return true;
			};

		auto formatPointsJson = [&](int start, int end, int step, bool reverse) {
			std::vector<std::pair<std::string, int>> points;
			for (int i = start; (step > 0 ? i < end : i > end) && points.size() < 5; i += step) {
				const char* name = route.GetPointName(i);
				if (isNotificationPoint(name)) points.emplace_back(name, route.GetPointDistanceInMinutes(i));
			}
			if (reverse) std::reverse(points.begin(), points.end());

			std::ostringstream ss;
			ss << "[";
			time_t now = time(nullptr);
			for (size_t i = 0; i < points.size(); ++i) {
				if (i > 0) ss << ",";
				int totalMinutes = (int)((now / 60) % 1440) + points[i].second;
				char timeStr[8];
				snprintf(timeStr, sizeof(timeStr), "%02d%02d", (totalMinutes / 60) % 24, totalMinutes % 60);
				ss << "{\"name\":\"" << JsonBuilder::Escape(points[i].first) << "\",\"eta\":\"" << timeStr << "\"}";
			}
			ss << "]";
			return ss.str();
			};

		int depIdx = -1, arrIdx = -1;
		for (int i = 0; i < route.GetPointsNumber(); i++) {
			const char* name = route.GetPointName(i);
			if (name && strlen(name) == 4) {
				if (depIdx == -1) depIdx = i; else arrIdx = i;
			}
		}

		if (depIdx != -1 && arrIdx != -1) {
			json.AddRaw("departurePoints", formatPointsJson(depIdx + 1, arrIdx, 1, false));
			json.AddRaw("arrivalPoints", formatPointsJson(arrIdx - 1, -1, -1, true));
		}
		else {
			json.AddRaw("departurePoints", "[]");
			json.AddRaw("arrivalPoints", "[]");
		}

		json.Add("clearedAltitude", assignedData.GetClearedAltitude());
		json.Add("assignedHeading", assignedData.GetAssignedHeading());
		json.Add("assignedMach", assignedData.GetAssignedMach() / 100.0);
		json.Add("assignedSpeed", assignedData.GetAssignedSpeed());
		json.Add("entryPoint", fp.GetEntryCoordinationPointName());
		json.Add("exitPoint", fp.GetExitCoordinationPointName());
		json.Add("nextCopxPoint", fp.GetNextCopxPointName());
		json.Add("nextFirCopxPoint", fp.GetNextFirCopxPointName());

		return json.Build();
	}

public:
	void OnRadarScreenCreated(CRadarScreen* pRadarScreen) {
		g_pScreen = pRadarScreen;
	}

	void OnControllerPositionUpdate(CController Controller) override {
		ConnectToGateway();
	}

	void OnTimer(int Counter) override {
		// Process any commands from the background thread
		ProcessPendingTasks();

		if (wsConnected) {
			CheckAllFlightPlans();

			// Push ATC list every 30 seconds
			if (Counter % 30 == 0) {
				SendAtcList();
			}
		}
	}

	void OnControllerDisconnect(CController Controller) override {
		{
			std::lock_guard<std::mutex> lock(aircraftMutex);
			assumedAircraft.clear();
		}
	}

	void OnFlightPlanFlightPlanDataUpdate(CFlightPlan fp) override {
		if (!fp.IsValid() || !wsConnected) return;

		std::string callsign = fp.GetCallsign();
		int state = fp.GetState();

		HandleTransferState(fp, callsign, state);
		HandleAssumedState(fp, callsign, state);

		if (state == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) {
			std::string acJson = BuildAircraftJson(fp);

			std::lock_guard<std::mutex> lock2(aircraftMutex);
			if (acJson != lastAircraftData[callsign]) {
				JsonBuilder json;
				json.Add("type", "fpupdate");
				json.AddRaw("data", acJson);
				SendWebSocketMessage(json.Build());
				lastAircraftData[callsign] = acJson;
			}
		}
	}

	void OnFlightPlanControllerAssignedDataUpdate(CFlightPlan fp, int DataType) override {
		if (!fp.IsValid() || !wsConnected) return;

		if (fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) {
			std::string callsign = fp.GetCallsign();
			std::string acJson = BuildAircraftJson(fp);

			std::lock_guard<std::mutex> lock2(aircraftMutex);
			if (acJson != lastAircraftData[callsign]) {
				JsonBuilder json;
				json.Add("type", "fpupdate");
				json.AddRaw("data", acJson);
				SendWebSocketMessage(json.Build());
				lastAircraftData[callsign] = acJson;
			}
		}
	}

	void OnFlightPlanDisconnect(CFlightPlan fp) override {
		if (!fp.IsValid() || !wsConnected) return;

		if (fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) {
			std::string callsign = fp.GetCallsign();
			JsonBuilder json;
			json.Add("type", "release");
			json.Add("callsign", callsign);
			SendWebSocketMessage(json.Build());

			std::lock_guard<std::mutex> lock2(aircraftMutex);
			lastAircraftData.erase(callsign);
		}
	}

	bool OnCompileCommand(const char* sCommandLine) override {
		std::string cmd = sCommandLine;
		std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

		if (cmd.find(".striprestart") == 0) {
			LogMessage("Manual restart triggered via command.");
			DisconnectFromGateway();
			ConnectToGateway();
			DisplayUserMessage("StripCol", "System", "Gateway connection restarted.", true, true, false, false, false);
			return true;
		}

		if (cmd.find(".stripset") == 0) {

			std::string original = sCommandLine;
			size_t spacePos = original.find(' ');

			std::string newAddr;

			if (spacePos == std::string::npos) {
				newAddr = "127.0.0.1";
			}
			else {
				newAddr = original.substr(spacePos + 1);

				newAddr.erase(0, newAddr.find_first_not_of(" \t\r\n"));
				newAddr.erase(newAddr.find_last_not_of(" \t\r\n") + 1);

				if (newAddr.empty()) {
					newAddr = "127.0.0.1";
				}
			}

			{
				std::lock_guard<std::mutex> lock(gatewayMutex);
				gatewayAddress = newAddr;
			}

			std::string msg = "Gateway set to: " + newAddr;
			DisplayUserMessage("StripCol", "Gateway", msg.c_str(),
				true, true, false, false, false);

			DisconnectFromGateway();
			ConnectToGateway();

			return true;
		}

		if (cmd.find(".stripcode") == 0) {
			std::lock_guard<std::mutex> lock(codeMutex);
			if (!pairingCode.empty()) {
				std::string msg = "Current Pairing Code: " + pairingCode;
				DisplayUserMessage("StripCol", "System", msg.c_str(), true, true, false, false, false);
			}
			else {
				DisplayUserMessage("StripCol", "System", "No pairing code generated yet.", true, true, false, false, false);
			}
			return true;
		}

		return false;
	}
};

// ==================== PLUGIN ENTRY POINTS ====================

static StripCol* g_pPlugin = nullptr;

void EuroScopePlugInInit(CPlugIn** ppPlugInInstance) {
	g_pPlugin = new StripCol();
	*ppPlugInInstance = g_pPlugin;
}

void EuroScopePlugInExit(void) {
	delete g_pPlugin;
	g_pPlugin = nullptr;
}
