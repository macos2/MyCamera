/*
 * mycamera.h
 *
 *  Created on: 2019年12月30日
 *      Author: tom
 */

#ifndef MYCAMERA_H_
#define MYCAMERA_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <libv4l2.h>
#include <linux/fs.h>

#define CLEAR(x) memset(&x,0,sizeof(x))
#define N_REQBUF 4
typedef enum {
	MY_CAMERA_STATUS_CAPTURE_STOP, MY_CAMERA_STATUS_CAPTURE_START
} MyCameraStatus;

typedef struct {
	struct v4l2_frmsizeenum frmsize;
	struct v4l2_frmivalenum interval[5];
} frmsize_frmival;

typedef struct {
	struct v4l2_capability capability;
	struct v4l2_fmtdesc fmtdesc[10];
	struct v4l2_input input[5];
	frmsize_frmival frame[10];
	int current_fmtdesc, current_frm, current_input,current_interval;
	FILE *dev;
	char *dev_path;
	MyCameraStatus status;
	size_t reqbuf_len[N_REQBUF];
	void *reqbuf_offset[N_REQBUF];
	unsigned char DQBUF_INDEX;
} MyCamera;

int xioctl(int fd, int cmd, void *arg);
MyCamera * my_camera_new(char *device_path);
void my_camera_free(MyCamera *camera);
void my_camera_start_capture(MyCamera *camera);
void *my_camera_get_image(MyCamera *camera);
void my_camera_stop_capture(MyCamera *camera);

#endif /* MYCAMERA_H_ */
