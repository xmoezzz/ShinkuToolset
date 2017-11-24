#include "stdafx.h"
#include <cstdio>
#include <string>
#include <map>
#include <vector>
#include <windows.h>
#include <string>
#include <Algorithm>
#include "zlib128\zlib-1.2.8\zlib.h"

#pragma comment(lib,"zlib1.lib")

using namespace std;

typedef unsigned char u8;
typedef DWORD         HashType;

HashType CalHashValue(const char *str);


typedef struct FVPImageHeader
{
	char  Signature1[4]; //hzc1
	DWORD OriLen;//FileSize
	DWORD HeaderLen; //0x20
	char  Signature2[4]; // NVSG
	WORD  unknown1; //0x100
	WORD  ImageType; //32bit, value = 1
	WORD  Width;
	WORD  Height;
	WORD  offset_x;//offset->read
	WORD  offset_y;
	DWORD unknown2;//0
	DWORD EntryCount;//0
	DWORD unknown3;//0
	DWORD unknown4;//0
};


typedef struct FileInfo
{
public:
	FVPImageHeader Header;
	string         name;
	unsigned long  Hash;
	FileInfo() : Hash(0){}
	
	friend bool operator < (const FileInfo& lhs, const FileInfo& rhs)
	{
		return lhs.Hash < rhs.Hash;
	}

	friend bool operator == (const FileInfo& lhs, const FileInfo& rhs)
	{
		return lhs.Hash == rhs.Hash;
	}

	friend bool operator <= (const FileInfo& lhs, const FileInfo& rhs)
	{
		return lhs.Hash <= rhs.Hash;
	}

	friend bool operator > (const FileInfo& lhs, const FileInfo& rhs)
	{
		return lhs.Hash > rhs.Hash;
	}

	friend bool operator >= (const FileInfo& lhs, const FileInfo& rhs)
	{
		return lhs.Hash >= rhs.Hash;
	}

	friend bool operator != (const FileInfo& lhs, const FileInfo& rhs)
	{
		return lhs.Hash != rhs.Hash;
	}
	
}FileInfo;


bool Compare(FileInfo& lhs, FileInfo& rhs)
{
	return lhs.Hash < rhs.Hash;
}


vector<FileInfo> FileInfoPool;


typedef struct ArcInfo
{
	FVPImageHeader rdHeader;
	string         name;
}ArcInfo;

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
	unsigned char  signature[4]; // "hzc1"
	DWORD original_length;
	DWORD header_length;
};

struct NVSGHDR
{
	unsigned char  signature[4]; // "NVSG"
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


bool Process(const char *filename, unsigned char *buff, DWORD len)
{
	if (len < 4 || memcmp(buff, "hzc1", 4))
	{
		return false;
	}

	HZC1HDR    *hzc1hdr   = (HZC1HDR*) buff;
	DWORD data_len  = len - sizeof(*hzc1hdr);
	unsigned char*  data_buff = (unsigned char*) (hzc1hdr + 1);

	if (data_len < 4 || memcmp(data_buff, "NVSG", 4))
	{
    	return false;
	}
  	NVSGHDR *nvsghdr = (NVSGHDR*) data_buff;
  	data_buff += hzc1hdr->header_length;
	FileInfo tempInsert;
	tempInsert.Hash = CalHashValue(filename);
	tempInsert.name = filename;

	tempInsert.Header.unknown1 = nvsghdr->unknown1;
	tempInsert.Header.ImageType = nvsghdr->type;
	tempInsert.Header.Width    = nvsghdr->width;
	tempInsert.Header.Height   = nvsghdr->height;
	tempInsert.Header.offset_x = nvsghdr->offset_x;
	tempInsert.Header.offset_y = nvsghdr->offset_y;
	tempInsert.Header.unknown2 = nvsghdr->unknown2;
	tempInsert.Header.EntryCount = nvsghdr->entry_count;
	tempInsert.Header.unknown3 = nvsghdr->unknown3;
	tempInsert.Header.unknown4 = nvsghdr->unknown4;

	FileInfoPool.push_back(tempInsert);
	return true;
}

string get_file_extension(const std::string& filename)
{
	string temp;
	string::size_type pos = filename.find_last_of("\\");
	if (pos != string::npos)
	{
		temp = filename.substr(pos + 1);
	}
	else
	{
		temp = filename;
	}
	return temp;
}


HashType CalHashValue(const char *str)
{
	if (!str)
	{
		return 0;
	}
	HashType hash = 0;
    for(int i = 0; i < strlen(str); i++)
    {
		hash = str[i] + (hash << 6) + (hash << 16) - hash;
    }
	return hash;
}

FileInfo& QueryFile(string filename, unsigned long Hash, bool& result)
{
	result = false;
	FileInfo HashInfo;
	HashInfo.Hash = Hash;
	auto itBegin = lower_bound(FileInfoPool.begin(), FileInfoPool.end(), HashInfo);
	auto itEnd = upper_bound(FileInfoPool.begin(), FileInfoPool.end(), HashInfo);

	for (auto it = itBegin; it != itEnd; it++)
	{
		if (!strcmp(filename.c_str(), it->name.c_str()))
		{
			result = true;
			return *it;
		}
	}
}

int main(int argc, char **argv)
{
	if(argc != 3)
	{
		printf("FVP Image repacker coded by X'moe\nUsage : <%s> <Img.bmp> <original pack>\n",argv[0]);
		return 0;
	}
	if(argv[1] == NULL)
	{
		printf("Cannot Load Image file\n");
		return 0;
	}

	///read pack
	FILE *pack = fopen(argv[2],"rb");
	if(pack == 0)
	{
		printf("Cannot Load Bin File.\n");
		return 0;
	}
	fseek(pack,0,SEEK_END);
	DWORD pdwFileSize=ftell(pack);
	rewind(pack);
	char *pFile = new char[pdwFileSize];
	fread(pFile,pdwFileSize,1,pack);
	fclose(pack);

	BINHDR hdr;
	memcpy(&hdr,pFile,sizeof(hdr));
	BINENTRY *entry = new BINENTRY[hdr.entry_count];
	memcpy(entry,pFile+sizeof(hdr),sizeof(BINENTRY) * hdr.entry_count);
	
    DWORD filenames_len  = hdr.filenames_length;
    unsigned char *filenames_buff = new unsigned char[filenames_len];
    //read(fd, filenames_buff, filenames_len);
    memcpy(filenames_buff, pFile+sizeof(hdr)+sizeof(BINENTRY) * hdr.entry_count, filenames_len);
    
    for (DWORD i = 0; i < hdr.entry_count; i++)
    {
    	char* filename = (char*) (filenames_buff + entry[i].filename_offset);
	    DWORD len  = entry[i].length;
    	unsigned char*   buff = new unsigned char[len];
    	memcpy(buff, pFile+entry[i].offset, len);
    	if (!Process(filename, buff, len))
		{
        	continue;
    	}

	    delete [] buff;
    }
    delete [] filenames_buff;
  	delete [] entry;
	std::sort(FileInfoPool.begin(), FileInfoPool.end(), Compare);

	/////////
	string FileName_t(argv[1]);
	string FileName = get_file_extension(FileName_t);
	//string FileName(argv[1]);
	char NewFileName[512];
	do
	{
		memset(NewFileName,0,sizeof(NewFileName));
		//memset(FileName,0,sizeof(FileName));
		FILE *bmp = fopen(FileName_t.c_str(),"rb");
		if(bmp == NULL)
		{
			printf("Cannot open file[%s] to repack.\n",FileName);
			continue;
		}
		printf("[FVP] compressing @ [%s]\n",FileName.c_str());
		strncpy(NewFileName,FileName.c_str(),FileName.length()-strlen(".bmp"));
		////
		fseek(bmp,0,SEEK_END);
    	DWORD dwFileSize=ftell(bmp);
    	rewind(bmp);
    	char *pFile = new char[dwFileSize];
   		fread(pFile,dwFileSize,1,bmp);
		fclose(bmp);

		DWORD hashname = CalHashValue(NewFileName);
		if(hashname == 0)
		{
			printf("Failed to Compress Image [%s]\n",NewFileName);
			continue;
		}


		char *Compressed = new char[dwFileSize];
		memset(Compressed,0,sizeof(Compressed));
		char *ToWrite = new char[dwFileSize+0x40];
		memset(ToWrite,0,sizeof(ToWrite));
		DWORD dstLen;

		bool result = false;
		FileInfo itPtr = QueryFile(string(NewFileName), hashname, result);

		if(result)
		{

			BITMAPFILEHEADER bmf;
			BITMAPINFOHEADER bmi;
			memcpy(&bmf, pFile, sizeof(bmf));
			memcpy(&bmi, pFile+sizeof(bmf), sizeof(bmi));
			printf("Debug: Compress Type = %d\n",bmi.biCompression);
			printf("Debug: BitMap   Type = %d\n",bmi.biBitCount);
			printf("Debug: Original Size = %d\n",bmi.biSizeImage);
		
			compress((unsigned char*)Compressed,&dstLen,(unsigned char *)pFile+bmf.bfOffBits,dwFileSize-(sizeof(bmf)+sizeof(bmi)));
			printf("Debug: Cal      Size = %d\n",dwFileSize-(sizeof(bmf)+sizeof(bmi)));
			FVPImageHeader pHeader;
			strncpy(pHeader.Signature1,"hzc1",4);
			pHeader.OriLen = dwFileSize-sizeof(bmf)-sizeof(bmi);
			pHeader.HeaderLen = 0x20;
			strncpy(pHeader.Signature2,"NVSG",4);

			pHeader.unknown1 = itPtr.Header.unknown1;
			pHeader.ImageType = (WORD)itPtr.Header.ImageType;
			pHeader.Width = (WORD)itPtr.Header.Width;
			pHeader.Height = (WORD)itPtr.Header.Height;

			pHeader.offset_x = (WORD)itPtr.Header.offset_x;
			pHeader.offset_y = (WORD)itPtr.Header.offset_y;
			pHeader.unknown2 = 0;
			pHeader.unknown3 = 0;
			pHeader.unknown4 = 0;
			pHeader.EntryCount = 0;
		
			DWORD iPos = 0;
			memcpy(ToWrite,&pHeader,sizeof(pHeader));
			iPos+=sizeof(pHeader);
			memcpy((ToWrite+iPos),Compressed,dstLen);
			iPos+=dstLen;

			FILE *bmp_out = fopen(NewFileName,"wb");
			fwrite(ToWrite,iPos,1,bmp_out);
			fclose(bmp_out);
		}
		else 
		{
			printf("Failed to find[%s]\n", itPtr.name.c_str());
			continue;
		}
	
		delete[] pFile;
		delete[] ToWrite;
		delete[] Compressed;
	}while(false);
	return 0;
} 

