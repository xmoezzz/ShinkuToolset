// FVPSingleDumper.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"


#include "windows.h"
#include <cstdio>
#include <string>
#include "zlib128\zlib-1.2.8\zlib.h"

#pragma comment(lib,"zlib1.lib")

using namespace std;

struct BINHDR 
{
  DWORD entry_count;
  DWORD filenames_length;
};

struct BINENTRY 
{
  DWORD filename_offset;
  DWORD offset;
  DWORD length;
};

struct HZC1HDR 
{
  BYTE  signature[4]; // "hzc1"
  DWORD original_length;
  DWORD header_length;
};

struct NVSGHDR
{
  BYTE  signature[4]; // "NVSG"
  WORD unknown1;
  WORD type;
  WORD width;
  WORD height;
  WORD offset_x;
  WORD offset_y;
  DWORD unknown2;
  DWORD entry_count;
  DWORD unknown3;
  DWORD unknown4;
};

static const WORD NVSGHDR_TYPE_SINGLE_24BIT = 0;
static const WORD NVSGHDR_TYPE_SINGLE_32BIT = 1;
static const WORD NVSGHDR_TYPE_MULTI_32BIT  = 2;
static const WORD NVSGHDR_TYPE_SINGLE_8BIT  = 3;
static const WORD NVSGHDR_TYPE_SINGLE_1BIT  = 4;


int write_bmp(const    string& filename,
               unsigned char*   buff,
               unsigned long    len,
               unsigned long    width,
               unsigned long    height,
               unsigned short   depth_bytes)
{
  BITMAPFILEHEADER bmf;
  BITMAPINFOHEADER bmi;

  memset(&bmf, 0, sizeof(bmf));
  memset(&bmi, 0, sizeof(bmi));

  bmf.bfType     = 0x4D42;
  bmf.bfSize     = sizeof(bmf) + sizeof(bmi) + len;
  bmf.bfOffBits  = sizeof(bmf) + sizeof(bmi);

  bmi.biSize     = sizeof(bmi);
  bmi.biWidth    = width;
  bmi.biHeight   = height;
  bmi.biPlanes   = 1;
  bmi.biBitCount = depth_bytes * 8;
           
  FILE* fd = fopen(filename.c_str(), "wb");
  fwrite(&bmf, 1, sizeof(bmf), fd);
  fwrite(&bmi, 1, sizeof(bmi), fd);
  fwrite(buff, 1,         len, fd);
  fclose(fd);
  return 0;
}

bool process(const string& filename, BYTE*  buff, DWORD  len)
{
	if (len < 4 || memcmp(buff, "hzc1", 4))
 	{
    	return false;
 	}

	HZC1HDR*    hzc1hdr   = (HZC1HDR*) buff;
	DWORD data_len  = len - sizeof(*hzc1hdr);
	BYTE*     data_buff = (BYTE*) (hzc1hdr + 1);

   if (data_len < 4 || memcmp(data_buff, "NVSG", 4)) 
	{
    	return false;
  	}

	NVSGHDR* nvsghdr = (NVSGHDR*) data_buff;
	data_buff += hzc1hdr->header_length;
  
	DWORD depth = 0;

	switch (nvsghdr->type)
	{
  		case NVSGHDR_TYPE_SINGLE_24BIT:
    		depth = 3;
    	break;

	  	case NVSGHDR_TYPE_SINGLE_32BIT:
  		case NVSGHDR_TYPE_MULTI_32BIT:
    		depth = 4;
    	break;

  		case NVSGHDR_TYPE_SINGLE_8BIT:
    		depth = 1;
    	break;

  		case NVSGHDR_TYPE_SINGLE_1BIT:
    		depth = 1;
    	break;

  		default:
    		return false;
	}

	DWORD out_len  = hzc1hdr->original_length;
  	BYTE*   out_buff = new BYTE[out_len];
	uncompress(out_buff, &out_len, data_buff, data_len);

	if (nvsghdr->type == NVSGHDR_TYPE_SINGLE_1BIT)
	{
    	for (DWORD i = 0; i < out_len; i++)
		{
      		if (out_buff[i] == 1) 
 			{
        		out_buff[i] = 0xFF;
      		}
    	}
  	}

  	if (nvsghdr->entry_count)
  	{
    	DWORD frame_len = nvsghdr->width * nvsghdr->height * depth;

    	for (DWORD j = 0; j < nvsghdr->entry_count; j++) 
		{
			char sub_name[12];
			memset(sub_name,0,sizeof(sub_name));
			sprintf(sub_name, "%03d.bmp", j);
			string filesub = sub_name;
      		write_bmp(filename + sub_name,
                    out_buff + (j * frame_len),
                    frame_len,
                    nvsghdr->width,
                    0 - nvsghdr->height,
                    depth);
    	}
  	}
  	else
  	{
    	write_bmp(filename + ".bmp",
                  out_buff,
                  out_len,
                  nvsghdr->width,
                  0 - nvsghdr->height,
                  depth);
	}
	delete [] out_buff;
	return true;
}

int main(int argc, char** argv) 
{
	string in_filename = argv[1];
	FILE* fd = fopen(in_filename.c_str(), "rb");
	fseek(fd,0,SEEK_END);
	DWORD FileSize=ftell(fd);
	rewind(fd);
	char *buff=new char [FileSize];
	fread(buff,FileSize,1,fd);
	fclose(fd);
	
	if (!process(in_filename, (BYTE*)buff, FileSize)) 
	{
		printf("Unsupported file!\n");
    }

    delete [] buff;
	return 0;
}


