#include <iostream>
#include <thread>
#include <mutex>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

struct Session
{
	OVERLAPPED overlappedR;
	OVERLAPPED overlappedS;
	SOCKET socket;
	CHAR sendBuffer[100] = "Hello, Server!";
	CHAR recvBuffer[100];
	mutex mtx;
};

void sendEvent(shared_ptr<Session> s)
{
	s->overlappedS.hEvent = ::WSACreateEvent();
	cout << "Send Event Listener Start" << endl;

	while (true)
	{
		WSABUF wsaBuf;
		wsaBuf.buf = s->sendBuffer;
		wsaBuf.len = sizeof(s->sendBuffer);

		DWORD numOfBytes = 0;
		DWORD flags = 0;
		if (SOCKET_ERROR == ::WSASend(s->socket, &wsaBuf, 1, &numOfBytes, flags, &s->overlappedS, nullptr))
		{
			int errorCode = ::WSAGetLastError();
			if (errorCode == WSA_IO_PENDING)
			{
				::WSAWaitForMultipleEvents(1, &s->overlappedS.hEvent, TRUE, WSA_INFINITE, FALSE);
				::WSAGetOverlappedResult(s->socket, &s->overlappedS, &numOfBytes, FALSE, &flags);
			}
			else
			{
				break;
			}

		}

		{
			lock_guard<mutex> lock(s->mtx);
			cout << "Data Sent : " << numOfBytes << endl;
		}
		::this_thread::sleep_for(1s);
	}

	::WSACloseEvent(s->overlappedS.hEvent);
}

void recvEvent(shared_ptr<Session> s)
{
	s->overlappedR.hEvent = ::WSACreateEvent();
	cout << "Receive Event Listener Start" << endl;

	while (true)
	{
		WSABUF wsaBuf;
		wsaBuf.buf = s->recvBuffer;
		wsaBuf.len = sizeof(s->recvBuffer);

		DWORD numOfBytes = 0;
		DWORD flags = 0;
		if (SOCKET_ERROR == ::WSARecv(s->socket, &wsaBuf, 1, &numOfBytes, &flags, &s->overlappedR, nullptr))
		{
			int errorCode = ::WSAGetLastError();
			if (errorCode == WSA_IO_PENDING)
			{
				::WSAWaitForMultipleEvents(1, &s->overlappedR.hEvent, TRUE, WSA_INFINITE, FALSE);
				::WSAGetOverlappedResult(s->socket, &s->overlappedR, &numOfBytes, FALSE, &flags);
			}
			else
			{
				break;
			}
		}

		{
			lock_guard<mutex> lock(s->mtx);
			cout << "Data Received : " << s->recvBuffer << endl;
		}
	}

	::WSACloseEvent(s->overlappedR.hEvent);
}

int main()
{
	WSADATA wsaData;
	if (0 != ::WSAStartup(MAKEWORD(2, 2), &wsaData))
		return -1;

	SOCKET clientSocket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (clientSocket == INVALID_SOCKET)
		return -1;

	u_long on = 1;
	if (INVALID_SOCKET == ::ioctlsocket(clientSocket, FIONBIO, &on))
		return -1;

	SOCKADDR_IN sockAddr;
	sockAddr.sin_family = AF_INET;
	::InetPton(AF_INET, L"127.0.0.1", &sockAddr.sin_addr);
	sockAddr.sin_port = ::htons(7777);

	::this_thread::sleep_for(3s);

	::connect(clientSocket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr));
	cout << "Server Connected" << endl;

	shared_ptr<Session> s = make_shared<Session>();
	s->socket = clientSocket;

	thread sendWorker{ sendEvent, s };
	thread recvWorker{ recvEvent, s };

	sendWorker.join();
	recvWorker.join();

	::closesocket(s->socket);
	::WSACleanup();

	return 0;
}