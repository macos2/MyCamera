/*
 * mycamera.c
 *
 *  Created on: 2019年12月30日
 *      Author: tom
 */

#include "mycamera.h"

int xioctl(int fd, int cmd, void *arg) {
	int result = 0;
	do {
		result = ioctl(fd, cmd, arg);
	} while (result == -1 && (errno == EAGAIN || errno == EINTR));
	return result;
}
;

FILE *my_camera_open_device(char *device_path, int Open_flags) {
	int times = 0;
	FILE *device = NULL;
	while (device == NULL) {
		device = fdopen(open(device_path,Open_flags, 0), "r+");
		if (device == NULL) {
			if(errno==EBUSY)continue;
			perror("Open Device Fail,Try Again");
			if (times++ > 10)
				break;
		}
	}
	return device;
}
MyCamera * my_camera_new(char *device_path) {
	int ret, i, j;
	FILE *file;
	file = my_camera_open_device(device_path,O_RDWR);
	if (file == NULL)
		return NULL;
	MyCamera *cam = calloc(1, sizeof(MyCamera));
	cam->dev = file;
	cam->dev_path = calloc(1, sizeof(device_path + 1));
	strcpy(cam->dev_path, device_path);
	//Query capability
	ret = xioctl(file->_fileno, VIDIOC_QUERYCAP, &cam->capability);
	if (ret != 0) {
		perror("VIDIOC_QUERYCAP FAIL\n");
		my_camera_free(cam);
		return NULL;
	}

	//Query input
	i = 0;
	do {
		cam->input[i].index = i;
		ret = xioctl(file->_fileno, VIDIOC_ENUMINPUT, &cam->input[i]);
		if (ret != 0)
			break;
		i++;
	} while (1);
	if (i == 0) {
		perror("VIDIOC_ENUMINPUT FAIL\n");
		my_camera_free(cam);
		return NULL;
	}
	for (j = 0; j < i; j++) {
		if (cam->input[j].type == V4L2_INPUT_TYPE_CAMERA) {
			cam->current_input = j;
			break;
		}
	}

	//Query pix format
	i = 0;
	do {
		cam->fmtdesc[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cam->fmtdesc[i].index = i;
		ret = xioctl(file->_fileno, VIDIOC_ENUM_FMT, &cam->fmtdesc[i]);
		if (ret != 0)
			break;
		i++;
	} while (1);
	if (i == 0) {
		perror("VIDIOC_ENUM_FMT FAIL\n");
		my_camera_free(cam);
		return NULL;
	}

	//Query frame size and interval
	i = 0;
	j = 0;
	do {
		cam->frame[i].frmsize.index = i;
		cam->frame[i].frmsize.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cam->frame[i].frmsize.pixel_format = cam->fmtdesc[0].pixelformat;
		ret = xioctl(file->_fileno, VIDIOC_ENUM_FRAMESIZES,
				&cam->frame[i].frmsize);
		if (ret != 0)
			break;
		j = 0;
		do {
			cam->frame[i].interval[j].index = j;
			cam->frame[i].interval[j].pixel_format =
					cam->fmtdesc[0].pixelformat;
			cam->frame[i].interval[j].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			cam->frame[i].interval[j].width =
					cam->frame[i].frmsize.discrete.width;
			cam->frame[i].interval[j].height =
					cam->frame[i].frmsize.discrete.height;
			ret = xioctl(file->_fileno, VIDIOC_ENUM_FRAMEINTERVALS,
					&cam->frame[i].interval[j]);
			if (ret != 0)
				break;
			j++;
		} while (1);
		i++;
	} while (1);
	if (i == 0) {
		perror("VIDIOC_ENUM_FRAMESIZES FAIL\n");
		my_camera_free(cam);
		return NULL;
	}
	fclose(cam->dev);
	cam->dev = NULL;
	return cam;
}
;
void my_camera_free(MyCamera *camera) {
	if (camera->status == MY_CAMERA_STATUS_CAPTURE_START)
		my_camera_stop_capture(camera);
	fclose(camera->dev);
	free(camera->dev_path);
	free(camera);
}
;
void my_camera_start_capture(MyCamera *camera) {
	int ret = 0, fd, i = 0;
	struct v4l2_format format;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	struct v4l2_streamparm stream_parm;
	if (camera->status == MY_CAMERA_STATUS_CAPTURE_START) {
		perror("Camera has started capture");
		return;
	}
	camera->dev= my_camera_open_device(camera->dev_path,O_RDWR);
	if(camera->dev==NULL){
		perror("open device fail\n");
		return;
	}
	fd = fileno(camera->dev);
	//Set or select input
	ret = xioctl(fd, VIDIOC_S_INPUT, &camera->input[camera->current_input]);
	if (ret != 0) {
		perror("VIDIOC_S_INPUT FAIL,TRY OTHER INPUT\n");

		for (i = 0; i < 5; i++)
			if (camera->input[i].type == V4L2_INPUT_TYPE_CAMERA)
				break;
		camera->current_input = i;
		ret = xioctl(fd, VIDIOC_S_INPUT, &camera->input[camera->current_input]);
	}
	if (ret != 0) {
		perror("VIDIOC_S_INPUT FAIL\n");
		return;
	}

	//Set output format
	CLEAR(format);
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.pixelformat =
			camera->fmtdesc[camera->current_fmtdesc].pixelformat;
	format.fmt.pix.width =
			camera->frame[camera->current_frm].frmsize.discrete.width;
	format.fmt.pix.height =
			camera->frame[camera->current_frm].frmsize.discrete.height;
	xioctl(fd, VIDIOC_TRY_FMT, &format);
	ret = xioctl(fd, VIDIOC_S_FMT, &format);
	if (ret != 0) {
		perror("VIDIOC_S_FMT FAIL\n");
		return;
	}

	//Set Stream Param
	CLEAR(stream_parm);
	stream_parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = xioctl(fd, VIDIOC_G_PARM, &stream_parm);
	if (ret == 0) {
		if (V4L2_CAP_TIMEPERFRAME & stream_parm.parm.capture.capability) {
			stream_parm.parm.capture.capturemode |= V4L2_CAP_TIMEPERFRAME;
			printf("Camera Support V4L2_CAP_TIMEPERFRAME\n");
		}
		if (V4L2_MODE_HIGHQUALITY & stream_parm.parm.capture.capability) {
			stream_parm.parm.capture.capturemode |= V4L2_MODE_HIGHQUALITY;
			printf("Try to set V4L2_MODE_HIGHQUALITY\n");
		}
		stream_parm.parm.capture.timeperframe.denominator =
				camera->frame[camera->current_frm].interval[camera->current_interval].discrete.denominator;
		stream_parm.parm.capture.timeperframe.numerator =
				camera->frame[camera->current_frm].interval[camera->current_interval].discrete.numerator;
		ret = xioctl(fd, VIDIOC_S_PARM, &stream_parm);
		if (ret != 0) {
			perror("Set Stream Parm FAIL\n");
		}
	}

	//Request buffer
	CLEAR(req);
	req.count = N_REQBUF;
	req.memory = V4L2_MEMORY_MMAP;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = xioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret != 0) {
		perror("VIDIOC_REQBUFS FAIL\n");
		return;
	}

	//Query buffer
	CLEAR(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	for (i = 0; i < N_REQBUF; i++) {
		camera->reqbuf_len[i] = 0;
		camera->reqbuf_offset[i] = NULL;
		buf.index = i;
		ret = xioctl(fd, VIDIOC_QUERYBUF, &buf);
		if (ret != 0)
			continue;
		camera->reqbuf_len[i] = buf.length;
		camera->reqbuf_offset[i] = mmap(NULL, buf.length, PROT_READ, MAP_SHARED,
				fd, buf.m.offset);
	}

	//Queue buffer
	for (i = 0; i < N_REQBUF; i++) {
		if (camera->reqbuf_offset[i] == NULL)
			continue;
		buf.index = i;
		ret = xioctl(fd, VIDIOC_QBUF, &buf);
		if (ret != 0) {
			fprintf(stderr, "%d BUFFER VIDIOC_QBUF FAIL\n", i);
			if (i > 0)
				continue;
			else
				return;
		}
	}

	//Stream ON
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = xioctl(fd, VIDIOC_STREAMON, &type);
	if (ret != 0) {
		perror("VIDIOC_STREAMON FAIL\n");
		for (i = 0; i < N_REQBUF; i++) {
			if (camera->reqbuf_offset[i] != NULL)
				munmap(camera->reqbuf_offset, camera->reqbuf_len[i]);
			camera->reqbuf_len[i] = 0;
			camera->reqbuf_offset[i] = NULL;
		}
		return;
	}
	camera->status = MY_CAMERA_STATUS_CAPTURE_START;
}
;
void *my_camera_get_image(MyCamera *camera) {
	if (camera->status != MY_CAMERA_STATUS_CAPTURE_START || camera->dev == NULL) {
		perror("camera have never start capture or open the device");
		return NULL;
	}
	struct v4l2_buffer buf;
	int ret = 0, fd = fileno(camera->dev);
	CLEAR(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = camera->DQBUF_INDEX;
	ret = xioctl(fd, VIDIOC_DQBUF, &buf);
	if (ret != 0)
		return NULL;
	char *temp = malloc(buf.bytesused + 1);
	memcpy(temp, camera->reqbuf_offset[buf.index], buf.bytesused);
	temp[buf.bytesused] = '\0';
	xioctl(fd, VIDIOC_QBUF, &buf);
	camera->DQBUF_INDEX++;
	return temp;
}
;
void my_camera_stop_capture(MyCamera *camera) {
	int ret, fd, i;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (camera == NULL || camera->status != MY_CAMERA_STATUS_CAPTURE_START)
		return;
	fd = fileno(camera->dev);
	ret = xioctl(fd, VIDIOC_STREAMOFF, &type);
	if (ret != 0)
		perror("VIDIOC_STREAMOFF FAIL\n");

	for (i = 0; i < N_REQBUF; i++) {
		ret = munmap(camera->reqbuf_offset[i], camera->reqbuf_len[i]);
		if (ret == -1) {
			fprintf(stderr, "%d munmap fail\n", i);
			continue;
		}
		camera->reqbuf_len[i] = 0;
		camera->reqbuf_offset[i] = NULL;
	}
	camera->status = MY_CAMERA_STATUS_CAPTURE_STOP;
	fclose(camera->dev);
	camera->dev = NULL;
	camera->DQBUF_INDEX = 0;
}
;
