#include <iostream>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment (lib, "ws2_32.lib")

using namespace std;

struct Session
{
	WSAOVERLAPPED overlappedR;
	WSAOVERLAPPED overlappedS;
	SOCKET socket;
	SOCKADDR_IN address;
	CHAR recvBuffer[100];
	CHAR sendBuffer[100] = "Hello, Client!";
	condition_variable conVar;
	mutex mtx;
};

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
			s->conVar.notify_one();
		}
	}

	::WSACloseEvent(s->overlappedR.hEvent);
}

void sendEvent(shared_ptr<Session> s)
{
	s->overlappedS.hEvent = ::WSACreateEvent();
	cout << "Send Event Listener Start" << endl;

	while (true)
	{
		{
			unique_lock<mutex> lock(s->mtx);
			s->conVar.wait(lock, [] { return true; });
		}

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
				cout << "Pending..." << endl;
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

int main()
{
	WSADATA wsaData;
	if (0 != ::WSAStartup(MAKEWORD(2, 2), &wsaData))
		return -1;

	SOCKET socket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (socket == INVALID_SOCKET)
		return -1;

	u_long on = 1;
	if (INVALID_SOCKET == ::ioctlsocket(socket, FIONBIO, &on))
		return -1;

	SOCKADDR_IN sockAddr;
	sockAddr.sin_family = AF_INET;
	::InetPton(AF_INET, L"127.0.0.1", &sockAddr.sin_addr);
	sockAddr.sin_port = ::htons(7777);
	if (SOCKET_ERROR == ::bind(socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr)))
		return -1;

	if (SOCKET_ERROR == ::listen(socket, SOMAXCONN))
		return -1;

	while (true)
	{
		SOCKADDR_IN clientAddr;
		int sizeOfAddr = sizeof(clientAddr);
		SOCKET clientSocket = ::accept(socket, reinterpret_cast<SOCKADDR*>(&clientAddr), &sizeOfAddr);
		if (INVALID_SOCKET == clientSocket)
			continue;

		cout << "Client Connected" << endl;

		shared_ptr<Session> s = make_shared<Session>();
		s->socket = clientSocket;
		s->address = clientAddr;

		thread recvThread{ recvEvent, s };
		thread sendThread{ sendEvent, s };

		recvThread.join();
		sendThread.join();
		::closesocket(s->socket);
		cout << "Client Disconnected" << endl;
	}

	::closesocket(socket);
	::WSACleanup();
	return 0;
}