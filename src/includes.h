#ifndef  INCLUDES_PRESENT
#define  INCLUDES_PRESENT

//#define TASK_DBG 1
//#define NETWORK_RX_DBG 1

#define NONE "\033[m"
#define RED "\033[0;32;31m"
#define LIGHT_RED "\033[1;31m"
#define GREEN "\033[0;32;32m"
#define LIGHT_GREEN "\033[1;32m"
#define BLUE "\033[0;32;34m"
#define LIGHT_BLUE "\033[1;34m"
#define DARY_GRAY "\033[1;30m"
#define CYAN "\033[0;36m"
#define LIGHT_CYAN "\033[1;36m"
#define PURPLE "\033[0;35m"
#define LIGHT_PURPLE "\033[1;35m"
#define BROWN "\033[0;33m"
#define YELLOW "\033[1;33m"
#define LIGHT_GRAY "\033[0;37m"
#define WHITE "\033[1;37m"

#include  <ucos_ii.h>

#define dump(x,y) NOTHING()
#define dump2(x,y) NOTHING()

// #define dump(x,y) \
// do {  volatile long result; \
// 	printf(CYAN "[%s:%d] OSPrioHighRdy=%d ",__func__,__LINE__,OSPrioHighRdy); \
//     asm("mov    %[result], sp" : [result]"=r" (result) /* Rotation result. */ : : /* No clobbers */); \
// 	printf("sp:0x%x, ",result);\
// 	asm("mov    %[result], lr" : [result]"=r" (result) /* Rotation result. */ : : /* No clobbers */);\
// 	printf("lr:0x%x, ",result);\
// 	asm("mov    %[result], pc" : [result]"=r" (result) /* Rotation result. */ : : /* No clobbers */);\
// 	printf("pc:0x%x\n" NONE,result);\
// } while(0)

// #define dump2(x,y) \
// do {  volatile long result; \
// 	printf(LIGHT_PURPLE "[%s:%d] OSPrioHighRdy=%d ",__func__,__LINE__,OSPrioHighRdy); \
//     asm("mov    %[result], sp" : [result]"=r" (result) /* Rotation result. */ : : /* No clobbers */); \
// 	printf("sp:0x%x, ",result);\
// 	asm("mov    %[result], lr" : [result]"=r" (result) /* Rotation result. */ : : /* No clobbers */);\
// 	printf("lr:0x%x, ",result);\
// 	asm("mov    %[result], pc" : [result]"=r" (result) /* Rotation result. */ : : /* No clobbers */);\
// 	printf("pc:0x%x\n" NONE,result);\
// } while(0)
#endif

