#pragma once

#include <common/all.h>

#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/sysinfo.h>

#include <stdio.h>
#include <ctype.h>

struct ip_range
{
    u32 ip;
    u8 cidr;
} typedef ip_range_t;

extern int cont;

void print_help_menu();
void signal_hndl(int code);
ip_range_t parse_ip_range(const char* ip);

const char* get_protocol_str_by_id(int id);
int get_protocol_id_by_str(char* name);

void print_tool_info();
u64 get_boot_nano_time();

void lower_str(char *str);