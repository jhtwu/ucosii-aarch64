
/*
*********************************************************************************************************
*                                            EXAMPLE CODE
*
*                          (c) Copyright 2009-2015; Micrium, Inc.; Weston, FL
*
*               All rights reserved.  Protected by international copyright laws.
*
*               Please feel free to use any application code labeled as 'EXAMPLE CODE' in
*               your application products.  Example code may be used as is, in whole or in
*               part, or may be used as a reference only.
*
*               Please help us continue to provide the Embedded community with the finest
*               software available.  Your honesty is greatly appreciated.
*
*               You can contact us at www.micrium.com.
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*
*                                      APPLICATION CONFIGURATION
*
*                                            IMX6SX-SDB
*
* Filename      : app_cfg.h
* Version       : V1.00
* Programmer(s) : JBL
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                                INCLUDE
*********************************************************************************************************
*/

#ifndef  __APP_CFG_H__
#define  __APP_CFG_H__



#include  <stdio.h>
#include  <cpu.h>

                                                                            /* Choose the level of debug messages                   */
#define  BSP_CFG_TRACE_LEVEL                       TRACE_LEVEL_OFF
#define  APP_CFG_TRACE_LEVEL                       TRACE_LEVEL_DBG


void  BSP_Ser_Printf (CPU_CHAR  *format, ...);

#define  APP_CFG_TRACE  BSP_Ser_Printf


#define  APP_TRACE_INFO(x)               ((APP_CFG_TRACE_LEVEL >= TRACE_LEVEL_INFO)  ? (void)(APP_CFG_TRACE x) : (void)0)
#define  APP_TRACE_DBG(x)                ((APP_CFG_TRACE_LEVEL >= TRACE_LEVEL_DBG)   ? (void)(APP_CFG_TRACE x) : (void)0)




#endif /* __APP_CFG_H__ */
