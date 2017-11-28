#include <my.h>

#pragma comment(lib, "MyLibrary_x86_static.lib")

int wmain(int argc , WCHAR **argv)
{
	NTSTATUS   Status;
	NtFileDisk File;
	FILE*      StackFile;
	PBYTE      Buffer;
	ULONG      Size, iPos, Offset;
	BYTE       Length;
	CHAR       FileName[MAX_PATH];

	if(argc != 2)
	{
		PrintConsoleA("Usage: %s *.hcb\n", argv[0]);
		return -1;
	}
	
	Status = File.Open(argv[1]);
	if(NT_FAILED(Status))
		return -1;

	Size = File.GetSize32();
	if (!Size)
	{
		File.Close();
		return -1;
	}

	Buffer = (PBYTE)AllocateMemoryP(Size);
	if (!Buffer)
	{
		File.Close();
		return -1;
	}
	File.Read(Buffer, Size);
	File.Close();
	

	Offset = *(PDWORD)Buffer;
    iPos = 4;
	StackFile = nullptr;
	while(iPos < Offset)
	{
		switch (Buffer[iPos])
		{
		case 0x0E: //push string
			iPos++; Length = Buffer[iPos]; iPos++;
			if (Length <= 1)
				iPos += Length;
			else
				fprintf(StackFile, "[0x%08x]%s\r\n", &Buffer[iPos]);
				iPos += Length;
			break;

		case 0x02: //call
		case 0x06: //jmp
		case 0x07: //jz
		case 0x0A: //push int32
		case 0x0D: //push float
			iPos += 5;
			break;

		case 0x03: //call sys
		case 0x0B: //push int16
		case 0x0F: //push global[int16]
		case 0x11: 
		case 0x15: //global[int61] = stack.pop
		case 0x17:
			iPos += 3;
			break;

		case 0x0C: //push int8
		case 0x10: //push local.stack[int8]
		case 0x12:
		case 0x16: //stack[int8] = stack.pop
		case 0x18:
			iPos += 2; 
			break;

		case 0x01: //init stack
			if (StackFile)
				fclose(StackFile);

			FormatStringA(FileName, "%08x.txt", iPos);
			PrintConsoleA("dumping function [%08x]\n", iPos);
			StackFile = fopen(FileName, "wb");
			iPos += 3;
			break;

		case 0x04: //return void
		case 0x05: //return eax
		case 0x08: //push0
		case 0x09: //push1
		case 0x13: //push top
		case 0x14: //push eax
		case 0x19: //neg
		case 0x1A: //add
		case 0x1B: //sub
		case 0x1C: //mul
		case 0x1D: //div
		case 0x1E: //mod
		case 0x1F: //bit test
		case 0x20: //&&
		case 0x21: //||
		case 0x22: //SetE
		case 0x23: //SetNE
		case 0x24: //SetG
		case 0x25: //SetLE
		case 0x26: //SetL
		case 0x27: //SetGE
			iPos++;
			break;

		default:
			PrintConsoleA("Unknown instruction @ 0x08x\n", iPos);
			if (StackFile)
				fclose(StackFile);
			goto END_OF_PARSE;
		}
	}

END_OF_PARSE:
	PrintConsoleA("Disassemble ok\n");
	return 0;
}


