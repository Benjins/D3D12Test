
#include "dxbc_hash.h"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stdint.h"



// 
// These are my own shims
//

namespace bx
{

inline ::uint32_t uint32_ror(::uint32_t val, ::uint32_t shift)
{
	return (((val) >> (shift)) | ((val) << (32 - (shift))));
}

inline ::uint32_t uint32_rol(::uint32_t val, ::uint32_t shift)
{
	return (((val) << (shift)) | ((val) >> (32 - (shift))));
}

inline ::uint32_t uint32_xor(::uint32_t a, ::uint32_t b)
{
	return a ^ b;
}

inline ::uint32_t uint32_and(::uint32_t a, ::uint32_t b)
{
	return a & b;
}

inline ::uint32_t uint32_orc(::uint32_t a, ::uint32_t b)
{
	return a | ~b;
}


void* memSet(void* ptr, int value, size_t num)
{
	return ::memset(ptr, value, num);
}

void* memCopy(void* destination, const void* source, size_t num)
{
	return ::memcpy(destination, source, num);
}

}

//
// All subsequent code in this file is from https://github.com/bkaradzic/bgfx
// Copyright 2010-2020 Branimir Karadzic
// Full license found at https://github.com/bkaradzic/bgfx/blob/master/LICENSE
// or https://raw.githubusercontent.com/bkaradzic/bgfx/master/LICENSE
// Accessed Dec. 29, 2020
//

/*
Copyright 2010-2020 Branimir Karadzic

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this
	  list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
*/

inline uint32_t dxbcMixF(uint32_t _b, uint32_t _c, uint32_t _d)
{
	const uint32_t tmp0 = bx::uint32_xor(_c, _d);
	const uint32_t tmp1 = bx::uint32_and(_b, tmp0);
	const uint32_t result = bx::uint32_xor(_d, tmp1);

	return result;
}

inline uint32_t dxbcMixG(uint32_t _b, uint32_t _c, uint32_t _d)
{
	return dxbcMixF(_d, _b, _c);
}

inline uint32_t dxbcMixH(uint32_t _b, uint32_t _c, uint32_t _d)
{
	const uint32_t tmp0 = bx::uint32_xor(_b, _c);
	const uint32_t result = bx::uint32_xor(_d, tmp0);

	return result;
}

inline uint32_t dxbcMixI(uint32_t _b, uint32_t _c, uint32_t _d)
{
	const uint32_t tmp0 = bx::uint32_orc(_b, _d);
	const uint32_t result = bx::uint32_xor(_c, tmp0);

	return result;
}

void dxbcHashBlock(const uint32_t* data, uint32_t* hash)
{
	const uint32_t d0 = data[0];
	const uint32_t d1 = data[1];
	const uint32_t d2 = data[2];
	const uint32_t d3 = data[3];
	const uint32_t d4 = data[4];
	const uint32_t d5 = data[5];
	const uint32_t d6 = data[6];
	const uint32_t d7 = data[7];
	const uint32_t d8 = data[8];
	const uint32_t d9 = data[9];
	const uint32_t d10 = data[10];
	const uint32_t d11 = data[11];
	const uint32_t d12 = data[12];
	const uint32_t d13 = data[13];
	const uint32_t d14 = data[14];
	const uint32_t d15 = data[15];

	uint32_t aa = hash[0];
	uint32_t bb = hash[1];
	uint32_t cc = hash[2];
	uint32_t dd = hash[3];

	aa = bb + bx::uint32_rol(aa + dxbcMixF(bb, cc, dd) + d0 + 0xd76aa478, 7);
	dd = aa + bx::uint32_rol(dd + dxbcMixF(aa, bb, cc) + d1 + 0xe8c7b756, 12);
	cc = dd + bx::uint32_ror(cc + dxbcMixF(dd, aa, bb) + d2 + 0x242070db, 15);
	bb = cc + bx::uint32_ror(bb + dxbcMixF(cc, dd, aa) + d3 + 0xc1bdceee, 10);
	aa = bb + bx::uint32_rol(aa + dxbcMixF(bb, cc, dd) + d4 + 0xf57c0faf, 7);
	dd = aa + bx::uint32_rol(dd + dxbcMixF(aa, bb, cc) + d5 + 0x4787c62a, 12);
	cc = dd + bx::uint32_ror(cc + dxbcMixF(dd, aa, bb) + d6 + 0xa8304613, 15);
	bb = cc + bx::uint32_ror(bb + dxbcMixF(cc, dd, aa) + d7 + 0xfd469501, 10);
	aa = bb + bx::uint32_rol(aa + dxbcMixF(bb, cc, dd) + d8 + 0x698098d8, 7);
	dd = aa + bx::uint32_rol(dd + dxbcMixF(aa, bb, cc) + d9 + 0x8b44f7af, 12);
	cc = dd + bx::uint32_ror(cc + dxbcMixF(dd, aa, bb) + d10 + 0xffff5bb1, 15);
	bb = cc + bx::uint32_ror(bb + dxbcMixF(cc, dd, aa) + d11 + 0x895cd7be, 10);
	aa = bb + bx::uint32_rol(aa + dxbcMixF(bb, cc, dd) + d12 + 0x6b901122, 7);
	dd = aa + bx::uint32_rol(dd + dxbcMixF(aa, bb, cc) + d13 + 0xfd987193, 12);
	cc = dd + bx::uint32_ror(cc + dxbcMixF(dd, aa, bb) + d14 + 0xa679438e, 15);
	bb = cc + bx::uint32_ror(bb + dxbcMixF(cc, dd, aa) + d15 + 0x49b40821, 10);

	aa = bb + bx::uint32_rol(aa + dxbcMixG(bb, cc, dd) + d1 + 0xf61e2562, 5);
	dd = aa + bx::uint32_rol(dd + dxbcMixG(aa, bb, cc) + d6 + 0xc040b340, 9);
	cc = dd + bx::uint32_rol(cc + dxbcMixG(dd, aa, bb) + d11 + 0x265e5a51, 14);
	bb = cc + bx::uint32_ror(bb + dxbcMixG(cc, dd, aa) + d0 + 0xe9b6c7aa, 12);
	aa = bb + bx::uint32_rol(aa + dxbcMixG(bb, cc, dd) + d5 + 0xd62f105d, 5);
	dd = aa + bx::uint32_rol(dd + dxbcMixG(aa, bb, cc) + d10 + 0x02441453, 9);
	cc = dd + bx::uint32_rol(cc + dxbcMixG(dd, aa, bb) + d15 + 0xd8a1e681, 14);
	bb = cc + bx::uint32_ror(bb + dxbcMixG(cc, dd, aa) + d4 + 0xe7d3fbc8, 12);
	aa = bb + bx::uint32_rol(aa + dxbcMixG(bb, cc, dd) + d9 + 0x21e1cde6, 5);
	dd = aa + bx::uint32_rol(dd + dxbcMixG(aa, bb, cc) + d14 + 0xc33707d6, 9);
	cc = dd + bx::uint32_rol(cc + dxbcMixG(dd, aa, bb) + d3 + 0xf4d50d87, 14);
	bb = cc + bx::uint32_ror(bb + dxbcMixG(cc, dd, aa) + d8 + 0x455a14ed, 12);
	aa = bb + bx::uint32_rol(aa + dxbcMixG(bb, cc, dd) + d13 + 0xa9e3e905, 5);
	dd = aa + bx::uint32_rol(dd + dxbcMixG(aa, bb, cc) + d2 + 0xfcefa3f8, 9);
	cc = dd + bx::uint32_rol(cc + dxbcMixG(dd, aa, bb) + d7 + 0x676f02d9, 14);
	bb = cc + bx::uint32_ror(bb + dxbcMixG(cc, dd, aa) + d12 + 0x8d2a4c8a, 12);

	aa = bb + bx::uint32_rol(aa + dxbcMixH(bb, cc, dd) + d5 + 0xfffa3942, 4);
	dd = aa + bx::uint32_rol(dd + dxbcMixH(aa, bb, cc) + d8 + 0x8771f681, 11);
	cc = dd + bx::uint32_rol(cc + dxbcMixH(dd, aa, bb) + d11 + 0x6d9d6122, 16);
	bb = cc + bx::uint32_ror(bb + dxbcMixH(cc, dd, aa) + d14 + 0xfde5380c, 9);
	aa = bb + bx::uint32_rol(aa + dxbcMixH(bb, cc, dd) + d1 + 0xa4beea44, 4);
	dd = aa + bx::uint32_rol(dd + dxbcMixH(aa, bb, cc) + d4 + 0x4bdecfa9, 11);
	cc = dd + bx::uint32_rol(cc + dxbcMixH(dd, aa, bb) + d7 + 0xf6bb4b60, 16);
	bb = cc + bx::uint32_ror(bb + dxbcMixH(cc, dd, aa) + d10 + 0xbebfbc70, 9);
	aa = bb + bx::uint32_rol(aa + dxbcMixH(bb, cc, dd) + d13 + 0x289b7ec6, 4);
	dd = aa + bx::uint32_rol(dd + dxbcMixH(aa, bb, cc) + d0 + 0xeaa127fa, 11);
	cc = dd + bx::uint32_rol(cc + dxbcMixH(dd, aa, bb) + d3 + 0xd4ef3085, 16);
	bb = cc + bx::uint32_ror(bb + dxbcMixH(cc, dd, aa) + d6 + 0x04881d05, 9);
	aa = bb + bx::uint32_rol(aa + dxbcMixH(bb, cc, dd) + d9 + 0xd9d4d039, 4);
	dd = aa + bx::uint32_rol(dd + dxbcMixH(aa, bb, cc) + d12 + 0xe6db99e5, 11);
	cc = dd + bx::uint32_rol(cc + dxbcMixH(dd, aa, bb) + d15 + 0x1fa27cf8, 16);
	bb = cc + bx::uint32_ror(bb + dxbcMixH(cc, dd, aa) + d2 + 0xc4ac5665, 9);

	aa = bb + bx::uint32_rol(aa + dxbcMixI(bb, cc, dd) + d0 + 0xf4292244, 6);
	dd = aa + bx::uint32_rol(dd + dxbcMixI(aa, bb, cc) + d7 + 0x432aff97, 10);
	cc = dd + bx::uint32_rol(cc + dxbcMixI(dd, aa, bb) + d14 + 0xab9423a7, 15);
	bb = cc + bx::uint32_ror(bb + dxbcMixI(cc, dd, aa) + d5 + 0xfc93a039, 11);
	aa = bb + bx::uint32_rol(aa + dxbcMixI(bb, cc, dd) + d12 + 0x655b59c3, 6);
	dd = aa + bx::uint32_rol(dd + dxbcMixI(aa, bb, cc) + d3 + 0x8f0ccc92, 10);
	cc = dd + bx::uint32_rol(cc + dxbcMixI(dd, aa, bb) + d10 + 0xffeff47d, 15);
	bb = cc + bx::uint32_ror(bb + dxbcMixI(cc, dd, aa) + d1 + 0x85845dd1, 11);
	aa = bb + bx::uint32_rol(aa + dxbcMixI(bb, cc, dd) + d8 + 0x6fa87e4f, 6);
	dd = aa + bx::uint32_rol(dd + dxbcMixI(aa, bb, cc) + d15 + 0xfe2ce6e0, 10);
	cc = dd + bx::uint32_rol(cc + dxbcMixI(dd, aa, bb) + d6 + 0xa3014314, 15);
	bb = cc + bx::uint32_ror(bb + dxbcMixI(cc, dd, aa) + d13 + 0x4e0811a1, 11);
	aa = bb + bx::uint32_rol(aa + dxbcMixI(bb, cc, dd) + d4 + 0xf7537e82, 6);
	dd = aa + bx::uint32_rol(dd + dxbcMixI(aa, bb, cc) + d11 + 0xbd3af235, 10);
	cc = dd + bx::uint32_rol(cc + dxbcMixI(dd, aa, bb) + d2 + 0x2ad7d2bb, 15);
	bb = cc + bx::uint32_ror(bb + dxbcMixI(cc, dd, aa) + d9 + 0xeb86d391, 11);

	hash[0] += aa;
	hash[1] += bb;
	hash[2] += cc;
	hash[3] += dd;
}

// dxbc hash function is slightly modified version of MD5 hash.
// https://web.archive.org/web/20190207230524/https://tools.ietf.org/html/rfc1321
// https://web.archive.org/web/20190207230538/http://www.efgh.com/software/md5.txt
//
// Assumption is that data pointer, size are both 4-byte aligned,
// and little endian.
//
void dxbcHash(const void* _data, uint32_t _size, void* _digest)
{
	uint32_t hash[4] =
	{
		0x67452301,
		0xefcdab89,
		0x98badcfe,
		0x10325476,
	};

	const uint32_t* data = (const uint32_t*)_data;
	for (uint32_t ii = 0, num = _size / 64; ii < num; ++ii)
	{
		dxbcHashBlock(data, hash);
		data += 16;
	}

	uint32_t last[16];
	bx::memSet(last, 0, sizeof(last));

	const uint32_t remaining = _size & 0x3f;

	if (remaining >= 56)
	{
		bx::memCopy(&last[0], data, remaining);
		last[remaining / 4] = 0x80;
		dxbcHashBlock(last, hash);

		bx::memSet(&last[1], 0, 56);
	}
	else
	{
		bx::memCopy(&last[1], data, remaining);
		last[1 + remaining / 4] = 0x80;
	}

	last[0] = _size * 8;
	last[15] = _size * 2 + 1;
	dxbcHashBlock(last, hash);

	bx::memCopy(_digest, hash, 16);
}

