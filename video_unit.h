/*
 * video_unit.h
 *
 *  Created on: 2019年12月12日
 *      Author: tom
 */

#ifndef VIDEO_UNIT_H_
#define VIDEO_UNIT_H_
#include <stdlib.h>
#include <stdio.h>

/*pix to pix transform
*y=y,cb=u,cr=v
*a yuv  pix to a 32bit argb pix(each element 8bit);
*/
int yuv_to_argb(unsigned char y,unsigned char cb,unsigned char cr,int alpha);

/*Map:
 *yyyy
 *yyyy
 *uu
 *uu
 *vv
 *vv
 * */
void * yuv422p_to_argb(void *data, int width, int height, int alpha);

/*Map:
 *yuyvyuyv
 *yuyvyuyv
 * */
void * yuyv_to_argb(void *data, int width, int height, int alpha);

/*Map:
 * yyyy
 * yyyy
 * uu
 * vv
 * */
void * yu12_to_argb(void *data, int width, int height, int alpha);

#endif /* VIDEO_UNIT_H_ */
