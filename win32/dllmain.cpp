// dllmain.cpp : Defines the entry point for the DLL application.
#include "../util/stdafx/stdafx.h"
#include <Windows.h>
#include <shellapi.h>
#include "../util/storage/storage.h"

#include "../qcommon/qcommon.h"
#include "../util/memutil/memutil.h"

#pragma comment(lib, "Wldap32.lib")

#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>
#pragma comment(lib, "Shell32.lib")

#if USE_CURL
	#define CURL_STATICLIB
	#include <curl/curl.h>
	#pragma comment(lib, "libcurl.lib")
#endif

#include <string>
#include <sstream>

#include "../util/picosha/picosha2.h"

#if 0
char	* va(const char *format, ...) {
	va_list		argptr;
	static char		string[2][32000];	// in case va is called by nested functions
	static int		index = 0;
	char	*buf;

	buf = string[index & 1];
	index++;

	va_start(argptr, format);
	vsprintf(buf, format, argptr);
	va_end(argptr);

	return buf;
}
#endif

size_t curl_to_string(void *ptr, size_t size, size_t nmemb, void *data)
{
	std::string *str = (std::string *) data;
	char *sptr = (char *)ptr;
	int x;

	for (x = 0; x < size * nmemb; ++x)
	{
		(*str) += sptr[x];
	}

	return size * nmemb;
}
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}

#if USE_CURL

void test2()
{
	CURL *curl;
	FILE *fp;
	CURLcode res;
	const char *url = "https://google.com";
	char outfilename[FILENAME_MAX] = "bbb.txt";
	curl = curl_easy_init();
	if (curl) {
		fp = fopen(outfilename, "wb");
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		res = curl_easy_perform(curl);
		/* always cleanup */
		curl_easy_cleanup(curl);
		fclose(fp);
	}
}

void Sys_CheckUpdates()
{
	if (!Sys_IsPHP())
		return;

	CURL *curl;
	FILE *fp;
	CURLcode res;
	const char *url = "http://xtnded.org/deployment.txt";
	curl = curl_easy_init();
	std::string pagedata;
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_PORT, 28860);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_to_string);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pagedata);
		res = curl_easy_perform(curl);

		if (res != CURLE_OK)
		{
			MessageBoxA(NULL, "Error occured during checking for any available updates.", "Error", MB_OK | MB_ICONWARNING);
		}
		else
		{
			std::istringstream iss(pagedata);
			std::string line;
			while (std::getline(iss, line)) //one file per line
			{
				auto sep = line.find_first_of(':');
				if (sep == std::string::npos)
				{
					MessageBoxA(NULL, "Update file corrupted, failed to parse!", "Error", MB_OK | MB_ICONWARNING);
					break;
				}
				std::string filename = line.substr(0, sep);
				std::string hash = line.substr(sep + 1, line.size());
				std::string trimmed;
				for (auto & ch : hash)
				{
					if (!isalnum(ch))continue; //mmm? really
					trimmed.push_back(ch);
				}
				MessageBoxA(NULL, va("file: %s (hash: %s)", filename.c_str(), trimmed.c_str()), "", 0);

				if (!storage::file_exists(filename))
				{
					MessageBoxA(NULL, "file does not exist!", "", 0); //update / grab this file
				}
				else
				{
					//is it old? hashes are not same?
					std::ifstream f(filename, std::ios::binary);
					std::vector<unsigned char> s(picosha2::k_digest_size);
					picosha2::hash256(f, s.begin(), s.end());

					std::string hex_str = picosha2::bytes_to_hex_string(s.begin(), s.end());
					MessageBoxA(NULL, va("found file %s, hash = %s", filename.c_str(), hex_str.c_str()), "", 0);

					if (trimmed != hex_str)
					{
						MessageBoxA(NULL, "update time!", "", 0);
						//Sys_ReplaceFile(...);
						//Sys_ElevateProgram etc
					}
					else
					{
						//file is good
					}
				}
			}
		}
		//MessageBoxA(0, va("page = %s, error = %d", pagedata.c_str(), res), 0, 0);
		curl_easy_cleanup(curl);
		exit(0);
	}
}
#else
	void Sys_CheckUpdates() {
		Com_Printf("Ignoring Sys_CheckUpdates()\n");
	}
#endif

BOOL (__stdcall *oDllMain)(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
);
static BYTE originalCode[5];
static PBYTE originalEP = 0;
void Main_UnprotectModule(HMODULE hModule);
void Main_DoInit()
{
	// unprotect our entire PE image
	HMODULE hModule;
	//if (SUCCEEDED(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)Main_DoInit, &hModule))) //TODO
	if ((GetModuleHandleExA(0x00000004, reinterpret_cast<LPCSTR>(Main_DoInit), &hModule) != 0) || (hModule != nullptr))
	{
		Main_UnprotectModule(hModule);
	}

	//HideCode_FindDeviceIoControl();

	void patch_opcode_loadlibrary(void);
	//patch_opcode_loadlibrary();


	Sys_CheckUpdates();

	void applyHooks();
	applyHooks();

	// return to the original EP
	memcpy(originalEP, &originalCode, sizeof(originalCode));
	__asm jmp originalEP
}
void Main_SetSafeInit()
{
	// find the entry point for the executable process, set page access, and replace the EP
	HMODULE hModule = GetModuleHandle(NULL); // passing NULL should be safe even with the loader lock being held (according to ReactOS ldr.c)

	if (hModule)
	{
		PIMAGE_DOS_HEADER header = (PIMAGE_DOS_HEADER)hModule;
		PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((DWORD)hModule + header->e_lfanew);

		Main_UnprotectModule(hModule);

		// back up original code
		PBYTE ep = (PBYTE)((DWORD)hModule + ntHeader->OptionalHeader.AddressOfEntryPoint);
		memcpy(originalCode, ep, sizeof(originalCode));

		// patch to call our EP
		int newEP = (int)Main_DoInit - ((int)ep + 5);
		ep[0] = 0xE9; // for some reason this doesn't work properly when run under the debugger
		memcpy(&ep[1], &newEP, 4);

		originalEP = ep;
	}
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{


#if 0
	static bool check = false;
	if (!check)
	{
		std::vector<char> data;
		if (!storage::read_file(szModuleName, data))
		{
			MessageBoxA(NULL, "Failed to check version!", "", MB_OK | MB_ICONWARNING);
			Com_Quit_f();
			return TRUE;
		}
		else
		{
			const char *searchString = "cod2sp_s.exe";
			if (search_memory((int)data.data(), (int)data.data() + data.size(), (BYTE*)searchString, strlen(searchString)) == -1)
			{
				//we're SP mode
				//MessageBoxA(0, "single player detected", "", 0);
			}
			else
			{
				isMP = true;
				//MessageBoxA(0, "mp detected", "", 0);
			}
		}
		//cod2sp_s.exe
		check = true;
	}
#endif

#if 0
	if (strstr(szModuleName, "rundll32") != NULL) {
		return TRUE;
	}
#endif
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		char szModuleName[MAX_PATH + 1];

		GetModuleFileNameA(NULL, szModuleName, MAX_PATH);
		void MSS32_Hook();
		MSS32_Hook();

		extern bool miles32_loaded;
		if (!miles32_loaded)
			return FALSE;
		Main_SetSafeInit();
		//DisableThreadLibraryCalls(hModule);
		break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

