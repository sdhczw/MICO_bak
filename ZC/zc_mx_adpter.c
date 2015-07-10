#include "stdio.h"
#include "ctype.h"
#include "MicoPlatform.h"
#include "MICO.h"
#include "MICODefine.h"
#include <zc_common.h>
#include <zc_protocol_controller.h>
#include <zc_module_interface.h>
#include <zc_timer.h>
#include <zc_mx_adpter.h>

static int need_reload = 0;
//extern vu32 MS_TIMER;

u8 g_u8recvbuffer[MX_MAX_SOCKET_LEN]; // cmd, fwd data are saved in this buffer
ZC_UartBuffer g_struUartBuffer;
extern mico_Context_t *context;


MX_Timer g_struMxTimer[ZC_TIMER_MAX_NUM];
u16 g_struMxTimerCount[ZC_TIMER_MAX_NUM];
u8 g_u8TimerIndex;
mico_timer_t g_struMicoTimer;
extern PTC_ProtocolCon  g_struProtocolController;
PTC_ModuleAdapter g_struAdapter;

MSG_Buffer g_struRecvBuffer;
MSG_Buffer g_struRetxBuffer;

MSG_Queue  g_struRecvQueue;
MSG_Buffer g_struSendBuffer[MSG_BUFFER_SEND_MAX_NUM];
MSG_Queue  g_struSendQueue;

u8 g_u8MsgBuildBuffer[MSG_BULID_BUFFER_MAXLEN];


u16 g_u16TcpMss;
u16 g_u16LocalPort;
u32 g_u32EasylinkFlag = 0;
u16 g_gu16flag = 0;

u32 u32CloudIp = 0;
u8 g_u8ClientSendLen = 0;
MSG_Buffer g_struClientBuffer;
struct sockaddr_t struRemoteAddr;
u32 g_u32BcSleepCount = 0;
/*************************************************
* Function: MX_WakeUp
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_WakeUp()
{
    PCT_WakeUp();
}
/*************************************************
* Function: HF_Sleep
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_Sleep()
{
    u32 u32Index;
    
    close(g_Bcfd);

    if (PCT_INVAILD_SOCKET != g_struProtocolController.struClientConnection.u32Socket)
    {
        close(g_struProtocolController.struClientConnection.u32Socket);
        g_struProtocolController.struClientConnection.u32Socket = PCT_INVAILD_SOCKET;
    }

    if (PCT_INVAILD_SOCKET != g_struProtocolController.struCloudConnection.u32Socket)
    {
        close(g_struProtocolController.struCloudConnection.u32Socket);
        g_struProtocolController.struCloudConnection.u32Socket = PCT_INVAILD_SOCKET;
    }
    
    for (u32Index = 0; u32Index < ZC_MAX_CLIENT_NUM; u32Index++)
    {
        if (0 == g_struClientInfo.u32ClientVaildFlag[u32Index])
        {
            close(g_struClientInfo.u32ClientFd[u32Index]);
            g_struClientInfo.u32ClientFd[u32Index] = PCT_INVAILD_SOCKET;
        }
    }

    PCT_Sleep();
}
/*************************************************
* Function: HF_WriteDataToFlash
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_WriteDataToFlash(u8 *pu8Data, u16 u16Len)
{
    uint32_t paraStartAddress, paraEndAddress;
	
    paraStartAddress = EX_PARA_START_ADDRESS;
    paraEndAddress = EX_PARA_END_ADDRESS;

    MicoFlashInitialize(MICO_FLASH_FOR_EX_PARA);
    
    MicoFlashErase(MICO_FLASH_FOR_EX_PARA, paraStartAddress, paraEndAddress);
    
    MicoFlashWrite(MICO_FLASH_FOR_EX_PARA,&paraStartAddress, pu8Data, u16Len);
    
    MicoFlashFinalize(MICO_FLASH_FOR_EX_PARA);
}
/*************************************************
* Function: MX_Init
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_CloudRecvfunc(void *inContext)
{
    s32 s32RecvLen=0; 
    fd_set fdread;
    u32 u32Index;
    u32 u32Len=0; 
    u32 u32ActiveFlag = 0;
    struct sockaddr_t cliaddr;
    int connfd;
    extern u8 g_u8ClientStart;
    u32 u32MaxFd = 0;
    struct timeval_t timeout; 
    struct sockaddr_t addr;
    int tmp=1;    

    
    while(1) 
    {
        ZC_StartClientListen();

        u32ActiveFlag = 0;
        
        timeout.tv_sec= 0; 
        timeout.tv_usec= 1000; 
        
        FD_ZERO(&fdread);

        FD_SET(g_Bcfd, &fdread);
        u32MaxFd = u32MaxFd > g_Bcfd ? u32MaxFd : g_Bcfd;

        if (PCT_INVAILD_SOCKET != g_struProtocolController.struClientConnection.u32Socket)
        {
            FD_SET(g_struProtocolController.struClientConnection.u32Socket, &fdread);
            u32MaxFd = u32MaxFd > g_struProtocolController.struClientConnection.u32Socket ? u32MaxFd : g_struProtocolController.struClientConnection.u32Socket;
            u32ActiveFlag = 1;
        }
        
        if ((g_struProtocolController.u8MainState >= PCT_STATE_WAIT_ACCESSRSP) 
        && (g_struProtocolController.u8MainState < PCT_STATE_DISCONNECT_CLOUD))
        {
            FD_SET(g_struProtocolController.struCloudConnection.u32Socket, &fdread);
            u32MaxFd = u32MaxFd > g_struProtocolController.struCloudConnection.u32Socket ? u32MaxFd : g_struProtocolController.struCloudConnection.u32Socket;
            u32ActiveFlag = 1;
        }


        for (u32Index = 0; u32Index < ZC_MAX_CLIENT_NUM; u32Index++)
        {
            if (0 == g_struClientInfo.u32ClientVaildFlag[u32Index])
            {
                FD_SET(g_struClientInfo.u32ClientFd[u32Index], &fdread);
                u32MaxFd = u32MaxFd > g_struClientInfo.u32ClientFd[u32Index] ? u32MaxFd : g_struClientInfo.u32ClientFd[u32Index];
                u32ActiveFlag = 1;            
            }
        }


        if (0 == u32ActiveFlag)
        {
            continue;
        }
        
        select(u32MaxFd + 1, &fdread, NULL, NULL, &timeout);
        
        if ((g_struProtocolController.u8MainState >= PCT_STATE_WAIT_ACCESSRSP) 
        && (g_struProtocolController.u8MainState < PCT_STATE_DISCONNECT_CLOUD))
        {
            if (FD_ISSET(g_struProtocolController.struCloudConnection.u32Socket, &fdread))
            {
                s32RecvLen = recv(g_struProtocolController.struCloudConnection.u32Socket, g_u8recvbuffer, MX_MAX_SOCKET_LEN, 0); 
                
                if(s32RecvLen > 0) 
                {
                    ZC_Printf("recv data len = %d\n", s32RecvLen);
                    MSG_RecvDataFromCloud(g_u8recvbuffer, s32RecvLen);
                }
                else
                {
                    ZC_Printf("recv error, len = %d\n",s32RecvLen);
                    PCT_DisConnectCloud(&g_struProtocolController);
                }
            }
            
        }

        
        for (u32Index = 0; u32Index < ZC_MAX_CLIENT_NUM; u32Index++)
        {
            if (0 == g_struClientInfo.u32ClientVaildFlag[u32Index])
            {
                if (FD_ISSET(g_struClientInfo.u32ClientFd[u32Index], &fdread))
                {
                    s32RecvLen = recv(g_struClientInfo.u32ClientFd[u32Index], g_u8recvbuffer, MX_MAX_SOCKET_LEN, 0); 
                    if (s32RecvLen > 0)
                    {
                        ZC_RecvDataFromClient(g_struClientInfo.u32ClientFd[u32Index], g_u8recvbuffer, s32RecvLen);
                    }
                    else
                    {   
                        ZC_ClientDisconnect(g_struClientInfo.u32ClientFd[u32Index]);
                        close(g_struClientInfo.u32ClientFd[u32Index]);
                    }
                    
                }
            }
            
        }

        if (PCT_INVAILD_SOCKET != g_struProtocolController.struClientConnection.u32Socket)
        {
            if (FD_ISSET(g_struProtocolController.struClientConnection.u32Socket, &fdread))
            {
                connfd = accept(g_struProtocolController.struClientConnection.u32Socket,(struct sockaddr_t *)&cliaddr,(socklen_t *)&u32Len);

                if (ZC_RET_ERROR == ZC_ClientConnect((u32)connfd))
                {
                    close(connfd);
                }
                else
                {
                    ZC_Printf("accept client = %d\n", connfd);
                }
            }
        }

        if (FD_ISSET(g_Bcfd, &fdread))
        {
            tmp = sizeof(addr); 
            s32RecvLen = recvfrom(g_Bcfd, g_u8MsgBuildBuffer, 100, 0, (struct sockaddr_t *)&addr, (socklen_t*)&tmp); 
            if(s32RecvLen > 0) 
            {
                ZC_SendClientQueryReq(g_u8MsgBuildBuffer, (u16)s32RecvLen);
            } 
        }
        
    } 
}

/*************************************************
* Function: MX_ConnectToCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 MX_ConnectToCloud(PTC_Connection *pstruConnection)
{
    int fd; 
    int opt = 0;
    struct sockaddr_t addr;
    memset((char*)&addr,0,sizeof(addr));
    
    if (1 == g_struZcConfigDb.struSwitchInfo.u32ServerAddrConfig)
    {
        addr.s_port = g_struZcConfigDb.struSwitchInfo.u16ServerPort;
        addr.s_ip = g_struZcConfigDb.struSwitchInfo.u32ServerIp;
    }
    else
    {
        addr.s_ip = u32CloudIp;
        addr.s_port = ZC_CLOUD_PORT;
    }
		
		
    if (0 == addr.s_ip)
    {
        return ZC_RET_ERROR;
    }
    
	ZC_Printf("connect ip = 0x%x!\n",addr.s_ip);
    
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setsockopt(fd,0,SO_BLOCKMODE,&opt,4);
    if(fd<0)
        return ZC_RET_ERROR;
    
    if (connect(fd, &addr, sizeof(addr))< 0)
    {
        close(fd);
			  if(g_struProtocolController.struCloudConnection.u32ConnectionTimes++>20)
        {
           g_struZcConfigDb.struSwitchInfo.u32ServerAddrConfig = 0;
        }
        return ZC_RET_ERROR;
    }

    ZC_Printf("connect ok!\n");
		
		g_struProtocolController.struCloudConnection.u32Socket = fd;
    ZC_Rand(g_struProtocolController.RandMsg);
    
    return ZC_RET_OK;
}


/*************************************************
* Function: MX_FirmwareUpdate
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 MX_FirmwareUpdate(u8 *pu8FileData, u32 u32Offset, u32 u32DataLen)
{
	u32  u32UpdataAddr= UPDATE_START_ADDRESS;

    if (0 == u32Offset)
    {
        MicoFlashInitialize(MICO_FLASH_FOR_UPDATE);
        MicoFlashErase(MICO_FLASH_FOR_UPDATE, UPDATE_START_ADDRESS, UPDATE_END_ADDRESS);
    }
    u32UpdataAddr = u32UpdataAddr + u32Offset;
    
    MicoFlashWrite(MICO_FLASH_FOR_UPDATE,&u32UpdataAddr, pu8FileData, u32DataLen);
    return ZC_RET_OK;
}
/*************************************************
* Function: MX_FirmwareUpdateFinish
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 MX_FirmwareUpdateFinish(u32 u32TotalLen)
{

    memset(&(context->flashContentInRam.bootTable), 0, sizeof(boot_table_t));
    context->flashContentInRam.bootTable.length = u32TotalLen;
    context->flashContentInRam.bootTable.start_address = UPDATE_START_ADDRESS;
    context->flashContentInRam.bootTable.type = 'A';
    context->flashContentInRam.bootTable.upgrade_type = 'U';

    MicoFlashFinalize(MICO_FLASH_FOR_UPDATE);
    
    MICOUpdateConfiguration(context);
    context->micoStatus.sys_state = eState_Software_Reset;
    mico_rtos_set_semaphore(&context->micoStatus.sys_state_change_sem);

    return ZC_RET_OK;
}
#define app_log(M, ...) custom_log("APP", M, ##__VA_ARGS__)
/*************************************************
* Function: MX_SendDataToMoudle
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 MX_SendDataToMoudle(u8 *pu8Data, u16 u16DataLen)
{
    u8 u8MagicFlag[4] = {0x02,0x03,0x04,0x05};

		MicoUartSend(UART_FOR_APP, u8MagicFlag, 4);
	  MicoUartSend(UART_FOR_APP, pu8Data, u16DataLen);
    return ZC_RET_OK;
}
/*************************************************
* Function: MX_StopTimer
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_StopTimer(u8 u8TimerIndex)
{
    g_struMxTimer[u8TimerIndex].u8ValidFlag = 0;
}

/*************************************************
* Function: MX_timer_callback
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void  MX_timer_callback(void* arg) 
{
	  u8 i =0;
    (void )arg;
    
    for(i=0;i<ZC_TIMER_MAX_NUM;i++)
    {
        if(g_struMxTimer[i].u8ValidFlag)
        {
            if(g_struMxTimer[i].u32Interval==g_struMxTimerCount[i]++)
            {
                g_struMxTimerCount[i]=0;
                TIMER_TimeoutAction(i);
							  TIMER_StopTimer(i);
            }
        }
    }
}

/*************************************************
* Function: MX_SetTimer
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 MX_SetTimer(u8 u8Type, u32 u32Interval, u8 *pu8TimeIndex)
{
    u8 u8TimerIndex;
    u32 u32Retval;
    u32Retval = TIMER_FindIdleTimer(&u8TimerIndex);

    if (ZC_RET_OK == u32Retval)
    {
        TIMER_AllocateTimer(u8Type, u8TimerIndex, (u8*)&g_struMxTimer[u8TimerIndex]);
        g_struMxTimer[u8TimerIndex].u32Interval = u32Interval/1000;
        g_struMxTimer[u8TimerIndex].u8ValidFlag = 1;
        g_struMxTimerCount[u8TimerIndex]=0;
			  *pu8TimeIndex = u8TimerIndex;
    }
    return u32Retval;

}

/*************************************************
* Function: MX_Rest
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_Rest()
{
	  g_u32EasylinkFlag = 1;
}
/*************************************************
* Function: MX_SendTcpData
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_SendTcpData(u32 u32Fd, u8 *pu8Data, u16 u16DataLen, ZC_SendParam *pstruParam)
{
    send(u32Fd, pu8Data, u16DataLen, 0);
}

/*************************************************
* Function: MX_SendUdpData
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_SendUdpData(u32 u32Fd, u8 *pu8Data, u16 u16DataLen, ZC_SendParam *pstruParam)
{

    sendto(u32Fd, pu8Data, u16DataLen, 0, 
        (struct sockaddr_t*)(pstruParam->pu8AddrPara), sizeof(struct sockaddr_t)); 
}	

/*************************************************
* Function: MX_ListenClient
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 MX_ListenClient(PTC_Connection *pstruConnection)
{
    int fd; 
    struct sockaddr_t servaddr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0)
        return ZC_RET_ERROR;

    servaddr.s_port = pstruConnection->u16Port;
    if(bind(fd,(struct sockaddr_t *)&servaddr,sizeof(servaddr))<0)
    {
        close(fd);
        return ZC_RET_ERROR;
    }
    
    if (listen(fd, 4)< 0)
    {
        close(fd);
        return ZC_RET_ERROR;
    }

    ZC_Printf("Tcp Listen Port = %d\n", pstruConnection->u16Port);
    g_struProtocolController.struClientConnection.u32Socket = fd;

    return ZC_RET_OK;
}

/*************************************************
* Function: MX_BcInit
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_BcInit()
{
    int tmp=1;
    struct sockaddr_t addr;
    addr.s_port = ZC_MOUDLE_PORT;
    addr.s_ip = INADDR_ANY;
    g_Bcfd = socket(AF_INET, SOCK_DGRM, IPPROTO_UDP); 
    tmp=1; 
    setsockopt(g_Bcfd, SOL_SOCKET,SO_BROADCAST,&tmp,sizeof(tmp)); 
    bind(g_Bcfd, &addr, sizeof(addr));
    g_struProtocolController.u16SendBcNum = 0;
    memset((char*)&struRemoteAddr,0,sizeof(struRemoteAddr));

    struRemoteAddr.s_port = ZC_MOUDLE_BROADCAST_PORT; 
    struRemoteAddr.s_ip = inet_addr("255.255.255.255"); 
    g_pu8RemoteAddr = (u8*)&struRemoteAddr;
    g_u32BcSleepCount = 500000;
    return;
}
/*************************************************
* Function: MX_Cloudfunc
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_Cloudfunc(void *inContext)
{
		int fd;
		u32 u32Timer = 0;

		MX_BcInit();

		while(1) 
		{
				fd = g_struProtocolController.struCloudConnection.u32Socket;
				PCT_Run();

				if (PCT_STATE_DISCONNECT_CLOUD == g_struProtocolController.u8MainState)
				{
						close(fd);
						u32Timer = rand();
						u32Timer = (PCT_TIMER_INTERVAL_RECONNECT) * (u32Timer % 10 + 1);
						PCT_ReconnectCloud(&g_struProtocolController, u32Timer);
					  g_gu16flag = 1;
				}
				else
				{
						MSG_SendDataToCloud((u8*)&g_struProtocolController.struCloudConnection);
				}
				ZC_SendBc();
		} 
}
/*************************************************
* Function: MX_TimerInit
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_TimerInit()
{
    u8 i = 0;

    for(i=0;i<ZC_TIMER_MAX_NUM;i++)
    {
        g_struMxTimer[i].u8ValidFlag = 0;
    }
		mico_init_timer(&g_struMicoTimer,1000,MX_timer_callback,NULL);	
	  mico_start_timer(&g_struMicoTimer);
}

/*************************************************
* Function: MX_Init
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_Init()
{
    ZC_Printf("MT Init\n");
    g_struAdapter.pfunConnectToCloud = MX_ConnectToCloud;
    g_struAdapter.pfunUpdate = MX_FirmwareUpdate;     
    g_struAdapter.pfunUpdateFinish = MX_FirmwareUpdateFinish;
    g_struAdapter.pfunSendToMoudle = MX_SendDataToMoudle;  
    g_struAdapter.pfunSetTimer = MX_SetTimer;   
    g_struAdapter.pfunStopTimer = MX_StopTimer;
    g_struAdapter.pfunListenClient = MX_ListenClient;
    g_struAdapter.pfunSendTcpData = MX_SendTcpData;  
    g_struAdapter.pfunSendUdpData = MX_SendUdpData; 
    g_struAdapter.pfunRest = MX_Rest;
    g_struAdapter.pfunWriteFlash = MX_WriteDataToFlash;
    
    g_u16TcpMss = 1000;
    PCT_Init(&g_struAdapter);
	  MX_TimerInit();
}

/*************************************************
* Function: socket_connected
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void socket_connected(int fd)
{
    ZC_Printf("socket connected\n");
    g_struProtocolController.struCloudConnection.u32Socket = fd;
   
//    ZC_Rand(g_struProtocolController.RandMsg);
//	  g_struProtocolController.u8MainState = PCT_STATE_WAIT_ACCESS;
//	  ZC_Printf("socket_connected fd = %d\n",fd);
}
/*************************************************
* Function: RptConfigmodeRslt
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void RptConfigmodeRslt(network_InitTypeDef_st *nwkpara)
{
    if(nwkpara == NULL)
    {
        ZC_Printf("open easy link\n");    
        //OpenEasylink(60*5);
			  context->micoStatus.sys_state = eState_EasyLink;
        mico_rtos_set_semaphore(&context->micoStatus.sys_state_change_sem);
    }
    else
    {
        ZC_Printf("recv config\n");    
			  memcpy(context->flashContentInRam.micoSystemConfig.ssid, nwkpara->wifi_ssid, maxSsidLen);
        memset(context->flashContentInRam.micoSystemConfig.bssid, 0x0, 6);
        memcpy(context->flashContentInRam.micoSystemConfig.user_key, nwkpara->wifi_key, maxKeyLen);
        context->flashContentInRam.micoSystemConfig.user_keyLength = strlen(nwkpara->wifi_key);
        /*Clear fastlink record*/
//        memset(&(context->flashContentInRam.micoSystemConfig.fastLinkConf), 0x0, sizeof(fast_link_st));
			  MICOUpdateConfiguration(context);
			  context->micoStatus.sys_state = eState_Software_Reset;
        mico_rtos_set_semaphore(&context->micoStatus.sys_state_change_sem);
    }
}

/*************************************************
* Function: dns_ip_set
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void dns_ip_set(u8 *hostname, u32 ip)
{
	  if((int)ip == -1)
	  {
	      ZC_Printf("DNS ERROR\n");
	  }
	  else
	  {
        u32CloudIp = ip;
        ZC_Printf("DNS = 0x%x\n", u32CloudIp);
	  }
}

/*************************************************
* Function: MX_ReadDataFormFlash
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void MX_ReadDataFormFlash(void) 
{
    u32 configInFlash;
    u32 u32MagicFlag = 0xFFFFFFFF;

    configInFlash = EX_PARA_START_ADDRESS;
	  MicoFlashInitialize(MICO_FLASH_FOR_EX_PARA);
		MicoFlashRead(MICO_FLASH_FOR_EX_PARA, &configInFlash, (uint8_t *)&u32MagicFlag, 4);
    if (ZC_MAGIC_FLAG == u32MagicFlag)
    {
        //memcpy(&g_struZcConfigDb, (void *)(configInFlash + sizeof(flash_content_t)), sizeof(ZC_ConfigDB));
			MicoFlashRead(MICO_FLASH_FOR_EX_PARA, &configInFlash, (uint8_t *)&g_struZcConfigDb.struConnection, sizeof(ZC_ConfigDB)-4);
    }
    else
    {
        ZC_Printf("no para, use default\n");
    }
    
}



/*************************************************
* Function: mxchipWNet_HA_tick
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void mxchipWNet_HA_tick(void)
{
    if (need_reload == 1) 
    {
        msleep(500);
        NVIC_SystemReset();
    }
		if(g_u32EasylinkFlag)
	  {
        OpenEasylink(60);	
        g_u32EasylinkFlag = 0;
    }
}


/******************************* FILE END ***********************************/


