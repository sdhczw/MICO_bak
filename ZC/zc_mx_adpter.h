/**
******************************************************************************
* @file     zc_MX_adpter.h
* @authors  cxy
* @version  V1.0.0
* @date     10-Sep-2014
* @brief    HANDSHAKE
******************************************************************************
*/

#ifndef  __ZC_MX_ADPTER_H__ 
#define  __ZC_MX_ADPTER_H__

#include <zc_common.h>
#include <zc_protocol_controller.h>
#include <zc_module_interface.h>

#define MX_MAX_SOCKET_LEN    (1000)

typedef struct 
{
    u32 u32Interval;  
    u8 u8TimerIndex;
    u8 u8ValidFlag;
}MX_Timer;

#ifdef __cplusplus
extern "C" {
#endif
void MX_Init(void);
void MX_BcInit(void);
void MX_WakeUp(void);
void MX_Sleep(void);
void MX_ReadDataFormFlash(void);
void MX_Cloudfunc(void *inContext);
void MX_CloudRecvfunc(void *inContext);
#ifdef __cplusplus
}
#endif
#endif
/******************************* FILE END ***********************************/

