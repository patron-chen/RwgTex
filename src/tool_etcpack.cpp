////////////////////////////////////////////////////////////////
//
// RwgTex / Ericsson's ETCPack support
// (c) Pavel [VorteX] Timofeyev
// See LICENSE text file for a license agreement
//
////////////////////////////////

#include "main.h"
#include "tex.h"
#include "etcpack/etcpack_lib.h"

TexTool TOOL_ETCPACK =
{
	"ETCPack", "Ericsson ETCPack", "etcpack",
	"ETC1",
	TEXINPUT_RGB | TEXINPUT_RGBA,
	&ETCPack_Init,
	&ETCPack_Option,
	&ETCPack_Load,
	&ETCPack_Compress,
	&ETCPack_Version,
};

// tool options
int        etcpack_speed[NUM_PROFILES];
int        etcpack_colormode;
OptionList etcpack_speedOption[] = 
{ 
	{ "normal", 0 }, 
	{ "exhaustive", 1 }, 
	{ 0 }
};

/*
==========================================================================================

  Init

==========================================================================================
*/

void ETCPack_Init(void)
{
	// ETC1-based
	RegisterFormat(&F_ETC1, &TOOL_ETCPACK);
	// ETC2-based
	RegisterFormat(&F_ETC2, &TOOL_ETCPACK);
	RegisterFormat(&F_ETC2A, &TOOL_ETCPACK);
	RegisterFormat(&F_ETC2A1, &TOOL_ETCPACK);
	RegisterFormat(&F_EAC1, &TOOL_ETCPACK);
	RegisterFormat(&F_EAC2, &TOOL_ETCPACK);
	// options
	etcpack_speed[PROFILE_FAST] = 0;
	etcpack_speed[PROFILE_REGULAR] = 1;
	etcpack_speed[PROFILE_BEST] = 1;
	// ETCPack startup
	readCompressParams();
}

void ETCPack_Option(const char *group, const char *key, const char *val, const char *filename, int linenum)
{
	if (!stricmp(group, "profiles"))
	{
		if (!stricmp(key, "fast"))
			etcpack_speed[PROFILE_FAST] = OptionEnum(val, etcpack_speedOption, etcpack_speed[PROFILE_REGULAR], TOOL_ETCPACK.name);
		else if (!stricmp(key, "regular"))
			etcpack_speed[PROFILE_REGULAR] = OptionEnum(val, etcpack_speedOption, etcpack_speed[PROFILE_REGULAR], TOOL_ETCPACK.name);
		else if (!stricmp(key, "best"))
			etcpack_speed[PROFILE_BEST] = OptionEnum(val, etcpack_speedOption, etcpack_speed[PROFILE_REGULAR], TOOL_ETCPACK.name);
		else
			Warning("%s:%i: unknown key '%s'", filename, linenum, key);
		return;
	}
	if (!stricmp(group, "options"))
	{
		Warning("%s:%i: unknown key '%s'", filename, linenum, key);
		return;
	}
	Warning("%s:%i: unknown group '%s'", filename, linenum, group);
}

void ETCPack_Load(void)
{
}


const char *ETCPack_Version(void)
{
	return ETCPACK_VERSION_S;
}

/*
==========================================================================================

  Compression

==========================================================================================
*/

void ETCPack_PrescaleImage(TexEncodeTask *t, byte **imagedata, int *w, int *h, byte **decoded, bool *resized, int bpp)
{
	int sw = *w;
	int sh = *h;
	int dw = (int)(ceil((float)(sw)/4.0f)*4);
	int dh = (int)(ceil((float)(sh)/4.0f)*4);
	
	*imagedata = Image_ConvertBPP(t->image, bpp);
	*decoded = (byte *)mem_alloc(dw * dh * bpp);
	// resize to match div/4
	if (dw != sw || dh != sh)
	{
		byte *tempimg = (byte *)mem_alloc(dw * dh * bpp);
		for (int y = 0; y < sh; y++)
		{
			memcpy(tempimg + dw*bpp*y, imagedata + sw*bpp*y, sw*bpp);
			for(int x = sw; x < dw; x++)
				memcpy(tempimg + (dw*y + sw + x), imagedata + (sw*y - 1)*bpp, bpp);
		}
		for(int y = sh; y < dh; y++)
			memcpy(tempimg + y*dw*bpp, tempimg + sh*dw*bpp, dw*bpp);
		*imagedata = tempimg;
		*resized = true;
		*w = dw;
		*h = dh;
		return;
	}
	// no prescale
	*resized = false;
	*w = dw;
	*h = dh;
}

void ETCPack_Free(byte *imagedata, byte *decoded, bool resized)
{
	if (resized)
		mem_free(imagedata);
	mem_free(decoded);
}

void ETCPack_WriteColorBlock(byte **stream, unsigned int block1, unsigned int block2)
{
	byte *data;

	data = *stream;
	data[0] = (block1 >> 24) & 0xFF;
	data[1] = (block1 >> 16) & 0xFF;
	data[2] = (block1 >> 8) & 0xFF;
	data[3] =  block1 & 0xFF;
	data[4] = (block2 >> 24) & 0xFF;
	data[5] = (block2 >> 16) & 0xFF;
	data[6] = (block2 >> 8) & 0xFF;
	data[7] =  block2 & 0xFF;
	*stream = data + 8;
}

// compress ETC1 RGB block
void ETCPack_CompressBlockETC1(byte **stream, byte *imagedata, byte *decoded, int imagewidth, int imageheight, int ofsx, int ofsy)
{
	unsigned int block1, block2;
	if (etcpack_speed[tex_profile])
		compressBlockETC1ExhaustivePerceptual(imagedata, decoded, imagewidth, imageheight, ofsx, ofsy, block1, block2);
	else
		compressBlockDiffFlipFastPerceptual(imagedata, decoded, imagewidth, imageheight, ofsx, ofsy, block1, block2);
	ETCPack_WriteColorBlock(stream, block1, block2);
}

// compress ETC2 RGB block
void ETCPack_CompressBlockETC2(byte **stream, byte *imagedata, byte *decoded, int imagewidth, int imageheight, int ofsx, int ofsy)
{
	unsigned int block1, block2;
	if (etcpack_speed[tex_profile])
		compressBlockETC2ExhaustivePerceptual(imagedata, decoded, imagewidth, imageheight, ofsx, ofsy, block1, block2);
	else
		compressBlockETC2FastPerceptual(imagedata, decoded, imagewidth, imageheight, ofsx, ofsy, block1, block2);
	ETCPack_WriteColorBlock(stream, block1, block2);
}

// compress ETC2 RGBA block
void ETCPack_CompressBlockETC2A(byte **stream, byte *imagedata, byte *decoded, int imagewidth, int imageheight, int ofsx, int ofsy)
{
	unsigned int block1, block2;
	//byte alphadata[8], alphasrcblock[16];
	// compress color block
	if (etcpack_speed[tex_profile])
		compressBlockETC2ExhaustivePerceptual(imagedata, decoded, imagewidth, imageheight, ofsx, ofsy, block1, block2);
	else
		compressBlockETC2FastPerceptual(imagedata, decoded, imagewidth, imageheight, ofsx, ofsy, block1, block2);
	ETCPack_WriteColorBlock(stream, block1, block2);
	// compress alpha block
	//CodecETC2_ExtractBlockAlpha(imagedata, ofsx, ofsy, 4, 4, alphasrcblock);
	//if (etcpack_speed[tex_profile])
	//	compressBlockAlphaSlow(alphaimg, 0, 0, 4, 4, alphadata);
	//else
	//	compressBlockAlphaFast(alphaimg, 0, 0, 4, 4, alphadata);
	//memcpy(stream, alphadata, 8);
	//stream += 8;
}

size_t ETCPack_CompressSingleImage(byte *stream, TexEncodeTask *t, int imagewidth, int imageheight, byte *imagedata, void (*compressBlockFunction)(byte **stream, byte *imagedata, byte *decoded, int imagewidth, int imageheight, int blockx, int blocky))
{
	bool resized = false;
	size_t out = 0;
	byte *decoded;

	ETCPack_PrescaleImage(t, &imagedata, &imagewidth, &imageheight, &decoded, &resized, 3);
	for (int y = 0; y < imageheight / 4; y++)
		for (int x = 0; x < imagewidth / 4; x++)
			compressBlockFunction(&stream, imagedata, decoded, imagewidth, imageheight, x*4, y*4);
	ETCPack_Free(imagedata, decoded, resized);

	return imagewidth*imageheight/2;
}

bool ETCPack_Compress(TexEncodeTask *t)
{
	void (*compressBlockFunction)(byte **stream, byte *imagedata, byte *decoded, int imagewidth, int imageheight, int ofsx, int ofsy);
	size_t output_size;

	// options
	if (t->format->block == &B_ETC1)
		compressBlockFunction = ETCPack_CompressBlockETC1;
	else if (t->format->block == &B_ETC2)
		compressBlockFunction = ETCPack_CompressBlockETC2;
	else
	{
		Warning("ETCPack: %s%s.dds - unsupported compression %s/%s", t->file->path.c_str(), t->file->name.c_str(), t->format->name, t->format->block->name);
		return 0;
	}

	// compress
	byte *stream = t->stream;
	output_size = ETCPack_CompressSingleImage(stream, t, t->image->width, t->image->height, Image_GetData(t->image, NULL), compressBlockFunction);
	if (output_size)
	{
		// compress mipmaps
		stream += output_size;
		for (MipMap *mipmap = t->image->mipMaps; mipmap; mipmap = mipmap->nextmip)
		{
			output_size = ETCPack_CompressSingleImage(stream, t, mipmap->width, mipmap->height, mipmap->data, compressBlockFunction);
			if (output_size)
				stream += output_size;
		}
	}
	return true;
}