#pragma once


#define S_VARLEN_LENGTH_PIC		"9(4) COMP-5"
#define S_VARLEN_PIC_SZ			4
#define S_VARLEN_LENGTH_SZ		2
#define S_VARLEN_LENGTH_T			uint16_t
#define S_VARLEN_BSWAP			COB_BSWAP_16

#define L_VARLEN_LENGTH_PIC		"9(8) COMP-5"
#define L_VARLEN_PIC_SZ			9
#define L_VARLEN_LENGTH_SZ		4
#define L_VARLEN_LENGTH_T			uint32_t
#define L_VARLEN_BSWAP			COB_BSWAP_32
