#include "bsnes-mt/scaling.h" // MT.

namespace bmi = bsnesMt::scaling; // MT.

extern uint16_t SnowData[800];
extern  uint8_t SnowVelDist[800];

/* MT. */
struct ScalingData {
	uint width, height, scaledWidth, scaledHeight, areaWidth, areaHeight;
	string output;
};

auto isSameScalingData(const ScalingData &a, const ScalingData &b) -> bool {
    return a.width        == b.width
        && a.height       == b.height
        && a.scaledWidth  == b.scaledWidth
        && a.scaledHeight == b.scaledHeight
        && a.areaWidth    == b.areaWidth
        && a.areaHeight   == b.areaHeight
        && a.output       == b.output;
}

static ScalingData prevScalingData;
/* /MT. */

/* MT. */
auto Program::showScalingInfo(
	uint width, uint height, uint scaledWidth, uint scaledHeight,
	uint areaWidth, uint areaHeight, string outputSetting
) -> void
{
	ScalingData scalingData = {width, height, scaledWidth, scaledHeight, areaWidth, areaHeight, outputSetting};

	if (isSameScalingData(prevScalingData, scalingData)) {
		return;
	}

	showMessage({
		width, "×", height, " → ", scaledWidth, "×", scaledHeight,
		" <", areaWidth, "×", areaHeight, "> [", outputSetting , "]"
	});

	prevScalingData = scalingData;
}
/* /MT. */

auto Program::viewportSize(uint& scaledWidth, uint& scaledHeight, uint width, uint height) -> void {
	auto [areaWidth, areaHeight] = video.size();

	string outputSetting = settings.video.output;

	if (outputSetting == "Stretch") {
		scaledWidth  = areaWidth;
		scaledHeight = areaHeight;

		if (settings.video.scalingInfo) {
			showScalingInfo(width, height, scaledWidth, scaledHeight, areaWidth, areaHeight, outputSetting);
		}

		return;
	}

	bool aspectCorrection = settings.video.aspectCorrection,
	     showOverscan     = settings.video.overscan,
	     parInsteadOfAr   = settings.video.parInsteadOfAr;

	if (outputSetting == "Pixel-Perfect") {
		auto scaledSize = bmi::calculateScaledSizePerfect(
			areaWidth, areaHeight,
			width, height,
			aspectCorrection, showOverscan,
			parInsteadOfAr
		);

		scaledWidth  = scaledSize.width;
		scaledHeight = scaledSize.height;
	}
	else if (outputSetting == "Center") {
		auto scaledSize = bmi::calculateScaledSizeCenter(
			areaWidth, areaHeight,
			width, height,
			aspectCorrection, showOverscan,
			parInsteadOfAr
		);

		scaledWidth  = scaledSize.width;
		scaledHeight = scaledSize.height;
	}
	else if (outputSetting == "Scale") {
		auto scaledSize = bmi::calculateScaledSizeScale(
			areaWidth, areaHeight,
			aspectCorrection, showOverscan,
			parInsteadOfAr
		);

		scaledWidth  = scaledSize.width;
		scaledHeight = scaledSize.height;
	}
	else {
		scaledWidth  = bmi::getWidth(aspectCorrection, width);
		scaledHeight = bmi::getHeight(showOverscan, height);
	}

	if (settings.video.scalingInfo) {
		showScalingInfo(width, height, scaledWidth, scaledHeight, areaWidth, areaHeight, outputSetting);
	}
}

auto Program::viewportRefresh() -> void {
	if (!emulator->loaded() && !settings.video.snow) {
		return;
	}

	static uint32_t SnowMover = 0;
	static uint32_t SnowTimer = 18;
	static uint32_t NumSnow   = 0;

	if (settings.video.snow) {
		SnowMover++;
	}

	static const uint16 nullData[256 * 240] = {};
	auto data   = nullData;
	uint pitch  = 512;
	uint width  = 256;
	uint height = 240;
	uint scale  = 1;

	if (emulator->loaded() && screenshot.data) {
		data   = screenshot.data;
		pitch  = screenshot.pitch;
		width  = screenshot.width;
		height = screenshot.height;
		scale  = screenshot.scale;
	}

	if (!settings.video.overscan) {
		uint multiplier = height / 240;
		data   += 8 * multiplier * (pitch >> 1);
		height -= 16 * multiplier;
	}

	uint outputWidth, outputHeight;

	viewportSize(outputWidth, outputHeight, width / scale, height / scale);

	uint filterWidth  = width,
	     filterHeight = height;

	auto filterRender = filterSelect(filterWidth, filterHeight, scale);

	if (auto [output, length] = video.acquire(filterWidth, filterHeight); output) {
		bool dimmed = settings.video.dimming && !presentation.frameAdvance.checked();
		filterRender(dimmed ? paletteDimmed : palette, output, length, (const uint16_t*)data, pitch, width, height);
		length >>= 2;

		if (settings.video.snow) {
			uint32_t i = 0;
			float snowX = filterWidth  / 256.0;
			float snowY = filterHeight / 256.0;

			do {
				uint x = uint8_t(SnowData[i * 2 + 0] >> 8) * snowX;
				uint y = uint8_t(SnowData[i * 2 + 1] >> 8) * snowY;

				if ((SnowVelDist[i * 2] & 8) != 0 && y) {
					uint32_t pixel = output[y * length + x];
					float   a = SnowVelDist[i * 2] / 255.0;
					uint8_t r = (pixel >> 16 & 0xff) * a + 255 * (1.0 - a);
					uint8_t g = (pixel >>  8 & 0xff) * a + 255 * (1.0 - a);
					uint8_t b = (pixel >>  0 & 0xff) * a + 255 * (1.0 - a);
					output[y * length + x] = 255u << 24 | r << 16 | g << 8 | b << 0;
				}
			} while (++i != 200);

			for (; SnowMover != 0; --SnowMover) {
				if (--SnowTimer == 0) {
					if (NumSnow < 400) {
						++NumSnow;
					}

					SnowTimer = 18;
				}

				uint32_t i = 0;
				uint32_t n = NumSnow;

				while (n-- != 0) {
					SnowData[i * 2 + 0] += SnowVelDist[i * 2 + 0] + 4 * (uint8_t)(100 - 50);
					SnowData[i * 2 + 1] += SnowVelDist[i * 2 + 1] + 256;

					if (SnowData[i * 2 + 1] <= 0x200) {
						SnowVelDist[i * 2] |= 8;
					}

					++i;
				}
			}
		}

		video.release();
		video.output(outputWidth, outputHeight);
	}
}

uint16_t SnowData[800] = {
	161, 251, 115, 211, 249,  87, 128, 101, 232, 176,  51, 180, 108, 193, 224, 112, 254, 159, 102, 238,
	223, 123, 218,  42, 173, 160, 143, 170,  64,   1, 174,  29,  34, 187, 194, 199,  40,  89, 232,  32,
	  7, 195, 141,  67, 216,  48, 234,   1, 243, 116, 164, 182, 146, 136,  66,  70,  36,  43,  98, 208,
	 63, 240, 216, 253, 147,  36,  33, 253,  98,  80, 228, 156,  73,  82,  85,   1,  97,  72, 187, 239,
	 18, 196, 127, 182,  22,  22, 101,  25, 124, 145, 240, 213, 186,  22,   7, 161,  30,  98,  90, 197,
	 22, 205,  32, 150,  59, 133,  49, 140,  10, 128, 142, 185, 176, 142, 220, 195, 100, 102, 105, 194,
	 43, 139, 184, 153,   1,  95, 176, 169, 192, 201, 233, 243,  73,  65, 188,  14, 194,  39, 251, 140,
	239, 181, 142, 160, 242, 248,  82,  49,   9, 157, 233, 162, 254, 121, 112,   6, 118,  24,  56, 121,
	 74, 209,   1, 223, 145,   6,  75,  73,  18, 168, 194, 168,  58,  39, 222, 170, 214,  75,  45, 218,
	 39, 197, 242,  98,  22,  90, 255,   5, 144, 244, 252,  55,  98,  18, 135, 101,  27,  85, 215, 207,
	183,  28, 201, 142,  45, 122, 145, 159,  41, 243, 109,  29, 117, 203,   7, 234, 231, 214, 131, 133,
	217,   8,  74, 207, 130,  77,  21, 229, 167,  78, 218, 109, 142,  58, 134, 238,  29, 182, 178,  14,
	144, 129, 196, 219,  60, 128,  30, 105,  57,  53,  76, 122, 242, 208, 101, 241, 246,  99, 248,  67,
	137, 244,  70,  51, 202,  94, 164, 125, 115,  72,  61,  72, 129, 169, 155, 122,  91, 154, 160,  83,
	 41, 102, 223, 218, 140,  40, 132,  16, 223,  92,  50, 230, 168,  47, 126, 117, 242, 136,   1, 245,
	171,   0,  36,  98,  73,  69,  14, 229,  66, 177, 108,  92,  39, 250, 243, 161, 111,  85, 211,  99,
	 52,  98, 121, 188, 128, 201,  90, 205, 223,  92, 177,  19,  87,  18,  75,  54,   6,  81, 235, 137,
	247,  66, 211, 129, 247,  39, 119, 206, 116, 250, 113, 231, 190, 196,  53,  51,  34, 114,  39,  22,
	192,  33, 249, 151,  26,  22, 139,  97, 171, 238, 182,  88,  22, 176, 157, 255, 178, 199, 138,  98,
	140,  36, 112,  90,  25, 245, 134,  64,  48, 190, 165, 113,  24, 195,  84,  70, 175,   9, 179,  69,
	 13,  26, 167, 237, 163, 159, 185, 128, 109, 114,  86,  74, 188, 103, 141,  48, 188, 203, 205, 191,
	215, 193, 224,   4, 153,  36, 108,   3, 172, 235,  56, 251, 211, 115, 173, 216, 240,  33,  78, 150,
	133,  64,  51, 103,  56,  26, 165, 222,  70, 148, 115, 119, 246, 229, 181,  63, 109,  49, 228, 108,
	126,  10, 170,  48,  87,  42, 193,  24,  28, 255, 176, 176, 209, 181,  97,  93,  61, 241, 201, 137,
	129,  97,  24, 159, 168, 215,  61, 113, 104, 143, 168,   7, 196, 216, 149, 239, 110,  65,  75, 143,
	238,   0,  37,  19,   8,  56,  65, 234, 228,  72,  42,   5, 226,  95, 243,  51,  55, 231, 114,  90,
	160, 141, 171, 108, 218, 252, 154,  64, 175, 142, 214, 211, 180, 129, 217, 118,  33, 130, 213,   2,
	 73, 145,  93,  21, 162, 141,  97, 225, 112, 253,  49,  43, 113, 208, 131, 104,  31,  51, 192,  37,
	117, 186,  16,  45,  61, 114, 220,   6,  89, 163, 197, 203, 142,  80,  89, 115, 190, 190, 228,  15,
	166, 145,  59, 139, 120,  79, 104, 252, 246,  73, 113, 144, 224,  65, 204, 155, 221,  85,  31,  99,
	 48, 253,  94, 159, 215,  31, 123, 204, 248, 153,  31, 210, 174, 178,  54, 146, 152,  88,  56,  92,
	197,  35, 124, 104, 211, 118,   1, 207, 108,  68, 123, 161, 107,  69, 143,  13,  79, 170, 130, 193,
	214, 153, 219, 247, 227,   2, 170, 208, 248, 139, 118, 241, 247, 183,  18, 135, 246, 126, 201,  46,
	 70, 234, 171,  72,  18, 135, 236, 216,  32, 178, 148, 231, 161,  15,   6, 254,  34, 181,   5,  71,
	  2, 219,  71,  87, 252,  16, 202, 190, 180,  83,  99, 209,  75, 134,  78,  84, 114,  32, 171, 246,
	125,  11,  57, 200, 102,  29, 176,  26, 205, 151, 152, 108, 100, 146, 117,  95,  71,  77, 158, 207,
	 60, 192,  50, 135, 223, 237, 231,  53,  27, 195, 170, 146, 155, 160,  92, 224, 247, 187,  14,  50,
	203,   5, 153,  42,  17,  75, 109,  14,  78, 160, 236, 114, 131, 105, 189, 209, 233, 135, 221, 207,
	226, 119, 104,  10, 178, 107,  77, 160, 233, 179, 120, 227, 133, 241,  32, 223,  63, 247,  66, 157,
	140,  81, 118,  81,  63, 193, 173, 228, 214,  78, 124, 123, 222, 149,   9, 242,   0, 128, 194, 110
};

uint8_t SnowVelDist[800] = {
	 57,  92, 100,  19, 100, 184, 238, 225,  55, 240, 255, 221, 215, 105, 226, 153, 164,  41,  22,  93,
	176, 203, 155, 199, 244,  52, 233, 219, 110, 227, 229, 227, 152, 240,  83, 248, 226,  31, 163,  22,
	 28, 156,  18,  10, 248,  67, 123, 167,  25, 138,  90,  10,  79, 107, 208, 229, 248, 233, 185,  10,
	167,  21,  19, 178, 132, 154,  81,  70,  20,  71,  95, 147,  72,  27,  91, 189,  13, 189, 102,  84,
	195, 123, 251,  93,  68,  36, 178,  59, 107,  99, 104, 191,  76, 110,  44, 206, 123,  46,  98, 112,
	 26,  50,   1,  35, 150,  17, 242, 208,  69,  23, 202, 197,  59,  80, 136, 124,  40,  89,  11,  40,
	  1, 136,  90,  72, 198,  83,   2, 174, 174,   4,  28, 205, 135,  35, 194,  54,  22,  40,   4, 132,
	191,  88, 163,  66, 204, 230,  35, 111,   9, 177, 254, 174, 163,  68,   5,  88, 111, 235,  58, 236,
	  4, 248, 172, 154, 101, 164,  43, 223,  10,  13, 210, 125, 146,  73, 192,  57, 117, 152, 128,  36,
	106,  21, 253, 113, 110, 133, 244,   4, 150,  32,  76,  71,  22, 106, 210, 244,  46, 128,  27, 215,
	231, 112, 177, 196, 198, 120, 196,  57, 234,  74, 235, 108,  64, 181, 209, 188, 177,  63, 197, 200,
	126, 164, 136, 163,  48,  62, 225, 223, 212, 201, 195, 121,  90,   7,  10, 196,  88,  53,  39, 249,
	147,  98,  65, 253, 246,   3, 152, 125, 242, 105,  44, 129,  94, 232,  13,   4,  86, 220, 194,  67,
	186, 210, 171, 197,  64, 138,  89,  78,  58, 150,  52,  79, 138, 201, 244, 111, 106, 181, 192,  69,
	234, 253, 239, 113,  98,  37, 209, 151,  60,  47, 241, 235, 185,  52, 173,  94, 172, 182,  47, 150,
	 80, 118,  10,  58, 161, 237,  10,  64, 238, 198,  14,  74, 132, 250, 234,  63, 169,  86, 158, 170,
	 76, 168, 124, 133,  28, 203, 246, 140, 228,  77,  50,  53, 115, 113, 157, 218,  90, 192,  28, 209,
	 72, 117, 156, 101, 226,  99,  11, 245,  69,  59,  17, 175, 164,  59,   8, 166, 163, 185,  10,  60,
	100,  19,  26,  38, 114, 232, 180, 115, 238, 184,  88, 103, 178,  67, 212,  21,  87,  64,  85,   1,
	 62,  87, 155,  62,  21,  96, 205, 195, 131,  97, 191, 252, 218, 209, 179, 201,  12,   2, 234, 110,
	162,  14, 145, 170, 156, 105,  85, 132, 132,  60, 239,  14,  80, 129, 225, 144, 149, 244, 188,   8,
	 13, 168, 181, 168,  30, 142,  24, 110,  26, 172, 231, 182,  50, 214,  66, 193, 100,  45, 132, 144,
	205, 190,  16, 133,  45, 250,  83, 183, 140, 229, 117, 226,  68,  59, 163,  96, 235, 227,  25, 155,
	209, 105,  41, 214,  30, 107,   2,  85, 180,  23, 241,  39, 113,  63,  75,  44, 107, 142,  93,  29,
	 62, 240, 235, 152, 147,  52,  54, 146, 109, 112, 139, 162, 238, 198, 201,   8, 141, 115, 112, 106,
	  4,  99,  25, 155, 111, 161, 114, 253,  75, 100,  28,  59, 101, 150,   2, 122, 228,   6,  12,  59,
	249, 181,  67, 136, 227, 227, 199,  46,  75, 203,  50,  25,  50,  61,  62,  22, 238, 124, 218, 134,
	243,  21, 243, 222,  94, 138, 161, 234, 133,  23, 138,  45,   4, 226, 154, 227,   8,  84, 105, 126,
	200, 127, 240, 144, 124, 197, 102, 144,  53,  29,  94, 231, 108, 175, 136,  37,  44, 183, 178,  95,
	 41, 196, 214,  12,  42, 221, 106, 225, 151,  32,  53, 130,  24, 211,  88,  14, 135,  18,  90, 219,
	177, 129,  90, 217, 162, 181, 199, 133, 116,  56,  36, 100, 230,  91, 220,  83,  41,  65,  20,  64,
	177, 197, 249,  24, 242,  62,  26, 234,  92,  44, 167, 153, 243,  94, 179, 163, 103,  29, 220, 199,
	128,  94, 236, 152,  53,  32,  77,  78, 228,  89, 124,  85,  87,  50, 197, 116, 179, 105, 236, 139,
	102,  17, 159,  66, 176,  27, 205,  36, 113,  80,  60,   6,  61, 174, 254, 174, 246,  72, 154,  31,
	 97,  40,  10,   8, 114, 203, 238,  26,  89,  51, 134, 110, 118, 176,  87,  32, 192, 210, 146, 207,
	 88,  45, 156, 179,  61, 224,  87, 107, 107,   1, 252, 187, 203, 100, 169, 211, 205, 105,  12, 231,
	137, 176, 166,  37, 192, 241, 169,  84,  32,  85, 112, 168, 154,   7, 247, 146, 183, 225, 246, 173,
	 57, 103, 110, 236, 113, 118, 203, 200,  22,  87, 251,   7, 138,  37,  12,  84, 221, 171,  51, 209,
	242,  37,  89,  73, 151, 162, 139, 189, 131, 209, 221,  96, 107, 144, 175,  79, 199, 123,  98, 138,
	226,  86, 221, 254,  72,  14, 126, 180, 200, 171,  85,  94, 120, 124, 196, 225, 150,  57, 219, 158
};