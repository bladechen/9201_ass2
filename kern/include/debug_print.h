#ifndef _DEBUG_PRINT_H_
#define _DEBUG_PRINT_H_
#include <lib.h>
#include <cdefs.h>
#define NONE                 "\e[0m"

#define BLACK                "\e[0;30m"
#define L_BLACK              "\e[1;30m"
#define RED                  "\e[0;31m"
#define L_RED                "\e[1;31m"
#define GREEN                "\e[0;32m"
#define L_GREEN              "\e[1;32m"
#define BROWN                "\e[0;33m"
#define YELLOW               "\e[1;33m"
#define BLUE                 "\e[0;34m"
#define L_BLUE               "\e[1;34m"
#define PURPLE               "\e[0;35m"
#define L_PURPLE             "\e[1;35m"
#define CYAN                 "\e[0;36m"                                                          #define L_CYAN               "\e[1;36m"
#define GRAY                 "\e[0;37m"
#define WHITE                "\e[1;37m"

#define BOLD                 "\e[1m"
#define UNDERLINE            "\e[4m"
#define BLINK                "\e[5m"
#define REVERSE              "\e[7m"
#define HIDE                 "\e[8m"
#define CLEAR                "\e[2J"
#define CLRLINE              "\r\e[K" //or "\e[1K\r"

#define OPEN_DEBUG_PRINT 1

#ifdef OPEN_DEBUG_PRINT
// #define DEBUG_PRINT (fmt, ...)  kprintf("[%s:%d]-<%s>: "##fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define DEBUG_PRINT ( kprintf)
// #define DEBUG_PRINT (kprintf(BLUE "<%s>-[%s:%d] " NONE, __FUNCTION__,__FILE__, __LINE__), kprintf)
// #define DEBUG_PRINT (fmt, ...) (kprintf(BLUE "<%s>-[%s:%d] ", __FUNCTION__,__FILE__, __LINE__), kprintf(__VA_ARGS__, NONE))
// #define DEBUG_PRINT(fmt, ...) kprintf(__VA_ARGS__)
#else
#define DEBUG_PRINT (fmt, ...) void(0)
#endif

#endif
