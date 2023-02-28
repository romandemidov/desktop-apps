#include "csocket.h"
//#include "utils.h"
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#ifndef UNICODE
# define UNICODE 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#pragma comment(lib, "Ws2_32.lib")
#include <Windows.h>
#include <winsock2.h>
#include <sys/types.h>
#include <io.h>
#define AF_TYPE AF_INET
#define INADDR "127.0.0.1"
#include "components/cmessage.h"
#define SEND_DELAY_MS 50
#define RETRIES_DELAY 50
#define RETRIES_COUNT 10
#define BUFFSIZE 1024

typedef struct sockaddr_in SockAddr;


class CSocket::CSocketPrv
{
public:
    CSocketPrv(CSocket *owner);
    ~CSocketPrv();

    static void handle_signal(int signal);
    bool createSocket(u_short port);
    bool connectToSocket(u_short port);
    void startReadMessages();
    void closeSocket(SOCKET &socket);

    CSocket *owner;
    SOCKET sender_fd,
           receiver_fd;
    static bool run;

private:

};

bool CSocket::CSocketPrv::run = true;

CSocket::CSocketPrv::CSocketPrv(CSocket *owner) :
    owner(owner),
    sender_fd(-1),
    receiver_fd(-1)
{}

CSocket::CSocketPrv::~CSocketPrv()
{}

bool CSocket::CSocketPrv::createSocket(u_short port)
{
    WSADATA wsaData = {0};
    int iResult = 0;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        //printf("WSAStartup failed: %d\n", iResult);
        return false;
    }
    SOCKET tmpd = INVALID_SOCKET;
    if ((tmpd = socket(AF_TYPE, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        //printf("Could not create socket\n");
        return false;
    }

    int len = 0;
    SockAddr addr;
    memset(&addr, 0, sizeof(SockAddr));
    addr.sin_family = AF_TYPE;
    addr.sin_addr.s_addr = inet_addr(INADDR);
    addr.sin_port = htons(port);
    len = sizeof(addr);

    // bind the name to the descriptor
    int ret = ::bind(tmpd, (struct sockaddr*)&addr, len);
    if (ret == 0) {
        receiver_fd = tmpd;
        return true;
    }
    closesocket(tmpd);
    return false;
}

bool CSocket::CSocketPrv::connectToSocket(u_short port)
{
    WSADATA wsaData = {0};
    int iResult = 0;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        //printf("WSAStartup failed: %d\n", iResult);
        return false;
    }
    SOCKET tmpd = INVALID_SOCKET;
    if ((tmpd = socket(AF_TYPE, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        //printf("Could not create socket\n");
        return false;
    }

    int len = 0;
    SockAddr addr;
    memset(&addr, 0, sizeof(SockAddr));
    addr.sin_family = AF_TYPE;
    addr.sin_addr.s_addr = inet_addr(INADDR);
    addr.sin_port = htons(port);
    len = sizeof(addr);

    // bind the name to the descriptor
    int ret = ::bind(tmpd, (struct sockaddr*)&addr, len);
    if (ret != 0) {
        if (WSAGetLastError() == WSAEADDRINUSE) {
            ret = ::connect(tmpd, (struct sockaddr*)&addr, sizeof(SockAddr));
            if (ret == 0) {
                sender_fd = tmpd;
                return true;
            }
        }
    }
    closesocket(tmpd);
    return false;
}

void CSocket::CSocketPrv::handle_signal(int signal)
{
    switch (signal) {
    case SIGTERM:
    case SIGABRT:
    case SIGBREAK:
        run = false;
    break;
    case SIGINT:
        run = false;
    break;
    }
}

void CSocket::CSocketPrv::startReadMessages()
{
    signal(SIGTERM, &CSocketPrv::handle_signal);
    signal(SIGABRT, &CSocketPrv::handle_signal);
    signal(SIGBREAK, &CSocketPrv::handle_signal);
    signal(SIGINT, &CSocketPrv::handle_signal);

    while (run) {
        char rcvBuf[BUFFSIZE] = {0};
        int ret_data = recv(receiver_fd, rcvBuf, BUFFSIZE, 0); // Receive the string data
        if (ret_data != BUFFSIZE) {
            if (WSAGetLastError() != WSATRY_AGAIN && WSAGetLastError() != WSAEWOULDBLOCK) {
                //printf("Error while accessing socket\n");
                // FAILURE
            }
            //printf("No further client_args in socket\n");

        } else {
            if (owner->m_received_callback)
                owner->m_received_callback(rcvBuf, strlen(rcvBuf));
            //printf("Received client arg: %s\n", rcvBuf);
            // SUCCESS
        }
    }
    //printf("Dropped out of daemon loop\n");
}

void CSocket::CSocketPrv::closeSocket(SOCKET &socket)
{
    if (socket >= 0) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        socket = -1;
    }
}

CSocket::CSocket(int sender_port, int receiver_port) :
    m_sender_port(sender_port),
    pimpl(new CSocketPrv(this))
{
    m_future = std::async(std::launch::async, [=]() {
        if (pimpl->createSocket(receiver_port))
            pimpl->startReadMessages();
        else
            CMessage::warning(nullptr, "Cannot create socket!");
    });
}

CSocket::~CSocket()
{
    pimpl->run = false;
    pimpl->closeSocket(pimpl->sender_fd);
    pimpl->closeSocket(pimpl->receiver_fd);
    if (m_future.valid())
        m_future.wait();
    WSACleanup();
    delete pimpl;
}

bool CSocket::sendMessage(void *data, size_t size)
{
    if (!data || size > BUFFSIZE - 1)
        return false;

    if (!pimpl->connectToSocket(m_sender_port)) {
        CMessage::warning(nullptr, "Cannot connect to socket!");
        return false;
    }

    char client_arg[BUFFSIZE] = {0};
    memcpy(client_arg, data, size);
    int ret_data = send(pimpl->sender_fd, client_arg, BUFFSIZE, 0); // Send the string
    if (ret_data != BUFFSIZE) {
        if (ret_data < 0) {
            //printf("Could not send device address to daemon!\n");
        } else {
            //printf("Could not send device address to daemon completely!\n");
        }
        pimpl->closeSocket(pimpl->sender_fd);
        return false;
    }
    //printf("Sended data to daemon: %s\n", msg.toLocal8Bit().data());
    pimpl->closeSocket(pimpl->sender_fd);
    return true;
}

void CSocket::onMessageReceived(FnVoidData callback)
{
    m_received_callback = callback;
}
