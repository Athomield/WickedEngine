#pragma once
#include "CommonInclude.h"
#include "wiGraphicsDevice.h"
#include "wiEnums.h"

struct wiImageParams;

namespace wiImage
{
	void Draw(const wiGraphics::Texture2D* texture, const wiImageParams& params, wiGraphics::CommandList cmd);

	void LoadShaders();
	void Initialize();
};

// Do not alter order or value because it is bound to lua manually!
enum STENCILMODE
{
	STENCILMODE_DISABLED,
	STENCILMODE_EQUAL,
	STENCILMODE_LESS,
	STENCILMODE_LESSEQUAL,
	STENCILMODE_GREATER,
	STENCILMODE_GREATEREQUAL,
	STENCILMODE_NOT,
	STENCILMODE_COUNT
};
enum STENCILREFMODE
{
	STENCILREFMODE_ENGINE,
	STENCILREFMODE_USER,
	STENCILREFMODE_ALL,
	STENCILREFMODE_COUNT
};
enum SAMPLEMODE
{
	SAMPLEMODE_CLAMP,
	SAMPLEMODE_WRAP,
	SAMPLEMODE_MIRROR,
	SAMPLEMODE_COUNT
};
enum QUALITY
{
	QUALITY_NEAREST,
	QUALITY_LINEAR,
	QUALITY_ANISOTROPIC,
	QUALITY_BICUBIC,
	QUALITY_COUNT
};
enum ImageType
{
	SCREEN,
	WORLD,
};

struct wiImageParams
{
	enum FLAGS
	{
		EMPTY = 0,
		DRAWRECT = 1 << 0,
		DRAWRECT2 = 1 << 1,
		MIRROR = 1 << 2,
		EXTRACT_NORMALMAP = 1 << 3,
		HDR = 1 << 4,
		FULLSCREEN = 1 << 5,
	};
	uint32_t _flags = EMPTY;

	XMFLOAT3 pos;
	XMFLOAT2 siz;
	XMFLOAT2 scale;
	XMFLOAT4 col;
	XMFLOAT4 drawRect;
	XMFLOAT4 drawRect2;
	XMFLOAT4 lookAt;			//(x,y,z) : direction, (w) :isenabled?
	XMFLOAT2 texOffset;
	XMFLOAT2 texOffset2;
	XMFLOAT2 pivot;				// (0,0) : upperleft, (0.5,0.5) : center, (1,1) : bottomright
	float rotation;
	float mipLevel;
	float fade;
	float opacity;
	XMFLOAT2 corners[4];		// you can deform the image by its corners (0: top left, 1: top right, 2: bottom left, 3: bottom right)

	uint8_t stencilRef;
	STENCILMODE stencilComp;
	STENCILREFMODE stencilRefMode;
	BLENDMODE blendFlag;
	ImageType typeFlag;
	SAMPLEMODE sampleFlag;
	QUALITY quality;

	const wiGraphics::Texture2D* maskMap;
	// Generic texture
	void setMaskMap(const wiGraphics::Texture2D* tex) { maskMap = tex; }

	void init() 
	{
		_flags = EMPTY;
		pos = XMFLOAT3(0, 0, 0);
		siz = XMFLOAT2(1, 1);
		col = XMFLOAT4(1, 1, 1, 1);
		scale = XMFLOAT2(1, 1);
		drawRect = XMFLOAT4(0, 0, 0, 0);
		drawRect2 = XMFLOAT4(0, 0, 0, 0);
		texOffset = XMFLOAT2(0, 0);
		texOffset2 = XMFLOAT2(0, 0);
		lookAt = XMFLOAT4(0, 0, 0, 0);
		pivot = XMFLOAT2(0, 0);
		fade = rotation = 0.0f;
		opacity = 1.0f;
		mipLevel = 0.f;
		stencilRef = 0;
		stencilComp = STENCILMODE_DISABLED;
		stencilRefMode = STENCILREFMODE_ALL;
		blendFlag = BLENDMODE_ALPHA;
		typeFlag = SCREEN;
		sampleFlag = SAMPLEMODE_MIRROR;
		quality = QUALITY_LINEAR;
		maskMap = nullptr;
		corners[0] = XMFLOAT2(0, 0);
		corners[1] = XMFLOAT2(1, 0);
		corners[2] = XMFLOAT2(0, 1);
		corners[3] = XMFLOAT2(1, 1);
	}

	constexpr bool isDrawRectEnabled() const { return _flags & DRAWRECT; }
	constexpr bool isDrawRect2Enabled() const { return _flags & DRAWRECT2; }
	constexpr bool isMirrorEnabled() const { return _flags & MIRROR; }
	constexpr bool isExtractNormalMapEnabled() const { return _flags & EXTRACT_NORMALMAP; }
	constexpr bool isHDREnabled() const { return _flags & HDR; }
	constexpr bool isFullScreenEnabled() const { return _flags & FULLSCREEN; }

	// enables draw rectangle for base texture (cutout texture outside draw rectangle)
	void enableDrawRect(const XMFLOAT4& rect) { _flags |= DRAWRECT; drawRect = rect; }
	// enables draw rectangle for mask texture (cutout texture outside draw rectangle)
	void enableDrawRect2(const XMFLOAT4& rect) { _flags |= DRAWRECT2; drawRect2 = rect; }
	// mirror the image horizontally
	void enableMirror() { _flags |= MIRROR; }
	// enable normal map extraction shader that will perform texcolor * 2 - 1 (preferably onto a signed render target)
	void enableExtractNormalMap() { _flags |= EXTRACT_NORMALMAP; }
	// enable HDR blending to float render target
	void enableHDR() { _flags |= HDR; }
	// enable full screen override. It just draws texture onto the full screen, disabling any other setup except sampler and stencil)
	void enableFullScreen() { _flags |= FULLSCREEN; }

	// disable draw rectangle for base texture (whole texture will be drawn, no cutout)
	void disableDrawRect() { _flags &= ~DRAWRECT; }
	// disable draw rectangle for mask texture (whole texture will be drawn, no cutout)
	void disableDrawRect2() { _flags &= ~DRAWRECT2; }
	// disable mirroring
	void disableMirror() { _flags &= ~MIRROR; }
	// disable normal map extraction shader
	void disableExtractNormalMap() { _flags &= ~EXTRACT_NORMALMAP; }
	// if you want to render onto normalized render target
	void disableHDR() { _flags &= ~HDR; }
	// disable full screen override
	void disableFullScreen() { _flags &= ~FULLSCREEN; }


	wiImageParams() 
	{
		init();
	}
	wiImageParams(float width, float height) 
	{
		init();
		siz = XMFLOAT2(width, height);
	}
	wiImageParams(float posX, float posY, float width, float height, const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1))
	{
		init();
		pos.x = posX;
		pos.y = posY;
		siz = XMFLOAT2(width, height);
		col = color;
	}
};
