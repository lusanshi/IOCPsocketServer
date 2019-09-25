#include <WinSock2.h>
#include <Windows.h>
#include <stdio.h>
#include <ws2def.h>
#include <WS2tcpip.h>

#pragma comment(lib, "Ws2_32")

#define PORT 1234
#define MSGSIZE 1024

typedef enum
{
	RECV_POSTED
} OPERATION_TYPE;      // 枚举，表示状态

typedef struct
{
	WSAOVERLAPPED  overlap;              // 重叠结构
	WSABUF         Buffer;				 // 缓冲区
	char           szMessage[MSGSIZE];	 // 缓冲区数组
	DWORD          NumberOfBytesRecvd;	 // 接收的字节数
	DWORD          Flags;				 
	OPERATION_TYPE OperationType;		 

}PER_IO_OPERATION_DATA, *LPPER_IO_OPERATION_DATA;

DWORD WINAPI WorkerThread(LPVOID);

int main(void)
{
	WSADATA				    wsaData;
	SOCKET				    sListen, sClient;
	SOCKADDR_IN			    local, client;
	DWORD			        i, dwThreadId;
	int				        iaddrSize = sizeof(SOCKADDR_IN);
	char                    addr[20];	// 存放临时客户端地址
	HANDLE				    CompletionPort = INVALID_HANDLE_VALUE;
	SYSTEM_INFO		        systeminfo;
	LPPER_IO_OPERATION_DATA lpPerIOData = NULL;

	// 初始化Windows库
	if (WSAStartup(0x0202, &wsaData) != 0) {
		return 0;
	}
	// 创建完成端口对象
	CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	// 根据处理器个数创建工作线程
	GetSystemInfo(&systeminfo);
	for (i = 0; i < systeminfo.dwNumberOfProcessors; i++)
	{
		CreateThread(NULL, 0, WorkerThread, CompletionPort, 0, &dwThreadId);
	}
	// 初始化套接字
	sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	local.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	local.sin_family = AF_INET;
	local.sin_port = htons(PORT);
	// 绑定socket与端口，同步接收首个请求
	bind(sListen, (struct sockaddr*) & local, sizeof(SOCKADDR_IN));
	listen(sListen, 3);
	while (TRUE)
	{
		sClient = accept(sListen, (struct sockaddr*) & client, &iaddrSize);
		inet_ntop(AF_INET, &client.sin_addr, addr, iaddrSize);
		printf("Accepted client: %s:%d\n", addr, ntohs(client.sin_port));
		// 将套接字和完成端口绑定
		CreateIoCompletionPort((HANDLE)sClient, CompletionPort, (DWORD)sClient, 0);
		// 初始化传输数据结构
		lpPerIOData = (LPPER_IO_OPERATION_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_IO_OPERATION_DATA));
		lpPerIOData->Buffer.len = MSGSIZE;
		lpPerIOData->Buffer.buf = lpPerIOData->szMessage;
		lpPerIOData->OperationType = RECV_POSTED;

		WSARecv(
			// 异步接收数据，立即返回，在workerThread里处理接收数据
			sClient,
			&lpPerIOData->Buffer,
			1,
			&lpPerIOData->NumberOfBytesRecvd,
			&lpPerIOData->Flags,
			&lpPerIOData->overlap,
			NULL
		);
	}

	PostQueuedCompletionStatus(CompletionPort, 0xFFFFFFFF, 0, NULL);
	CloseHandle(CompletionPort);
	closesocket(sListen);
	WSACleanup();
	return 0;
}

DWORD WINAPI WorkerThread(LPVOID CompletionPortID)
{
	HANDLE					CompletionPort = (HANDLE) CompletionPortID;
	DWORD					BytesTransferred;
	LPPER_IO_OPERATION_DATA	PerIoData;        // IO操作结构
	DWORD					Flags;            // WSARecv()函数中的标志位
	SOCKET					client;

	while (TRUE)
	{
		GetQueuedCompletionStatus(
			CompletionPort,
			&BytesTransferred,
			(LPDWORD)& client,
			(LPOVERLAPPED*)& PerIoData,
			INFINITE
		);
		printf("socket %d\n", client);
		if (BytesTransferred == 0xFFFFFFFF)
		{
			if (PerIoData != NULL)
				HeapFree(GetProcessHeap(), 0, PerIoData);
			printf("GetQueuedCompletionStatus quit!\n");
			return 0;
		}
		// 如果数据传送完了,则退出
		if (BytesTransferred == 0)
		{
			printf("Closing socket %d\n", client);
			// 关闭套接字
			if (closesocket(client) == SOCKET_ERROR)
			{
				printf("closesocket failed with error %d\n", WSAGetLastError());
				return 0;
			}
			// 释放结构资源
			HeapFree(GetProcessHeap(), 0, PerIoData);
			PerIoData = NULL;
			continue;
		}
		// 如果还没有记录接收的数据数量,则将收到的字节数保存在PerIoData->NumberOfBytesRecvd中
		if (PerIoData->NumberOfBytesRecvd == 0)
		{
			PerIoData->NumberOfBytesRecvd = BytesTransferred;
		}
		// 成功接收到数据
		printf("\nBytes received: %d\n", BytesTransferred);
		// 处理数据请求
		PerIoData->szMessage[BytesTransferred] = '\0';
		printf("Server received: %s\n", PerIoData->szMessage);
		send(client, PerIoData->szMessage, BytesTransferred, 0);
		PerIoData->NumberOfBytesRecvd = 0;
		Flags = 0;
		memset(PerIoData, 0, sizeof(PER_IO_OPERATION_DATA));
		PerIoData->Buffer.len = MSGSIZE;
		PerIoData->Buffer.buf = PerIoData->szMessage;
		PerIoData->OperationType = RECV_POSTED;
		WSARecv(
			client,
			&PerIoData->Buffer,
			1,
			&PerIoData->NumberOfBytesRecvd,
			&PerIoData->Flags,
			&PerIoData->overlap,
			NULL
		);
	}
}
