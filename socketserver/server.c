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
} OPERATION_TYPE;      // ö�٣���ʾ״̬

typedef struct
{
	WSAOVERLAPPED  overlap;              // �ص��ṹ
	WSABUF         Buffer;				 // ������
	char           szMessage[MSGSIZE];	 // ����������
	DWORD          NumberOfBytesRecvd;	 // ���յ��ֽ���
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
	char                    addr[20];	// �����ʱ�ͻ��˵�ַ
	HANDLE				    CompletionPort = INVALID_HANDLE_VALUE;
	SYSTEM_INFO		        systeminfo;
	LPPER_IO_OPERATION_DATA lpPerIOData = NULL;

	// ��ʼ��Windows��
	if (WSAStartup(0x0202, &wsaData) != 0) {
		return 0;
	}
	// ������ɶ˿ڶ���
	CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	// ���ݴ������������������߳�
	GetSystemInfo(&systeminfo);
	for (i = 0; i < systeminfo.dwNumberOfProcessors; i++)
	{
		CreateThread(NULL, 0, WorkerThread, CompletionPort, 0, &dwThreadId);
	}
	// ��ʼ���׽���
	sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	local.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	local.sin_family = AF_INET;
	local.sin_port = htons(PORT);
	// ��socket��˿ڣ�ͬ�������׸�����
	bind(sListen, (struct sockaddr*) & local, sizeof(SOCKADDR_IN));
	listen(sListen, 3);
	while (TRUE)
	{
		sClient = accept(sListen, (struct sockaddr*) & client, &iaddrSize);
		inet_ntop(AF_INET, &client.sin_addr, addr, iaddrSize);
		printf("Accepted client: %s:%d\n", addr, ntohs(client.sin_port));
		// ���׽��ֺ���ɶ˿ڰ�
		CreateIoCompletionPort((HANDLE)sClient, CompletionPort, (DWORD)sClient, 0);
		// ��ʼ���������ݽṹ
		lpPerIOData = (LPPER_IO_OPERATION_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_IO_OPERATION_DATA));
		lpPerIOData->Buffer.len = MSGSIZE;
		lpPerIOData->Buffer.buf = lpPerIOData->szMessage;
		lpPerIOData->OperationType = RECV_POSTED;

		WSARecv(
			// �첽�������ݣ��������أ���workerThread�ﴦ���������
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
	LPPER_IO_OPERATION_DATA	PerIoData;        // IO�����ṹ
	DWORD					Flags;            // WSARecv()�����еı�־λ
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
		// ������ݴ�������,���˳�
		if (BytesTransferred == 0)
		{
			printf("Closing socket %d\n", client);
			// �ر��׽���
			if (closesocket(client) == SOCKET_ERROR)
			{
				printf("closesocket failed with error %d\n", WSAGetLastError());
				return 0;
			}
			// �ͷŽṹ��Դ
			HeapFree(GetProcessHeap(), 0, PerIoData);
			PerIoData = NULL;
			continue;
		}
		// �����û�м�¼���յ���������,���յ����ֽ���������PerIoData->NumberOfBytesRecvd��
		if (PerIoData->NumberOfBytesRecvd == 0)
		{
			PerIoData->NumberOfBytesRecvd = BytesTransferred;
		}
		// �ɹ����յ�����
		printf("\nBytes received: %d\n", BytesTransferred);
		// ������������
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
