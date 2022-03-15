#include "STDInclude.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x) assert(x)
#include <stb_image.h>

namespace Components
{
	bool Image_LoadFontTextureFromPgm(Game::GfxImage* image)
	{
		auto pgmName = Utils::String::VA("images/%s.pgm", image->name);

		if (!Game::FS_FileExists(pgmName))
			return false;

		auto pgmFile = FileSystem::File(pgmName);

		// Build a header
		Game::GfxImageFileHeader header =
		{
			{ 'I', 'W', 'i' },
			/* version */
			8,
			/* flags */
			2,
			/* format */
			Game::IMG_FORMAT_BITMAP_RGBA,
			0,
			/* dimensions(x, y, z) */
			{ 0, 0, 1 },
			/* fileSizeForPicmip (mipSize in bytes + sizeof(GfxImageFileHeader)) */
			{ 0, 0, 0, 0 }
		};

		int w, h;

		auto* buffer = pgmFile.getBuffer().data();

		if (sscanf_s(buffer, "P5\n%d\n%d\n255\n", &w, &h) != 2)
		{
			Logger::SoftError("Image_LoadFontTextureFromPgm: Pgm %s is broken!\n", pgmFile.getName().data());
			return false;
		}

		int newLineCounter = 0;
		
		while (newLineCounter < 4)
			newLineCounter += *(buffer++) == '\n' ? 1 : 0;

		auto* pixels = Utils::Memory::AllocateArray<uint8_t>(w * h * 4);

		// Our font texture contains alpha channel only
		for (int i = 0; i < w * h * 4; i += 4)
		{
			pixels[i + 0] = 255;
			pixels[i + 1] = 255;
			pixels[i + 2] = 255;
			pixels[i + 3] = buffer[i / 4];
		}

		header.dimensions[0] = static_cast<short>(w);
		header.dimensions[1] = static_cast<short>(h);

		auto fileSize = w * h * 4;

		for (int i = 0; i < 4; ++i)
			header.fileSizeForPicmip[i] = fileSize;

		header.fileSizeForPicmip[0] += sizeof(Game::GfxImageFileHeader);

		image->noPicmip = 0;
		image->width = header.dimensions[0];
		image->height = header.dimensions[1];
		image->depth = 1;

		Game::Image_PicmipForSemantic(image->semantic, &image->picmip);
		Game::Image_LoadFromData(image, &header, pixels);

		Utils::Memory::Free(pixels);

		return true;
	}

	bool Image_LoadFromPng(Game::GfxImage* image)
	{
		auto pngName = Utils::String::VA("images/%s.png", image->name);

		if (!Game::FS_FileExists(pngName))
			return false;

		auto pngFile = FileSystem::File(pngName);

		// Build a header
		Game::GfxImageFileHeader header =
		{
			{ 'I', 'W', 'i' },
			/* version */
			8,
			/* flags */
			2,
			/* format */
			Game::IMG_FORMAT_BITMAP_RGBA,
			0,
			/* dimensions(x, y, z) */
			{ 0, 0, 1 },
			/* fileSizeForPicmip (mipSize in bytes + sizeof(GfxImageFileHeader)) */
			{ 0, 0, 0, 0 }
		};

		auto buf = pngFile.getBuffer();

		int x, y, comp;

		auto* pngImage = stbi_load_from_memory(reinterpret_cast<const uint8_t*>(buf.data()), buf.size(), &x, &y, &comp, STBI_rgb_alpha);

		if (!pngImage)
		{
			Logger::SoftError("Image_LoadFromPng: Png %s is broken!\n", pngFile.getName().data());
			return false;
		}

		// now convert BGRA to RGBA
		for (int i = 0; i < x * y * 4; i += 4)
			std::swap(pngImage[i], pngImage[i + 2]);

		header.dimensions[0] = static_cast<short>(x);
		header.dimensions[1] = static_cast<short>(y);

		auto fileSize = x * y * comp;

		for (int i = 0; i < 4; ++i)
			header.fileSizeForPicmip[i] = fileSize;

		header.fileSizeForPicmip[0] += sizeof(Game::GfxImageFileHeader);

		image->noPicmip = 0;
		image->width = static_cast<short>(x);
		image->height = static_cast<short>(y);
		image->depth = 1;

		Game::Image_PicmipForSemantic(image->semantic, &image->picmip);
		Game::Image_LoadFromData(image, &header, pngImage);

		stbi_image_free(pngImage);

		return true;
	}

	bool Image_LoadFromFileWithReader_stub(Game::GfxImage* image, Game::Reader_t reader)
	{
		if (Utils::String::StartsWith(image->name, "if_") && Image_LoadFontTextureFromPgm(image))
			return true;

		if (Image_LoadFromPng(image))
			return true;

		return Game::Image_LoadFromFileWithReader(image, reader);
	}

	Images::Images()
	{
		Utils::Hook(0x51F486, Image_LoadFromFileWithReader_stub, HOOK_CALL).install()->quick();
		Utils::Hook(0x51F595, Image_LoadFromFileWithReader_stub, HOOK_CALL).install()->quick();
		Utils::Hook(0x51F809, Image_LoadFromFileWithReader_stub, HOOK_CALL).install()->quick();
		Utils::Hook(0x51F896, Image_LoadFromFileWithReader_stub, HOOK_CALL).install()->quick();
	}
}
