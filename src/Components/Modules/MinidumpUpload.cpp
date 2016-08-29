#include "STDInclude.hpp"
#include "Shlwapi.h"

const int MiniDumpTiny = MiniDumpFilterMemory | MiniDumpWithoutAuxiliaryState | MiniDumpWithoutOptionalData | MiniDumpFilterModulePaths | MiniDumpIgnoreInaccessibleMemory;

namespace Components
{

#pragma region Minidump class implementation
	Minidump::Minidump()
	{
		this->fileHandle = this->mapFileHandle = INVALID_HANDLE_VALUE;
	}

	Minidump::~Minidump()
	{
		if (this->mapFileHandle != NULL && this->mapFileHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(this->mapFileHandle);
			this->mapFileHandle = INVALID_HANDLE_VALUE;
		}

		if (this->fileHandle != NULL && this->fileHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(this->fileHandle);
			this->fileHandle = INVALID_HANDLE_VALUE;
		}
	}

	std::string Minidump::ToString()
	{
		if (!this->EnsureFileMapping()) return false;

		auto pBuf = MapViewOfFile(this->mapFileHandle, FILE_MAP_READ, 0, 0, 0);
		if (pBuf == NULL)
		{
			Utils::OutputDebugLastError();
			throw new std::runtime_error("Could not read minidump.");
		}

		size_t fileSize;
		DWORD fileSizeHi;
		fileSize = GetFileSize(this->fileHandle, &fileSizeHi);
#ifdef _WIN64
		fileSize |= ((size_t)fileSizeHi << 32);
#endif
		std::string retval = std::string((const char*)pBuf, fileSize * sizeof(const char));
		UnmapViewOfFile(pBuf);
		return retval;
	}

	bool Minidump::GetStream(MINIDUMP_STREAM_TYPE type, PMINIDUMP_DIRECTORY* directoryPtr, PVOID* streamBeginningPtr, ULONG* streamSizePtr)
	{
		if (!this->EnsureFileMapping()) return false;

		auto pBuf = MapViewOfFile(this->mapFileHandle, FILE_MAP_READ, 0, 0, 0);
		if (pBuf == NULL)
		{
			Utils::OutputDebugLastError();
			throw new std::runtime_error("Could not read minidump.");
		}

		BOOL success = MiniDumpReadDumpStream(pBuf, type, directoryPtr, streamBeginningPtr, streamSizePtr);
		UnmapViewOfFile(pBuf);
		if (success != TRUE)
			return false;

		return true;
	}

	inline bool Minidump::Check()
	{
		/*PMINIDUMP_DIRECTORY directory;
		PVOID stream;
		ULONG streamSize;
		return Minidump::GetStream(ExceptionStream, &directory, &stream, &streamSize);*/
		return Minidump::GetStream(ExceptionStream, NULL, NULL, NULL);
	}

	Minidump* Minidump::Create(std::string path, LPEXCEPTION_POINTERS exceptionInfo, int type)
	{
		Minidump* minidump = Minidump::Initialize(path);
		if (minidump == NULL) return minidump;

		// Do the dump generation
		MINIDUMP_EXCEPTION_INFORMATION ex = { GetCurrentThreadId(), exceptionInfo, FALSE };
		if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), minidump->fileHandle, (MINIDUMP_TYPE)type, &ex, NULL, NULL))
		{
			Utils::OutputDebugLastError();
			delete minidump;
			return NULL;
		}

		if (SetFilePointer(minidump->fileHandle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			Utils::OutputDebugLastError();
			delete minidump;
			DeleteFileA(path.c_str());
			return NULL;
		}

		return minidump;
	}

	Minidump* Minidump::Open(std::string path)
	{
		Minidump* minidump = Minidump::Initialize(path, FILE_SHARE_READ);
		return minidump;
	}

	bool Minidump::EnsureFileMapping()
	{
		if (this->mapFileHandle == NULL || this->mapFileHandle == INVALID_HANDLE_VALUE)
		{
			this->mapFileHandle = CreateFileMappingA(this->fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
			if (this->mapFileHandle == NULL || this->mapFileHandle == INVALID_HANDLE_VALUE)
			{
				Utils::OutputDebugLastError();
				return false;
			}
		}
		return true;
	}

	Minidump* Minidump::Initialize(std::string path, DWORD fileShare)
	{
		Minidump* minidump = new Minidump();

		minidump->fileHandle = CreateFileA(path.c_str(),
			GENERIC_WRITE | GENERIC_READ, fileShare,
			NULL, (fileShare & FILE_SHARE_WRITE) > 0 ? OPEN_ALWAYS : OPEN_EXISTING, NULL, NULL);
		if (minidump->fileHandle == NULL || minidump->fileHandle == INVALID_HANDLE_VALUE)
		{
			Utils::OutputDebugLastError();
			delete minidump;
			return NULL;
		}

		return minidump;
	}
#pragma endregion

#pragma region Minidump uploader class implementation
	MinidumpUpload* MinidumpUpload::Singleton = NULL;

	const std::string MinidumpUpload::queuedMinidumpsFolder = "minidumps\\";
#ifdef DISABLE_BITMESSAGE
	const std::vector<std::string> MinidumpUpload::targetUrls = {
		"https://reich.io/upload.php",
		"https://hitlers.kz/upload.php"
	};
#else
	const std::string MinidumpUpload::targetAddress = "BM-2cSksR7gyyFcNK7MaFoxGCjRJWxtoGckdj";
	const unsigned int MinidumpUpload::maxSegmentSize = 200 * 1024; // 200 kB
#endif

	MinidumpUpload::MinidumpUpload()
	{
		if (Singleton != NULL)
			throw new std::runtime_error("Can only create one instance at a time.");

		Singleton = this;

#if !defined(DEBUG) || defined(FORCE_MINIDUMP_UPLOAD)
		this->uploadThread = std::thread([&]() { this->UploadQueuedMinidumps(); });
#endif
	}

	MinidumpUpload::~MinidumpUpload()
	{
		Singleton = NULL;

		if (this->uploadThread.joinable())
		{
			this->uploadThread.join();
		}
	}

	bool MinidumpUpload::EnsureQueuedMinidumpsFolderExists()
	{
		BOOL success = CreateDirectoryA(queuedMinidumpsFolder.c_str(), NULL);
		if (success != TRUE) success = GetLastError() == ERROR_ALREADY_EXISTS;
		return success == TRUE;
	}

	Minidump* MinidumpUpload::CreateQueuedMinidump(LPEXCEPTION_POINTERS exceptionInfo, int minidumpType)
	{
		// Note that most of the Path* functions are DEPRECATED and they have been replaced by Cch variants that only work on Windows 8+.
		// If you plan to drop support for Windows 7, please upgrade these calls to prevent accidental buffer overflows!

		if (!EnsureQueuedMinidumpsFolderExists()) return NULL;

		// Current executable name
		char exeFileName[MAX_PATH];
		GetModuleFileNameA(NULL, exeFileName, MAX_PATH);
		PathStripPathA(exeFileName);
		PathRemoveExtensionA(exeFileName);

		// Generate filename
		char filenameFriendlyTime[MAX_PATH];
		__time64_t time;
		tm ltime;
		_time64(&time);
		_localtime64_s(&ltime, &time);
		strftime(filenameFriendlyTime, sizeof(filenameFriendlyTime) - 1, "%Y%m%d%H%M%S", &ltime);

		// Combine with queuedMinidumpsFolder
		char filename[MAX_PATH];
		PathCombineA(filename, queuedMinidumpsFolder.c_str(), Utils::String::VA("%s-" VERSION_STR "-%s.dmp", exeFileName, filenameFriendlyTime));

		// Generate the dump
		return Minidump::Create(filename, exceptionInfo, minidumpType);
	}

	bool MinidumpUpload::UploadQueuedMinidumps()
	{
#ifndef DISABLE_BITMESSAGE
		// Preload public key for our target that will receive minidumps
		if (!BitMessage::Singleton->RequestPublicKey(MinidumpUpload::targetAddress))
		{
			Logger::Error("Failed to request public key for minidump collection address.\n");
		}
#endif

		// Check if folder exists
		if (!PathIsDirectoryA(queuedMinidumpsFolder.c_str()))
		{
			// Nothing to upload
			return PathFileExistsA(queuedMinidumpsFolder.c_str()) == FALSE;
		}

		// Walk through directory and search for valid minidumps
		WIN32_FIND_DATAA ffd;
		HANDLE hFind = FindFirstFileA(Utils::String::VA("%s\\*.dmp", queuedMinidumpsFolder), &ffd);
		if (hFind != INVALID_HANDLE_VALUE)
		{
			do
			{
				if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) > 0)
					continue; // ignore directory

				char fullPath[MAX_PATH_SIZE];
				PathCombineA(fullPath, queuedMinidumpsFolder.c_str(), ffd.cFileName);

				// Try to open this minidump
				auto minidump = Minidump::Open(fullPath);
				if (minidump == NULL)
					continue; // file can't be opened

				// Upload!
				if (!MinidumpUpload::Upload(minidump))
					continue; // couldn't upload that minidump, keep for another attempt

				delete minidump;

#ifndef KEEP_MINIDUMPS_AFTER_UPLOAD
				// Delete minidump if possible
				DeleteFileA(fullPath);
#endif
			} while (FindNextFileA(hFind, &ffd) != 0);
		}

		return true;
	}

	bool MinidumpUpload::Upload(Minidump* minidump)
	{
		// Checking if we can extract any information from minidump first, just to be sure that this is valid.
		if (!minidump->Check())
		{
			Utils::OutputDebugLastError();
			return false;
		}

		std::map<std::string, std::string> extraHeaders = {
			{"Hash-SHA512", Utils::Cryptography::SHA512::Compute(minidump->ToString(), true)},
			{"Compression", "deflate"},
			{"ID", Utils::String::GenerateUUIDString()},
		};

		Logger::Print("Compressing minidump...\n");

		std::string compressedMinidump = Utils::Compression::ZLib::Compress(minidump->ToString());

#ifndef DISABLE_BASE128
		extraHeaders["Encoding"] = "base128";
		compressedMinidump = Utils::String::EncodeBase128(compressedMinidump);
#endif

		Logger::Print("Uploading minidump...\n");

#ifdef DISABLE_BITMESSAGE
		for (auto& targetUrl : targetUrls)
		{
			Utils::WebIO webio("Firefucks", targetUrl);

			std::string buffer = MinidumpUpload::Encode(compressedMinidump, extraHeaders);
			std::string result = webio.PostFile(buffer, "files[]", "minidump.dmpx");

			std::string errors;
			json11::Json object = json11::Json::parse(result, errors);

			if (!object.is_object()) continue;

			json11::Json success = object["success"];

			if (!success.is_bool() || !success.bool_value()) return false;

			json11::Json files = object["files"];

			if (!files.is_array()) continue;

			for (auto file : files.array_items())
			{
				json11::Json url = file["url"];
				json11::Json hash = file["hash"];

				if (hash.is_string() && url.is_string())
				{
					if (Utils::String::ToLower(Utils::Cryptography::SHA1::Compute(buffer, true)) == Utils::String::ToLower(hash.string_value()))
					{
						MessageBoxA(0, url.string_value().data(), 0, 0);
						return true;
					}
				}
			}
		}

		return false;
#else
		// BitMessage has a max msg size that is somewhere around 220 KB, split it up as necessary!
		auto totalParts = compressedMinidump.size() / this->maxSegmentSize + 1;
		extraHeaders.insert({ "Parts", Utils::String::VA("%d",totalParts) });

		for (size_t offset = 0; offset < compressedMinidump.size(); offset += this->maxSegmentSize)
		{
			auto extraPartHeaders = extraHeaders;

			auto part = compressedMinidump.substr(offset, std::min(this->maxSegmentSize, compressedMinidump.size() - offset));
			auto partNum = offset / this->maxSegmentSize + 1;
			extraPartHeaders.insert({ "Part", Utils::String::VA("%d", partNum) });

			Logger::Print("Uploading part %d out of %d (%d bytes)...\n", partNum, totalParts, part.size());
			BitMessage::Singleton->SendMsg(MinidumpUpload::targetAddress, MinidumpUpload::Encode(part, extraPartHeaders));
		}

		return true;
#endif
	}

	std::string MinidumpUpload::Encode(std::string data, std::map<std::string, std::string> extraHeaders)
	{
		std::string marker = "MINIDUMP";
		std::stringstream output;

		if (extraHeaders.find("Encoding") == extraHeaders.end())
			extraHeaders["Encoding"] = "raw";
		extraHeaders["Version"] = VERSION_STR;

		output << "-----BEGIN " << marker << "-----\n";

		// Insert extra headers
		for (auto& header : extraHeaders)
		{
			output << header.first << ": " << header.second << "\n";
		}

		output << "\n"
			<< data << "\n"
			<< "-----END " << marker << "-----\n";

		return output.str();
	}
#pragma endregion
}