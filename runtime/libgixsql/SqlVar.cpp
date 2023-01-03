/*
* This file is part of Gix-IDE, an IDE and platform for GnuCOBOL
* Copyright (C) 2021-2022 Marco Ridoni
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* as published by the Free Software Foundation; either version 3,
* or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; see the file COPYING.LIB.  If
* not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor
* Boston, MA 02110-1301 USA
*/

#include <stdlib.h>
#include <math.h>
#include <string>
#include <cstring>
#include <locale.h>

#include "SqlVar.h"
#include "utils.h"
#include "Logger.h"
#include "custom_formatters.h"

#define assertm(exp, msg) assert(((void)msg, exp))

#define DECIMAL_LENGTH 1

#define DBERR_NUM_OUT_OF_RANGE		-410

#if defined(unix) || defined(__unix__) || defined(__unix) || defined(__linux__)
#include <byteswap.h>
#define COB_BSWAP_16(val) (bswap_16 (val))
#define COB_BSWAP_32(val) (bswap_32(val))
#define COB_BSWAP_64(val) (bswap_64 (val))
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define COB_BSWAP_16(val) (OSSwapInt16(val))
#define COB_BSWAP_32(val) (OSSwapInt32(val))
#define COB_BSWAP_64(val) (OSSwapInt64(val))
#else
#define COB_BSWAP_16(val) (_byteswap_ushort (val))
#define COB_BSWAP_32(val) (_byteswap_ulong (val))
#define COB_BSWAP_64(val) (_byteswap_uint64 (val))
#endif

#define CBL_FIELD_FLAG_NONE		(uint32_t)0x0
#define CBL_FIELD_FLAG_VARLEN	(uint32_t)0x80
#define CBL_FIELD_FLAG_BINARY	(uint32_t)0x100
#define CBL_FIELD_FLAG_AUTOTRIM	(uint32_t)0x200

#define ASCII_ZERO ((unsigned char)0x30)

const char SqlVar::_decimal_point = [] {
    struct lconv	*lc = localeconv();
    return lc->decimal_point[0];
}();

//void rtrim(char* const s);
int get_trimmed_length(char* const s, int l);

SqlVar::SqlVar(CobolVarType _type, int _length, int _power, uint32_t _flags, void *_addr)
{
	type = _type;
	length = _length;
	power = _power;
	addr = _addr;
	flags = _flags;
	is_variable_length = (_flags & CBL_FIELD_FLAG_VARLEN);
	is_binary = (_flags & CBL_FIELD_FLAG_BINARY);
	is_autotrim = (_flags & CBL_FIELD_FLAG_AUTOTRIM);
	allocate_realdata_buffer();
}


SqlVar::~SqlVar()
{
	//if (realdata)
	//	free(realdata);
}

SqlVar* SqlVar::copy()
{
	SqlVar* v = new SqlVar(type, length, power, 0, addr);

	v->is_variable_length = is_variable_length;
	v->is_binary = is_binary;
	v->is_autotrim = is_autotrim;

	return v;
}

const std_binary_data& SqlVar::getDbData()
{
	return db_data_buffer;
}


void SqlVar::createRealData()
{
	CobolVarType type = this->type;
	int length = this->length;
	int power = this->power;
	void* addr = this->addr;

	memset(db_data_buffer.data(), 0, db_data_buffer_len);
	db_data_len = db_data_buffer_len;

	switch (type) {
	case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER:
		{
			memcpy(db_data_buffer.data(), addr, db_data_buffer_len);

			if (power < 0) {
				insert_decimal_point(reinterpret_cast<char *>(db_data_buffer.data()), db_data_buffer_len, power);
			}

			spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			break;
		}
		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TC:
		{
			memcpy(db_data_buffer.data() + SIGN_LENGTH, addr, length);

			if (type_tc_is_positive(reinterpret_cast<char*>(db_data_buffer.data()) + SIGN_LENGTH + length - 1)) {
				db_data_buffer[0] = '+';
			}
			else {
				db_data_buffer[0] = '-';
			}

			if (power < 0) {
				insert_decimal_point(reinterpret_cast<char*>(db_data_buffer.data()), db_data_buffer_len + SIGN_LENGTH, power);
			}

			spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			break;
		}
		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LS:
		{
			memcpy(db_data_buffer.data(), addr, db_data_buffer_len);

			if (power < 0) {
				insert_decimal_point(reinterpret_cast<char*>(db_data_buffer.data()), db_data_buffer_len + SIGN_LENGTH, power);
			}

			spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			break;
		}
		case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER_PD:
		{
			double dlength;
			int skip_first;

			dlength = ceil(((double)length + 1) / 2);
			skip_first = (length + 1) % 2; // 1 -> skip first 4 bits

			int i;
			int index = 0;
			char* ptr;
			unsigned char tmp;
			unsigned char ubit = 0xF0;
			unsigned char lbit = 0x0F;

			for (i = 0; i < (int)dlength; i++) {

				ptr = ((char*)addr) + i * sizeof(char);
				tmp = (unsigned char)*ptr;

				if (i != 0 || !skip_first) {
					//sprintf(val, "%d", (tmp & ubit) >> 4);
					unsigned char c = ((tmp & ubit) >> 4) + ASCII_ZERO;
					db_data_buffer[index] = c;
					index++;
				}
				if (i != dlength - 1) {
					//sprintf(val, "%d", tmp & lbit);
					unsigned char c = (tmp & lbit) + ASCII_ZERO;
					db_data_buffer[index] = c;
					index++;
				}
			}

			if (power < 0) {
				insert_decimal_point(reinterpret_cast<char*>(db_data_buffer.data()), db_data_buffer_len, power);
			}

			spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			break;
		}
		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_PD:
		{
			double dlength;
			int skip_first;

			dlength = ceil(((double)length + 1) / 2);
			skip_first = (length + 1) % 2; // 1 -> skip first 4 bits

			/* set real data */
			int i;
			int index = SIGN_LENGTH;
			char* ptr;
			unsigned char tmp;
			unsigned char ubit = 0xF0;
			unsigned char lbit = 0x0F;

			for (i = 0; i < (int)dlength; i++) {

				ptr = ((char*)addr) + i * sizeof(char);
				tmp = (unsigned char)*ptr;

				if (i != 0 || !skip_first) {
					//sprintf(val, "%d", (tmp & ubit) >> 4);
					unsigned char c = ((tmp & ubit) >> 4) + 48;
					db_data_buffer[index] = c;
					index++;
				}
				if (i != dlength - 1) {
					//sprintf(val, "%d", tmp & lbit);
					unsigned char c = (tmp & lbit) + 48;
					db_data_buffer[index] = c;
					index++;
				}
				else {
					if ((tmp & lbit) == 0x0C) {
						db_data_buffer[0] = '+';
					}
					else {
						db_data_buffer[0] = '-';
					}
				}
			}

			if (power < 0) {
				insert_decimal_point(reinterpret_cast<char*>(db_data_buffer.data()), db_data_buffer_len + SIGN_LENGTH, power);
			}

			spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			break;
		}

		case CobolVarType::COBOL_TYPE_JAPANESE:
			length = length * 2;
			/* no break */
			[[fallthrough]];
		case CobolVarType::COBOL_TYPE_ALPHANUMERIC:
		{
			if (!is_variable_length) {
				memcpy(db_data_buffer.data(), (char*)addr, length);
				if (is_autotrim) {
					db_data_len = get_trimmed_length(reinterpret_cast<char*>(db_data_buffer.data()), length);
					//rtrim(reinterpret_cast<char*>(realdata.data()));
					//length = strlen(reinterpret_cast<char*>(realdata.data()));
				}
				spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			}
			else {
				void* actual_addr = (char*)addr + VARLEN_LENGTH_SZ;
				VARLEN_LENGTH_T *len_addr = (VARLEN_LENGTH_T *)addr;
				int actual_len = (*len_addr);
				memcpy(db_data_buffer.data(), (char*)actual_addr, actual_len);
				db_data_len = actual_len;
				spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			}
		}
		break;

		case CobolVarType::COBOL_TYPE_UNSIGNED_BINARY:

			if (this->length == 1) {	// 1 byte
				uint8_t n8 = *((uint8_t*)addr);
				snprintf((char*)db_data_buffer.data(), length, "%d", n8);
			}
			else {
				if (this->length == 2) {	// 1 byte
					uint8_t n8 = *((uint8_t*)addr);
					snprintf((char*)db_data_buffer.data(), length, "%d", n8);
				}
				else {
					if (this->length == 3 || this->length == 4) {	// 2 bytes
						uint16_t n16 = *((uint16_t*)addr);
						n16 = COB_BSWAP_16(n16);
						snprintf((char*)db_data_buffer.data(), length, "%d", n16);
					}
					else {
						if (this->length >= 5 || this->length <= 9) {	// 4 bytes
							uint32_t n32 = *((uint32_t*)addr);
							n32 = COB_BSWAP_32(n32);
							snprintf((char*)db_data_buffer.data(), length, "%d", n32);
						}
						else {
							if (this->length >= 10 || this->length <= 18) {	// 8 bytes
								uint64_t n64 = *((uint64_t*)addr);
								n64 = COB_BSWAP_64(n64);
								snprintf((char*)db_data_buffer.data(), length, "%d", n64);
							}
							else {
								// Should never happen - TODO: log fatal and abort
							}
						}
					}
				}
			}

			spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			break;

		case CobolVarType::COBOL_TYPE_SIGNED_BINARY:

			if (this->length == 1) {	// 1 byte
				int8_t n8 = *((int8_t*)addr);
				snprintf((char*)db_data_buffer.data(), length, "%d", n8);
			}
			else {
				if (this->length == 2) {	// 1 byte
					//int8_t  s_byte_number = (int8_t)strtoll(retstr, NULL, 0);
					int8_t n8 = *((int8_t*)addr);
					snprintf((char*)db_data_buffer.data(), length, "%d", n8);
				}
				else {
					if (this->length == 3 || this->length == 4) {	// 2 bytes
						int16_t n16 = *((int16_t*)addr);
						n16 = COB_BSWAP_16(n16);
						snprintf((char*)db_data_buffer.data(), length, "%d", n16);
					}
					else {
						if (this->length >= 5 || this->length <= 9) {	// 4 bytes
							int32_t n32 = *((int32_t*)addr);
							n32 = COB_BSWAP_32(n32);
							snprintf((char*)db_data_buffer.data(), length, "%d", n32);
						}
						else {
							if (this->length >= 10 || this->length <= 18) {	// 8 bytes
								int64_t n64 = *((int64_t*)addr);
								n64 = COB_BSWAP_64(n64);
								snprintf((char*)db_data_buffer.data(), length, "%d", n64);
							}
							else {
								// Should never happen - TODO: log fatal and abort
							}
						}
					}
				}
			}

			spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			break;

		default:
			db_data_buffer = std_binary_data(db_data_buffer_len);

			memcpy(db_data_buffer.data(), (char*)addr, db_data_buffer_len);
			spdlog::trace(FMT_FILE_FUNC "type: {}, length: {}, data: {}, db_data_buffer: [{}]", __FILE__, __func__, type, length, addr, (const char *)db_data_buffer.data());
			break;
	}
#if _DEBUG
	assertm(db_data_len >= 0, "db_data_len not set");
#endif
}

void* SqlVar::getAddr()
{
	return addr;
}


void SqlVar::createCobolData(char *retstr, int datalen, int *sqlcode)
{
	*sqlcode = 0;

	void* addr = this->addr;

	switch (type) {
		case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER:
		{
			char* ptr;
			int int_fillzero;

			// before decimal point
			int beforedp = 0;
			for (ptr = retstr; *ptr != '\0'; ptr++) {
				if (*ptr == _decimal_point) {
					break;
				}
				else {
					beforedp++;
				}
			}

			int_fillzero = length - beforedp + power;
			if (int_fillzero < 0)
				int_fillzero = 0;

			memset(addr, ASCII_ZERO, int_fillzero);
			if (beforedp <= length) {
				memcpy((uint8_t*)addr + int_fillzero, retstr, beforedp);
			}
			else {	// Truncation will occur
				memcpy((uint8_t*)addr, retstr + (beforedp - this->length), length);
				*sqlcode = DBERR_NUM_OUT_OF_RANGE;
			}

			if (power < 0) {
				int afterdp = 0;

				if (*ptr != '\0') {
					ptr++;

					// after decimal point
					for (; *ptr != '\0'; ptr++) {
						afterdp++;
					}

					// fill zero
					memcpy((uint8_t*)addr + int_fillzero + beforedp,
						retstr + beforedp + DECIMAL_LENGTH, afterdp);
				}

				int dec_fillzero = -power - afterdp;
				uint8_t* ptr = ((uint8_t*)addr + int_fillzero + beforedp) + afterdp;
				memset(ptr, ASCII_ZERO, dec_fillzero);
			}
			break;
		}
		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TC:
		{
			char* value;
			char* ptr;
			int is_negative = false;

			int int_fillzero;
			int final_length;

			if (retstr[0] == '-') {
				is_negative = true;
				value = retstr + 1;
			}
			else {
				value = retstr;
			}

			// before decimal point
			int beforedp = 0;
			for (ptr = value; *ptr != '\0'; ptr++) {
				if (*ptr == _decimal_point) {
					break;
				}
				else {
					beforedp++;
				}
			}

			int_fillzero = length - beforedp + power;
			if (int_fillzero < 0)
				int_fillzero = 0;

			memset(addr, ASCII_ZERO, int_fillzero);
			if (beforedp <= length) {	
				memcpy((uint8_t*)addr + int_fillzero, value, beforedp);
			}
			else {	// Truncation will occur
				memcpy((uint8_t*)addr, value + (beforedp - this->length), length);
				*sqlcode = DBERR_NUM_OUT_OF_RANGE;
			}

			if (power < 0) {
				int afterdp = 0;

				if (*ptr != '\0') {
					ptr++;

					// after decimal point
					for (; *ptr != '\0'; ptr++) {
						afterdp++;
					}
					memcpy((uint8_t*)addr + int_fillzero + beforedp, value +
						beforedp + DECIMAL_LENGTH, afterdp);
				}

				// fill zero
				int dec_fillzero = -power - afterdp;
				uint8_t* ptr = ((uint8_t*)addr + int_fillzero + beforedp) + afterdp;
				memset(ptr, ASCII_ZERO, dec_fillzero);

			}

			final_length = (int)strlen((const char *)addr);
			uint8_t* addr_ptr = (uint8_t*)addr;
			if (is_negative) {
				int index = *(addr_ptr + (final_length - 1)) - '0';
				addr_ptr[final_length - 1] = type_tc_negative_final_number[index];
			}
			break;
		} 
		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LS:
		{
			unsigned char* value;
			unsigned char* ptr;

			int int_fillzero;

			if (retstr[0] == '-') {
				((uint8_t *)addr)[0] = '-';
				value = (unsigned char *)retstr + 1;
			}
			else {
				((uint8_t*)addr)[0] = '+';
				value = (unsigned char*)retstr;
			}

			// before decimal point
			int beforedp = 0;
			for (ptr = value; *ptr != '\0'; ptr++) {
				if (*ptr == _decimal_point) {
					break;
				}
				else {
					beforedp++;
				}
			}

			int_fillzero = length - beforedp + power;
			memset(addr, ASCII_ZERO, int_fillzero);
			if (beforedp <= length) {
				memcpy((uint8_t*)addr + int_fillzero, value, beforedp);
			}
			else {	// Truncation will occur
				memcpy((uint8_t*)addr, value + (beforedp - this->length), length);
				*sqlcode = DBERR_NUM_OUT_OF_RANGE;
			}

			if (power < 0) {
				int afterdp = 0;

				if (*ptr != '\0') {
					ptr++;

					// after decimal point
					for (; *ptr != '\0'; ptr++) {
						afterdp++;
					}

					// fill zero
					memcpy((uint8_t*)addr + SIGN_LENGTH + int_fillzero + beforedp,
						value + beforedp + DECIMAL_LENGTH, afterdp);
				}

				int dec_fillzero = -power - afterdp;
				ptr = ((uint8_t*)addr + SIGN_LENGTH + int_fillzero + beforedp) + afterdp;
				memset(ptr, ASCII_ZERO, dec_fillzero);
			}
			break;
		} 

		case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER_PD:
		{
			display_to_comp3(retstr, false);
			break;
		} 

		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_PD:
		{
			display_to_comp3(retstr, true);
			break;
		}

		case CobolVarType::COBOL_TYPE_ALPHANUMERIC:

			if (!is_variable_length) {
				if (datalen >= length) {
					memcpy(addr, retstr, length);
				}
				else {
					char pad_char = is_binary ? 0 : ' ';
					memset(addr, pad_char, length);
					memcpy(addr, retstr, datalen);
				}
			}
			else {
				void* actual_addr = (uint8_t*)addr + VARLEN_LENGTH_SZ;
				int actual_len = length - VARLEN_LENGTH_SZ;
				if (datalen >= actual_len) {
					memcpy(actual_addr, retstr, actual_len);
				}
				else {
					char pad_char = is_binary ? 0 : ' ';
					memset(actual_addr, pad_char, actual_len);
					memcpy(actual_addr, retstr, datalen);
				}
				VARLEN_LENGTH_T* fld_len_addr = (VARLEN_LENGTH_T *)addr;
				//*fld_len_addr = VARLEN_BSWAP((VARLEN_LENGTH_T) datalen);
				*fld_len_addr = ((VARLEN_LENGTH_T) datalen);
			}
			break;

		case CobolVarType::COBOL_TYPE_JAPANESE:

			if (strlen(retstr) >= length * 2) {
				memcpy(addr, retstr, length * 2);
			}
			else {
				int i;
				char* tmp = (char*)addr;
				for (i = 0; i + 1 < length * 2; i = i + 2) {
					tmp[i] = 0x81;
					tmp[i + 1] = 0x40;
				}
				memcpy(addr, retstr, strlen(retstr));
			}
			break;

		case CobolVarType::COBOL_TYPE_UNSIGNED_BINARY:

			if (this->length == 1) {	// 1 byte
				*((uint8_t*)addr) = (uint8_t)strtoull(retstr, NULL, 0);
			}
			else {
				if (this->length == 2) {	// 1 byte
					uint8_t  u_byte_number = (uint8_t)strtoull(retstr, NULL, 0);
					*((uint8_t*)addr) = u_byte_number;
				}
				else {
					if (this->length == 3 || this->length == 4) {	// 2 bytes
						uint16_t  u_short_number = (uint16_t)strtoull(retstr, NULL, 0);
						*((uint16_t*)addr) = COB_BSWAP_16(u_short_number);
					}
					else {
						if (this->length >= 5 || this->length <= 9) {	// 4 bytes
							uint32_t  u_int_number = (uint32_t)strtoull(retstr, NULL, 0);
							uint32_t t = COB_BSWAP_32(u_int_number);
							*((uint32_t*)addr) = COB_BSWAP_32(u_int_number);
						}
						else {
							if (this->length >= 10 || this->length <= 18) {	// 8 bytes
								uint64_t  u_long_number = (uint64_t)strtoull(retstr, NULL, 0);
								*((uint64_t*)addr) = COB_BSWAP_64(u_long_number);
							}
							else {
								// Should never happen - TODO: log fatal and abort
							}
						}
					}
				}
			}
			break;

		case CobolVarType::COBOL_TYPE_SIGNED_BINARY:

			if (this->length == 1) {	// 1 byte
				*((int8_t*)addr) = (int8_t)strtoll(retstr, NULL, 0);
			}
			else {
				if (this->length == 2) {	// 1 byte
					int8_t  s_byte_number = (int8_t)strtoll(retstr, NULL, 0);
					*((int8_t*)addr) = s_byte_number;
				}
				else {
					if (this->length == 3 || this->length == 4) {	// 2 bytes
						int16_t  s_short_number = (int16_t)strtoll(retstr, NULL, 0);
						*((int16_t*)addr) = COB_BSWAP_16(s_short_number);
					}
					else {
						if (this->length >= 5 || this->length <= 9) {	// 4 bytes
							int32_t  s_int_number = (int32_t)strtoll(retstr, NULL, 0);
							*((int32_t*)addr) = COB_BSWAP_32(s_int_number);
						}
						else {
							if (this->length >= 10 || this->length <= 18) {	// 8 bytes
								int64_t  s_long_number = (int64_t)strtoll(retstr, NULL, 0);
								*((int64_t*)addr) = COB_BSWAP_64(s_long_number);
							}
							else {
								// Should never happen - TODO: log fatal and abort
							}
						}
					}
				}
			}
			break;

		default:
			break;
	}
}

void SqlVar::createCobolDataLowValue()
{
	memset(addr, 0, length);
}

CobolVarType SqlVar::getType()
{
	return type;
}

unsigned long SqlVar::getLength()
{
	return length;
}

//unsigned long SqlVar::getRealDataLength()
//{
//	if (!is_variable_length)
//		return is_autotrim ? trimmed_len : realdata_len;
//	else
//		return (realdata_len - VARLEN_LENGTH_SZ);
//}

unsigned long SqlVar::getDbDataLength()
{
	return db_data_len;
}

uint32_t SqlVar::getFlags()
{
	return flags;
}

bool SqlVar::isVarLen()
{
	return is_variable_length;
}

bool SqlVar::isBinary()
{
	return is_binary;
}

bool SqlVar::isAutoTrim()
{
	return is_autotrim;
}

void SqlVar::display_to_comp3(const char *data, bool has_sign) // , int total_len, int scale, int has_sign, uint8_t *addr
{
	uint8_t *addr = (uint8_t *) this->addr;
	bool is_negative = false;
	bool data_has_sign = false;

	// normalize

	uint8_t *tmp = (uint8_t *)malloc(this->length + 1);
	memset(tmp, ASCII_ZERO, this->length);
	tmp[this->length] = 0;

	int data_intpart_len = 0, data_decpart_len = 0;
	int dlen = 0;
	bool data_has_dp = false;

	for (uint8_t *ptr = (uint8_t *)(data + (strlen(data) - 1)); ptr >= (uint8_t *)data; ptr--) {
		if (*ptr == '-' || *ptr == '+')
			continue;

		if (*ptr == _decimal_point) {
			data_decpart_len = dlen;
			data_has_dp = true;
			break;
		}
		dlen++;
	}

	data_has_sign = (has_sign && (*data == '-') || (*data == '+'));
	if (has_sign && *data == '-') {
		is_negative = true;
	}

	data_intpart_len = strlen(data) - (data_decpart_len + (data_has_dp ? 1 : 0) + (data_has_sign ? 1 : 0));

	unsigned int abs_power = abs(this->power);
	unsigned int disp_intpart_len = this->length - abs_power;
	unsigned int disp_decpart_len = abs_power;

	// check for truncation (integer part)
	memcpy(tmp + (disp_intpart_len - data_intpart_len), data + (data_has_sign ? 1 : 0), data_intpart_len);

	if (disp_decpart_len > 0 && data_decpart_len > 0) {
		// check for truncation (decimal part)
		memcpy(tmp + disp_intpart_len, data + data_intpart_len + DECIMAL_LENGTH + (data_has_sign ? 1 : 0), data_decpart_len);
	}

	// convert
	int i; // string index
	int j; // byte array index
	bool nibble_ordinal = false;
	char ch1;
	uint8_t nibble;

	uint8_t *pknum = addr;

	i = this->length - 1;
	int comp3_len = (this->length / 2) + 1;
	j = comp3_len - 1; /* byte index */

	memset(pknum, 0, comp3_len);

	pknum[j] = has_sign ? 0x0c : 0x0f; // start with positive sign (if unsigned), otherwise 0x0f)

	while (i > -1) {
		ch1 = *(tmp + i);
		if ('0' <= ch1 && '9' >= ch1) {
			if (j < 0) {
				fprintf(stderr, "Invalid COMP-3 data");
				return;
			}
			nibble = (uint8_t)(ch1 - '0');
			if (nibble_ordinal) {
				pknum[j] = (uint8_t)(pknum[j] | nibble);
				nibble_ordinal ^= true;
			}
			else {
				pknum[j] = (uint8_t)(pknum[j] | nibble << 4);
				nibble_ordinal ^= true;
				--j;
			}
			--i; // get next char
		}
		else {
			--i; // get next char
		}
	}

	if (is_negative) {
		pknum[comp3_len - 1] = (uint8_t)(pknum[comp3_len - 1] & 0xf0);
		pknum[comp3_len - 1] = (uint8_t)(pknum[comp3_len - 1] | 0x0d);
	}

	free(tmp);
}

void SqlVar::allocate_realdata_buffer()
{
	switch (type) {
		case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER:
		{
			db_data_buffer_len = length;
			if (power < 0) {
				db_data_buffer_len++;
			}
			break;
		}
		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_TC:
		{
			db_data_buffer_len = SIGN_LENGTH + length;
			if (power < 0) {
				db_data_buffer_len++;
			}
			break;
		}
		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_LS:
		{
			db_data_buffer_len = SIGN_LENGTH + length;
			if (power < 0) {
				db_data_buffer_len++;
			}
			break;
		}
		case CobolVarType::COBOL_TYPE_UNSIGNED_NUMBER_PD:
		{
			db_data_buffer_len = length;
			if (power < 0) {
				db_data_buffer_len++;
			}
			break;
		}
		case CobolVarType::COBOL_TYPE_SIGNED_NUMBER_PD:
		{
			db_data_buffer_len = SIGN_LENGTH + length;
			if (power < 0) {
				db_data_buffer_len++;
			}
			break;
		}

		case CobolVarType::COBOL_TYPE_JAPANESE:
			length = length * 2;
			/* no break */
		case CobolVarType::COBOL_TYPE_ALPHANUMERIC:
			db_data_buffer_len = length;	// we always allocate the maximum size, to handle variable length types (the length field includes the extra 2 bytes)
			break;

		case CobolVarType::COBOL_TYPE_UNSIGNED_BINARY:
			db_data_buffer_len = length;
			break;

		case CobolVarType::COBOL_TYPE_SIGNED_BINARY:
			db_data_buffer_len = length;
			break;

		default:
			db_data_buffer_len = length;
			break;
	}

	//if (realdata_len)
	//	realdata = (char *)calloc(realdata_len + TERMINAL_LENGTH, sizeof(char));

	if (db_data_buffer_len)
		db_data_buffer = std_binary_data(db_data_buffer_len);

}

//void rtrim(char* const s)
//{
//	size_t len;
//	char* cur;
//
//	if (s && *s) {
//		len = strlen(s);
//		cur = s + len - 1;
//
//		while (cur != s && isspace(*cur))
//			--cur, --len;
//
//		cur[isspace(*cur) ? 0 : 1] = '\0';
//	}
//
//}

int get_trimmed_length(char* const s, int l)
{
	char* cur;

	if (s && *s) {
		cur = s + l - 1;

		while (cur != s && isspace(*cur))
			--cur, --l;

		return l;
	}
	return 0;
}
