/*
 * video_unit.c
 *
 *  Created on: 2019年12月12日
 *      Author: tom
 *      https://github.com/macos2/MyCamera
 *		https://gitee.com/macos2/MyCamera
 */

#include "video_unit.h"

int yuv_to_argb(unsigned char y, unsigned char cb, unsigned char cr, int alpha) {
	unsigned char a, r, g, b;
	a = alpha; //cause 32bit to 8bit transforms
//	r = y + ((360 * (cr - 128)) >> 8);
//	g = y - (((88 * (cb - 128) + 184 * (cr - 128))) >> 8);
//	b = y + ((455 * (cb - 128)) >> 8);
	//formula
//				r = 1.164*(y-16) + 1.596*(cr-128);
//				g = 1.164*(y-16) - 0.813*(cr-128) - 0.392*(cb-128);
//				b = 1.164*(y-16) + 2.017*(cb-128);

	//other transform formula
				b=y+1.772*(cb-128);
				g=y-0.34414*(cb-128)-0.71414*(cr-128);
				r=y+1.402*(cr-128);
	r = r * a / 255;
	g = g * a / 255;
	b = b * a / 255;
	//'a r g b' order in a 32bit int,each element 8bit.
	return (a << 24) + (r << 16) + (g << 8) + b;
}

void * yuv422p_to_argb(void *data, int width, int height, int alpha) {
	unsigned char *y, *cb, *cr;
	int w, h, half_w = width / 2;
	int *ret, *re;
	y = data;
	cb = y + width * height;
	cr = cb + width * height / 4;
	ret = malloc( width * height * 4);
	re = ret;
	for (h = 0; h < height; h++) {
		for (w = 0; w < half_w; w++) {
			//transform 2 pix each time.
			*ret = yuv_to_argb(*y, *cb, *cr, alpha);
			ret++;
			y++;
			*ret = yuv_to_argb(*y, *cb, *cr, alpha);
			ret++;
			y++;
			cb++;
			cr++;
		}
	}
	return re;
}
;

void * yuyv_to_argb(void *data, int width, int height, int alpha) {
	unsigned char *y, *cb, *cr;
	unsigned char r, g, b;
	int w, h, half_w = width / 2;
	int *ret, *re;
	y = data;
	cb = data + 1;
	cr = data + 3;
	ret = malloc(width * height * 4);
	re = ret;
	for (h = 0; h < height; h++) {
		for (w = 0; w < half_w; w++) {
			//transform 2 pix each time.
			*ret = yuv_to_argb(*y, *cb, *cr, alpha);
			ret++;
			y += 2;
			*ret = yuv_to_argb(*y, *cb, *cr, alpha);
			ret++;
			y += 2;
			cb += 4;
			cr += 4;
		}
	}
	return re;
}
;

void * yu12_to_argb(void *data, int width, int height, int alpha) {
	unsigned char *y, *cb, *cr;
	unsigned char r, g, b;
	int w, h, half_w = width / 2;
	int *ret, *re;
	y = data;
	cb = y + width * height;
	cr = cb + width * height / 4;
	ret = malloc( width * height * 4 );
	re = ret;
	for (h = 1; h < height + 1; h++) {
		for (w = 0; w < half_w; w++) {
			//transform 2 pix each time.
			*ret = yuv_to_argb(*y, *cb, *cr, alpha);
			ret++;
			y++;
			*ret = yuv_to_argb(*y, *cb, *cr, alpha);
			ret++;
			y++;
			cb++;
			cr++;
		}
		//Even numbers row  use the same uv values with pervious row;
		if (h % 2 != 0) {
			cb -= half_w;
			cr -= half_w;
		}
	}
	return re;
}
;

