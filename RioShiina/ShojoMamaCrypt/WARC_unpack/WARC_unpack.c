/*
用于解包文件头为WARC 1.7的WAR文件
made by Darkness-TX
2018.05.04
*/
#define _CRT_SECURE_NO_WARNINGS
#include "WARC_Decompress.h"
#include <png.h>
#define PI 3.1415926535897931

typedef unsigned char unit8;
typedef unsigned short unit16;
typedef unsigned int unit32;

struct warc_header
{
	char magic[8]; // WARC 1.7
	unit32 index_offset;
} WARC_Header;

struct warc_info
{
	unit8 name[32];
	unit32 offset;
	unit32 comprlen;
	unit32 uncomprlen;
	FILETIME time_stamp;
	unit32 flags;
} WARC_Info[5000];

unit32 FileNum = 0; // 总文件数，初始计数为0

unit32 Rand = 0;
char WARC_key[MAX_PATH];
unit32 Version = 0;			  // BR是2.48版本,2480或者0x9B0,SJ是2.50版本或者0x9C4
unit8 *RioShiinaImage = NULL; // RioShiina图片
unit8 *Region = NULL;		  // RioShiina2.png的BGRA排列
unit8 *Extra = NULL;		  // 诡异的东西
/*
第三张图，绿底裸女，不知是哪个傻屌的硬盘中翻出来的，
2.50版本出现，不知道其他是否通用，解密和之前的算法差不多，
不过直接在exe中搜索PNG头没有找到，从函数的调用来看或许是运行时动态解压到内存的，
判断方法是搜索push 202和push 204，跟踪push 202那个，
然后会调用GetProcAddress之后跳转到了内存中的解密函数。
烦的一匹，实在没什么可说的，吔屎！！！
*/
unit8 *ExtraImage = NULL;
unit32 Seed = 0;
unit32 key_src[5] = {0, 0, 0, 0, 0};
/*flag & 0x40000000 == 1时要用到，但是有些时候又不需要，很奇特，搜0x40000000然后跳转，
其中有块0x2000的内存，就是TM的decrypt2所在的那块内存，绝了
*/
unit8 *DecodeBin = NULL;

unit32 warc_max_index_length(unit32 WARC_version)
{
	unit32 entry_size, max_index_entries;

	if (WARC_version < 150)
	{
		entry_size = 0x38; // sizeof(warc_info)
		max_index_entries = 8192;
	}
	else
	{
		entry_size = 0x38; // sizeof(warc_info)
		max_index_entries = 16384;
	}
	return entry_size * max_index_entries;
}

unit32 get_rand()
{
	Rand = 0x5D588B65 * Rand + 1;
	return Rand;
}

unit32 BigEndian(unit32 u)
{
	return u << 24 | (u & 0xff00) << 8 | (u & 0xff0000) >> 8 | u >> 24;
}

DWORD CheckString(wchar_t *buff)
{
	if (wcsncmp(buff, L"0x", 2) == 0 || wcsncmp(buff, L"0X", 2) == 0)
		return wcstoul(buff + 2, NULL, 16);
	else
		return wcstoul(buff, NULL, 10);
}

void ReadPng(FILE *src)
{
	png_structp png_ptr;
	png_infop info_ptr, end_ptr;
	png_bytep *rows;
	unit32 i, width = 0, height = 0, bpp = 0, format = 0;
	unit8 buff;

	// 验证文件头
	png_byte header[8];
	fread(header, 1, 8, src);
	if (png_sig_cmp(header, 0, 8))
	{
		printf("文件不是有效的PNG图像!\n");
		exit(0);
	}
	fseek(src, 0, SEEK_SET);

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL)
	{
		printf("PNG信息创建失败!\n");
		exit(0);
	}
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		printf("info信息创建失败!\n");
		png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		exit(0);
	}
	end_ptr = png_create_info_struct(png_ptr);
	if (end_ptr == NULL)
	{
		printf("end信息创建失败!\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
		exit(0);
	}
	png_init_io(png_ptr, src);
	png_read_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, (png_uint_32 *)&width, (png_uint_32 *)&height, &bpp, &format, NULL, NULL, NULL);
	if (bpp == 8 && format == PNG_COLOR_TYPE_RGB_ALPHA)
	{
		Region = malloc(width * height * 4);
		rows = (png_bytep *)malloc(height * sizeof(char *));
		for (i = 0; i < height; i++)
			rows[i] = (png_bytep)(Region + width * i * 4);
		png_read_image(png_ptr, rows);
		free(rows);
		png_read_end(png_ptr, info_ptr);
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_ptr);
		for (i = 0; i < height * width; i++)
		{
			buff = Region[i * 4];
			Region[i * 4] = Region[i * 4 + 2];
			Region[i * 4 + 2] = buff;
		}
	}
	else
	{
		printf("Region块的图片不是bpp为8或不是32位RGBA图片，不符合规范！\n");
		system("pause");
		exit(0);
	}
}

void Init()
{
	FILE *src = NULL;
	unit32 size = 0;
	wchar_t filePath[MAX_PATH];
	wchar_t dirPath[MAX_PATH];
	wchar_t iniPath[MAX_PATH];
	wchar_t buff[20];
	GetCurrentDirectoryW(MAX_PATH, dirPath);
	wsprintfW(iniPath, L"%ls\\%ls", dirPath, L"RioShiina.ini");
	if (_waccess(iniPath, 4) == -1)
	{
		wprintf(L"初始化失败，请确认目录下是否含有RioShiina.ini\n");
		system("pause");
		exit(0);
	}
	GetPrivateProfileStringW(L"RioShiina", L"Version", L"2480", buff, MAX_PATH, iniPath);
	Version = CheckString(buff);
	GetPrivateProfileStringW(L"RioShiina", L"Key1", L"0", buff, MAX_PATH, iniPath);
	key_src[0] = CheckString(buff);
	GetPrivateProfileStringW(L"RioShiina", L"Key2", L"0", buff, MAX_PATH, iniPath);
	key_src[1] = CheckString(buff);
	GetPrivateProfileStringW(L"RioShiina", L"Key3", L"0", buff, MAX_PATH, iniPath);
	key_src[2] = CheckString(buff);
	GetPrivateProfileStringW(L"RioShiina", L"Key4", L"0", buff, MAX_PATH, iniPath);
	key_src[3] = CheckString(buff);
	GetPrivateProfileStringW(L"RioShiina", L"Key5", L"0", buff, MAX_PATH, iniPath);
	key_src[4] = CheckString(buff);
	printf("Version:%d key_src:0x%X|0x%X|0x%X|0x%X|0x%X\n", Version, key_src[0], key_src[1], key_src[2], key_src[3], key_src[4]);
	GetPrivateProfileStringW(L"Image", L"RioShiinaImage", L"", filePath, MAX_PATH, iniPath);
	if (wcscmp(filePath, L"") == 0)
	{
		wprintf(L"初始化图片失败，请确认RioShiina.ini中RioShiinaImage属性是否有值\n");
		system("pause");
		exit(0);
	}
	wprintf(L"Image:%ls\n", filePath);
	src = _wfopen(filePath, L"rb");
	if (src == NULL)
	{
		wprintf(L"初始化图片失败，请确认目录下是否含有%ls\n", filePath);
		system("pause");
		exit(0);
	}
	fseek(src, 0, SEEK_END);
	size = ftell(src);
	RioShiinaImage = malloc(size);
	fseek(src, 0, SEEK_SET);
	fread(RioShiinaImage, size, 1, src);
	fclose(src);
	GetPrivateProfileStringW(L"Image", L"Region", L"", filePath, MAX_PATH, iniPath);
	if (wcscmp(filePath, L"") == 0)
	{
		wprintf(L"初始化图片失败，请确认RioShiina.ini中Region属性是否有值\n");
		system("pause");
		exit(0);
	}
	wprintf(L"Region:%ls\n\n", filePath);
	src = _wfopen(filePath, L"rb");
	if (src == NULL)
	{
		wprintf(L"初始化图片失败，请确认目录下是否含有%ls\n", filePath);
		system("pause");
		exit(0);
	}
	ReadPng(src);
	fclose(src);
	if (Version == 2500)
	{
		GetPrivateProfileStringW(L"RioShiina", L"Seed", L"0", buff, MAX_PATH, iniPath);
		Seed = CheckString(buff);
		wprintf(L"Seed:0x%X\n\n", Seed);
		GetPrivateProfileStringW(L"Image", L"ExtraImage", L"", filePath, MAX_PATH, iniPath);
		if (wcscmp(filePath, L"") == 0)
		{
			wprintf(L"初始化图片失败，请确认RioShiina.ini中ExtraImage属性是否有值\n");
			system("pause");
			exit(0);
		}
		wprintf(L"ExtraImage:%ls\n\n", filePath);
		src = _wfopen(filePath, L"rb");
		if (src == NULL)
		{
			wprintf(L"初始化图片失败，请确认目录下是否含有%ls\n", filePath);
			system("pause");
			exit(0);
		}
		fseek(src, 0, SEEK_END);
		size = ftell(src);
		ExtraImage = malloc(size);
		fseek(src, 0, SEEK_SET);
		fread(ExtraImage, size, 1, src);
		fclose(src);
		GetPrivateProfileStringW(L"RioShiina", L"DecodeBin", L"", filePath, MAX_PATH, iniPath);
		if (wcscmp(filePath, L"") == 0)
		{
			wprintf(L"初始化失败，请确认RioShiina.ini中DecodeBin属性是否有值\n");
			system("pause");
			exit(0);
		}
		wprintf(L"DecodeBin:%ls\n\n", filePath);
		src = _wfopen(filePath, L"rb");
		if (src == NULL)
		{
			wprintf(L"初始化失败，请确认目录下是否含有%ls\n", filePath);
			system("pause");
			exit(0);
		}
		fseek(src, 0, SEEK_END);
		size = ftell(src);
		DecodeBin = malloc(size);
		fseek(src, 0, SEEK_SET);
		fread(DecodeBin, size, 1, src);
		fclose(src);
		GetPrivateProfileStringW(L"RioShiina", L"Extra", L"", filePath, MAX_PATH, iniPath);
		if (wcscmp(filePath, L"") == 0)
		{
			wprintf(L"初始化失败，请确认RioShiina.ini中Extra属性是否有值\n");
			system("pause");
			exit(0);
		}
		wprintf(L"Extra:%ls\n\n", filePath);
		src = _wfopen(filePath, L"rb");
		if (src == NULL)
		{
			wprintf(L"初始化失败，请确认目录下是否含有%ls\n", filePath);
			system("pause");
			exit(0);
		}
		fseek(src, 0, SEEK_END);
		size = ftell(src);
		Extra = malloc(size);
		fseek(src, 0, SEEK_SET);
		fread(Extra, size, 1, src);
		fclose(src);
	}
}

unit32 *InitCrcTable()
{
	unit32 table[0x100];
	for (unit32 i = 0; i != 256; ++i)
	{
		unit32 poly = i;
		for (unit32 j = 0; j < 8; ++j)
		{
			unit32 bit = poly & 1;
			poly = (poly >> 1) | (poly << 31);
			if (0 == bit)
				poly ^= 0x6DB88320;
		}
		table[i] = poly;
	}
	return table;
}

unit32 crc32_get(unit32 init_crc, unit8 *data, unit32 length)
{
	unit32 result = init_crc, j = 0, c = 0, i = 0;
	for (i = 0; i < length; i++)
	{
		result = (data[i] << 24) ^ result;
		for (j = 0; j < 8; j++)
		{
			c = result < 0x80000000 ? 0 : 0x4C11DB7;
			result = (result << 1) ^ c;
		}
	}
	return result;
}

unit32 RegionCrc32(unit8 *src, unit32 flags, unit32 rgb)
{
	unit32 *CustomCrcTable = InitCrcTable();
	int src_alpha = (int)flags & 0x1ff;
	int dst_alpha = (int)(flags >> 12) & 0x1ff;
	flags >>= 24;
	if (0 == (flags & 0x10))
		dst_alpha = 0;
	if (0 == (flags & 8))
		src_alpha = 0x100;
	int y_step = 0;
	int x_step = 4;
	int width = 48;
	int pos = 0;
	if (0 != (flags & 0x40)) // horizontal flip
	{
		y_step += width;
		pos += (width - 1) * 4;
		x_step = -x_step;
	}
	if (0 != (flags & 0x20)) // vertical flip
	{
		y_step -= width;
		pos += width * 0x2f * 4; // width*(height-1)*4;
	}
	y_step <<= 3;
	unit32 checksum = 0;
	for (int y = 0; y < 48; ++y)
	{
		for (int x = 0; x < 48; ++x)
		{
			int alpha = src[pos + 3] * src_alpha;
			alpha >>= 8;
			unit32 color = rgb;
			for (int i = 0; i < 3; ++i)
			{
				int v = src[pos + i];
				int c = (int)(color & 0xff); // rgb[i];
				c -= v;
				c = (c * dst_alpha) >> 8;
				c = (c + v) & 0xff;
				c = (c * alpha) >> 8;
				checksum = (checksum >> 8) ^ CustomCrcTable[(c ^ checksum) & 0xff];
				color >>= 8;
			}
			pos += x_step;
		}
		pos += y_step;
	}
	return checksum;
}

double decrypt_helper1(double a)
{
	if (a < 0)
		return -decrypt_helper1(-a);
	if (a < 18.0)
	{
		double v0 = a;
		double v1 = a;
		double v2 = -(a * a);

		for (int i = 3; i < 1000; i += 2)
		{
			v1 *= v2 / (i * (i - 1));
			v0 += v1 / i;
			if (v0 == v2)
				break;
		}
		return v0;
	}
	int flags = 0;
	double v0_l = 0;
	double v1 = 0;
	double div = 1 / a;
	double v1_h = 2.0;
	double v0_h = 2.0;
	double v1_l = 0;
	double v0 = 0;
	int i = 0;
	do
	{
		v0 += div;
		div *= ++i / a;
		if (v0 < v0_h)
			v0_h = v0;
		else
			flags |= 1;
		v1 += div;
		div *= ++i / a;
		if (v1 < v1_h)
			v1_h = v1;
		else
			flags |= 2;
		v0 -= div;
		div *= ++i / a;
		if (v0 > v0_l)
			v0_l = v0;
		else
			flags |= 4;
		v1 -= div;
		div *= ++i / a;
		if (v1 > v1_l)
			v1_l = v1;
		else
			flags |= 8;
	} while (flags != 15);
	return ((PI - cos(a) * (v0_l + v0_h)) - (sin(a) * (v1_l + v1_h))) / 2.0;
}

unit32 decrypt_helper2(unit32 WARC_version, double a)
{
	double v0, v1, v2, v3;

	if (a > 1.0)
	{
		v0 = sqrt(a * 2 - 1);
		while (1)
		{
			v1 = 1 - (double)get_rand() / 4294967296.0;
			v2 = 2.0 * (double)get_rand() / 4294967296.0 - 1.0;
			if (v1 * v1 + v2 * v2 > 1.0)
				continue;
			v2 /= v1;
			v3 = v2 * v0 + a - 1.0;
			if (v3 <= 0)
				continue;
			v1 = (a - 1.0) * log(v3 / (a - 1.0)) - v2 * v0;
			if (v1 < -50.0)
				continue;
			if (((double)get_rand() / 4294967296.0) <= (exp(v1) * (v2 * v2 + 1.0)))
				break;
		}
	}
	else
	{
		v0 = exp(1.0) / (a + exp(1.0));
		do
		{
			v1 = (double)get_rand() / 4294967296.0;
			v2 = (double)get_rand() / 4294967296.0;
			if (v1 < v0)
			{
				v3 = pow(v2, 1.0 / a);
				v1 = exp(-v3);
			}
			else
			{
				v3 = 1.0 - log(v2);
				v1 = pow(v3, a - 1.0);
			}
		} while ((double)get_rand() / 4294967296.0 >= v1);
	}
	if (WARC_version > 120)
		return (unit32)(v3 * 256.0);
	else
		return (unit8)((double)get_rand() / 4294967296.0);
}

unit32 decrypt_helper3(unit32 key)
{
	unit32 v0, v1, v2, v3;
	unit8 b0, b1, b2, b3;
	b3 = key >> 24;
	b2 = (key & 0xFF0000) >> 16;
	b1 = (key & 0xFF00) >> 8;
	b0 = key & 0xFF;
	float f;
	f = (float)(1.5 * (double)b0 + 0.1);
	v0 = BigEndian(*(unit32 *)&f);
	f = (float)(1.5 * (double)b1 + 0.1);
	v1 = (unit32)f;
	f = (float)(1.5 * (double)b2 + 0.1);
	v2 = (unit32) - *(int *)&f;
	f = (float)(1.5 * (double)b3 + 0.1);
	v3 = ~*(unit32 *)&f;
	return (v0 + v1) | (v2 - v3);
}

void decrypt_helper4(unit8 *data)
{
	unit32 code[80], key[10], i = 0;
	unit32 k0, k1, k2, k3, k4;
	unit32 *p = (unit32 *)(data + 44);
	for (i = 0; i < 0x10; i++)
		code[i] = ((p[i] & 0xFF00 | (p[i] << 16)) << 8) | (((p[i] >> 16) | p[i] & 0xFF0000) >> 8);
	for (unit32 k = 0; k < 80 - i; ++k)
	{
		unit32 tmp = code[0 + k] ^ code[2 + k] ^ code[8 + k] ^ code[13 + k];
		code[16 + k] = (tmp >> 31) | (tmp << 1);
	}
	memcpy(key, key_src, 4 * 5);
	k0 = key[0];
	k1 = key[1];
	k2 = key[2];
	k3 = key[3];
	k4 = key[4];
	for (i = 0; i < 0x50; i++)
	{
		unit32 f, c;
		if (i < 0x10)
		{
			f = k1 ^ k2 ^ k3;
			c = 0;
		}
		else if (i < 0x20)
		{
			f = k1 & k2 | k3 & ~k1;
			c = 0x5A827999;
		}
		else if (i < 0x30)
		{
			f = k3 ^ (k1 | ~k2);
			c = 0x6ED9EBA1;
		}
		else if (i < 0x40)
		{
			f = k1 & k3 | k2 & ~k3;
			c = 0x8F1BBCDC;
		}
		else
		{
			f = k1 ^ (k2 | ~k3);
			c = 0xA953FD4E;
		}
		unit32 new_k0 = code[i] + k4 + f + c + ((k0 >> 27) | (k0 << 5));
		unit32 new_k2 = (k1 >> 2) | (k1 << 30);
		k1 = k0;
		k4 = k3;
		k3 = k2;
		k2 = new_k2;
		k0 = new_k0;
	}
	key[0] += k0;
	key[1] += k1;
	key[2] += k2;
	key[3] += k3;
	key[4] += k4;
	FILETIME ft;
	ft.dwLowDateTime = key[1];
	ft.dwHighDateTime = key[0] & 0x7FFFFFFF;
	SYSTEMTIME sys_time;
	if (!FileTimeToSystemTime(&ft, &sys_time))
	{
		printf("decrypt_helper4中FileTimeToSystemTime失败!\n");
		system("pause");
		exit(0);
	}
	key[5] = (unit32)(sys_time.wYear | sys_time.wMonth << 16);
	key[7] = (unit32)(sys_time.wHour | sys_time.wMinute << 16);
	key[8] = (unit32)(sys_time.wSecond | sys_time.wMilliseconds << 16);
	unit32 flags = *p | 0x80000000;
	unit32 rgb = code[1] >> 8;
	if ((flags & 0x78000000) == 0)
		flags |= 0x98000000;
	key[6] = RegionCrc32(Region, flags, rgb);
	key[9] = (unit32)(((int)key[2] * (INT64)(int)key[3]) >> 8);
	if (Version > 2390)
		key[6] += key[9];
	unit32 *encoded = (unit32 *)(data + 4);
	for (i = 0; i < 10; i++)
		encoded[i] ^= key[i];
}

char *WARC_key_string(unit32 WARC_version)
{
	char *key = "Crypt Type %s - Copyright(C) 2000 Y.Yamada/STUDIO 傛偟偔傫"; // よしくん

	if (WARC_version <= 120)
		sprintf(WARC_key, key, "20000823");
	else
		sprintf(WARC_key, key, "20011002");

	return WARC_key;
}

void decrypt(unit32 WARC_version, unit8 *cipher, unit32 cipher_length)
{
	int a, b;
	unit32 fac = 0, idx = 0, index = 0, _cipher_length = cipher_length, ImageSize = 0;
	unit8 *Image = NULL;
	Rand = cipher_length;
	if (cipher_length < 3)
		return;
	if (cipher_length > 1024)
		cipher_length = 1024;
	if (WARC_version > 120)
	{
		a = (char)cipher[0] ^ (char)_cipher_length;
		b = (char)cipher[1] ^ (char)(_cipher_length / 2);
		if (_cipher_length != warc_max_index_length(170))
		{
			if (Version == 2500)
				ImageSize = _msize(RioShiinaImage) + _msize(Extra);
			else
				ImageSize = _msize(RioShiinaImage);
			Image = malloc(ImageSize);
			if (Version == 2500)
			{
				memcpy(Image, RioShiinaImage, _msize(RioShiinaImage));
				memcpy(Image + _msize(RioShiinaImage), Extra, _msize(Extra));
			}
			else
				memcpy(Image, RioShiinaImage, _msize(RioShiinaImage));
			idx = (unit32)((double)get_rand() * (_msize(Image) / 4294967296.0));
			if (WARC_version >= 160)
			{
				fac = Image[idx] + Rand;
				fac = decrypt_helper3(fac) & 0xfffffff;
				if (cipher_length > 0x80)
				{
					decrypt_helper4(cipher);
					index += 0x80;
					cipher_length -= 0x80;
				}
			}
			else if (WARC_version == 150)
			{
				fac = Rand + Image[idx];
				fac ^= (fac & 0xfff) * (fac & 0xfff);
				unit32 v = 0;
				for (unit32 i = 0; i < 32; ++i)
				{
					unit32 bit = fac & 1;
					fac >>= 1;
					if (0 != bit)
						v += fac;
				}
				fac = v;
			}
			else if (WARC_version == 140)
				fac = Image[idx];
			else if (WARC_version == 130)
				fac = Image[idx & 0xff];
			else
			{
				printf("不支持的WARC版本! WARC_version:%d\n", WARC_version);
				system("pause");
				exit(0);
			}
			free(Image);
		}
	}
	else
	{
		a = cipher[0];
		b = cipher[1];
	}
	Rand ^= (DWORD)(decrypt_helper1(a) * 100000000.0);
	double tmp = 0.0;
	if (0 != (a | b))
	{
		tmp = acos((double)a / sqrt((double)(a * a + b * b)));
		tmp = tmp / PI * 180.0;
	}
	if (b < 0)
		tmp = 360.0 - tmp;
	char *key_string = WARC_key_string(WARC_version);
	unit32 key_string_len = strlen(key_string);
	unit32 i = ((unit8)decrypt_helper2(WARC_version, tmp) + fac) % key_string_len;
	unit32 n = 0;
	for (unit32 k = 2; k < cipher_length; k++)
	{
		if (WARC_version > 120)
			cipher[index + k] ^= (unit8)((double)get_rand() / 16777216.0);
		else
			cipher[index + k] ^= (unit8)((double)get_rand() / 4294967296.0);
		cipher[index + k] = ((cipher[index + k] & 1) << 7) | (cipher[index + k] >> 1);
		cipher[index + k] ^= key_string[n++] ^ key_string[i];
		i = cipher[index + k] % key_string_len;
		if (n >= key_string_len)
			n = 0;
	}
}

void extra_decrypt(unit8 *data, unit32 length, unit32 flags)
{
	if (length < 0x400)
		return;

	// ShojoMamaCrypt implementation
	if (flags == 0x202) // DecryptPre
	{
		unit32 k[4];

		// InitKey - from ShojoMamaCrypt
		k[0] = Seed + 1;
		k[1] = Seed + 4;
		k[2] = Seed + 2;
		k[3] = Seed + 3;

		// EncryptedSize from ShojoMamaCrypt is 0xFF
		for (int i = 0; i < 0xFF; ++i)
		{
			unit32 j = k[3] ^ (k[3] << 11) ^ k[0] ^ ((k[3] ^ (k[3] << 11) ^ (k[0] >> 11)) >> 8);
			k[3] = k[2];
			k[2] = k[1];
			k[1] = k[0];
			k[0] = j;
			data[i] ^= ExtraImage[j % _msize(ExtraImage)];
		}
	}
	else if (flags == 0x204) // DecryptPost
	{
		// PostDataOffset is 0x200 in KeyDecryptBase
		int pos = 0x200;
		data[pos] ^= (byte)Seed;
		data[pos + 1] ^= (byte)(Seed >> 8);
		data[pos + 2] ^= (byte)(Seed >> 16);
		data[pos + 3] ^= (byte)(Seed >> 24);
	}
}

void extra_decrypt_maki(unit8 *data, unit32 length, unit32 flags)
{
	unit32 table_length = _msize(ExtraImage), i = 0, k = 0;
	if (length >= 0x400)
	{
		if (flags == 0x202) // pre
		{
			k = Seed;
			for (i = 0; i < 0x100; i++)
			{
				k = 0x343FD * k + 0x269EC3;
				data[i] ^= ExtraImage[((int)(k >> 16) & 0x7FFF) % table_length];
			}
		}
		else if (flags == 0x204) // post
		{
			data[0x200] ^= (unit8)Seed;
			data[0x201] ^= (unit8)(Seed >> 8);
			data[0x202] ^= (unit8)(Seed >> 16);
			data[0x203] ^= (unit8)(Seed >> 24);
		}
	}
}

void decrypt2(unit8 *data, unit32 length)
{
	if (length >= 0x400)
	{
		unit32 crc = crc32_get(0xFFFFFFFF, data, 0x100);
		unit32 index = 0x100;
		for (unit32 i = 0; i < 0x40; ++i)
		{
			unit32 src = *(unit32 *)&data[index] & 0x1FFC;
			src = *(unit32 *)&DecodeBin[src];
			unit32 key = src ^ crc;
			data[index++ + 0x100] ^= (byte)key;
			data[index++ + 0x100] ^= (byte)(key >> 8);
			data[index++ + 0x100] ^= (byte)(key >> 16);
			data[index++ + 0x100] ^= (byte)(key >> 24);
		}
	}
}

void ReadIndex(char *fname)
{
	FILE *src, *dst;
	char dstname[MAX_PATH];
	src = fopen(fname, "rb");
	unit32 filesize = 0, max_index_len = 0, i = 0;
	unit8 *data = NULL, *udata = NULL;
	fread(WARC_Header.magic, 1, 8, src);
	if (strncmp(WARC_Header.magic, "WARC 1.7", 8) == 0)
	{
		fread(&WARC_Header.index_offset, 4, 1, src);
		WARC_Header.index_offset ^= 0xF182AD82; // くん
		fseek(src, 0, SEEK_END);
		filesize = ftell(src);
		printf("%s WARC 1.7 index_offset:0x%X filesize:0x%X\n\n", fname, WARC_Header.index_offset, filesize);
		if (WARC_Header.index_offset > filesize)
		{
			printf("index_offset过大！index_offset:0x%X\n", WARC_Header.index_offset);
			system("pause");
			exit(0);
		}
		max_index_len = warc_max_index_length(170);
		data = malloc(max_index_len);
		memset(data, 0, max_index_len);
		fseek(src, WARC_Header.index_offset, SEEK_SET);
		fread(data, filesize - WARC_Header.index_offset, 1, src);
		decrypt(170, data, max_index_len);
		for (i = 0; i < max_index_len / 4; ++i)
			((unit32 *)data)[i] ^= WARC_Header.index_offset;
		for (i = 0; i < max_index_len; i++)
			data[i] ^= (unit8)~170;
		if (data[8] != 0x78)
		{
			printf("索引未经过zlib压缩，暂不支持此类型!\n");
			fclose(src);
			free(data);
			system("pause");
			exit(0);
		}
		unit32 udata_size = max_index_len;
		udata = malloc(udata_size);
		uncompress(udata, &udata_size, data + 8, filesize - WARC_Header.index_offset - 8);
		for (FileNum = 0; FileNum < udata_size / 0x38; FileNum++)
			memcpy(&WARC_Info[FileNum], udata + FileNum * 0x38, 0x38);
		sprintf(dstname, "%s.idx", fname);
		dst = fopen(dstname, "wb");
		fwrite(data, 8, 1, dst);
		fwrite(udata, udata_size, 1, dst);
		fclose(dst);
		free(udata);
		free(data);
	}
	else
	{
		printf("不支持的文件类型，请确认文件头为WARC 1.7\n");
		system("pause");
		exit(0);
	}
	fclose(src);
}

void UnpackFile(char *fname)
{
	FILE *src, *dst;
	unit32 i = 0;
	unit8 *cdata = NULL, *udata = NULL;
	unit32 sig = 0, uncomprlen = 0;
	unit32 dsize = 0;
	char dstname[MAX_PATH];
	WCHAR wdstname[MAX_PATH];
	src = fopen(fname, "rb");
	sprintf(dstname, "%s_unpack", fname);
	_mkdir(dstname);
	_chdir(dstname);
	for (i = 0; i < FileNum; i++)
	{
		sprintf(dstname, "%04d_%s", i, WARC_Info[i].name);
		// 那种冗余的、没用的文件的文件名会包含半角平假片假，可以不解包出来，但导出的话就要宽字节了
		dsize = MultiByteToWideChar(932, 0, dstname, strlen(dstname), NULL, 0);
		MultiByteToWideChar(932, 0, dstname, strlen(dstname), wdstname, dsize);
		wdstname[dsize] = L'\0';
		printf("%s offset:0x%X comprlen:0x%X uncomprlen:0x%X flags:0x%X ", WARC_Info[i].name, WARC_Info[i].offset, WARC_Info[i].comprlen, WARC_Info[i].uncomprlen, WARC_Info[i].flags);
		fseek(src, WARC_Info[i].offset, SEEK_SET);
		if (WARC_Info[i].comprlen < 8)
		{
			printf("type:nocompress");
			cdata = malloc(WARC_Info[i].comprlen);
			fread(cdata, WARC_Info[i].comprlen, 1, src);
			dst = _wfopen(wdstname, L"wb");
			fwrite(cdata, WARC_Info[i].comprlen, 1, dst);
			free(cdata);
			fclose(dst);
		}
		else
		{
			fread(&sig, 4, 1, src);
			fread(&uncomprlen, 4, 1, src);
			sig ^= (uncomprlen ^ 0x82AD82) & 0xFFFFFF;
			cdata = malloc(WARC_Info[i].comprlen - 8);
			fread(cdata, WARC_Info[i].comprlen - 8, 1, src);
			if (WARC_Info[i].flags & 0x80000000)
			{
				decrypt(170, cdata, WARC_Info[i].comprlen - 8);
				if (Version == 2500)
					extra_decrypt(cdata, WARC_Info[i].comprlen - 8, 0x202);
			}
			if (WARC_Info[i].flags & 0x20000000)
				decrypt2(cdata, WARC_Info[i].comprlen - 8);
			if ((sig & 0xFFFFFF) == 0x4B5059) // YPK, enter if
			{
				printf("type:YPK");
				udata = malloc(WARC_Info[i].uncomprlen);
				YPK_decompress(cdata, udata, WARC_Info[i].comprlen - 8, WARC_Info[i].uncomprlen, sig);
				free(cdata);
				if (WARC_Info[i].flags & 0x40000000)
					decrypt2(udata, WARC_Info[i].uncomprlen);
				if (Version == 2500)
					extra_decrypt(udata, WARC_Info[i].uncomprlen, 0x204);
				dst = _wfopen(wdstname, L"wb");
				fwrite(udata, WARC_Info[i].uncomprlen, 1, dst);
				free(udata);
				fclose(dst);
			}
			else if ((sig & 0xFFFFFF) == 0x314859) // YH1
			{
				printf("type:YH1");
				udata = malloc(WARC_Info[i].uncomprlen);
				YH1_decompress(cdata, udata, WARC_Info[i].comprlen - 8, WARC_Info[i].uncomprlen, sig);
				free(cdata);
				if (WARC_Info[i].flags & 0x40000000)
					decrypt2(udata, WARC_Info[i].uncomprlen);
				if (Version == 2500)
					extra_decrypt(udata, WARC_Info[i].uncomprlen, 0x204);
				dst = _wfopen(wdstname, L"wb");
				fwrite(udata, WARC_Info[i].uncomprlen, 1, dst);
				free(udata);
				fclose(dst);
			}
			else if ((sig & 0xFFFFFF) == 0x5A4C59) // YLZ
			{
				printf("type:YLZ");
				free(cdata);
				printf("未添加YLZ解压部分!\n");
				system("pause");
			}
			else
			{
				printf("type:nocompress");
				sig ^= (uncomprlen ^ 0x82AD82) & 0xFFFFFF; // 未压缩，还原回去
				dst = _wfopen(wdstname, L"wb");
				fwrite(&sig, 4, 1, dst);
				fwrite(&uncomprlen, 4, 1, dst);
				fwrite(cdata, WARC_Info[i].comprlen - 8, 1, dst);
				free(cdata);
				fclose(dst);
			}
		}
		printf("\n");
	}
	fclose(src);
}

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "chs");
	printf("project：Niflheim-RioShiina\n用于解包文件头为WARC 1.7的WAR文件。\n将war文件拖到程序上。\nby Darkness-TX 2018.05.03\n\n");
	ReadIndex(argv[1]);
	Init();
	UnpackFile(argv[1]);
	printf("已完成，总文件数%d\n", FileNum);
	system("pause");
	return 0;
}