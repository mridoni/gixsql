#pragma once

#ifdef USE_VARLEN_16
#define VARLEN_LENGTH_PIC		"9(4) COMP-5"
#define VARLEN_PIC_SZ			4
#define VARLEN_LENGTH_SZ		2
#define VARLEN_LENGTH_T			uint16_t
#define VARLEN_BSWAP			COB_BSWAP_16
#else
#define VARLEN_LENGTH_PIC		"9(8) COMP-5"
#define VARLEN_PIC_SZ			9
#define VARLEN_LENGTH_SZ		4
#define VARLEN_LENGTH_T			uint32_t
#define VARLEN_BSWAP			COB_BSWAP_32
#endif