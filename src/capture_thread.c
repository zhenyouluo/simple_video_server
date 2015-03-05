/* ==========================================================================
 * @file    : capture_thread.c
 *
 * @description : This file contains the video capture thread.
 *
 * @author  : Aman Kumar (2015)
 *
 * @copyright   : The code contained herein is licensed under the GNU General
 *				Public License. You may obtain a copy of the GNU General
 *				Public License Version 2 or later at the following locations:
 *              http://www.opensource.org/licenses/gpl-license.html
 *              http://www.gnu.org/copyleft/gpl.html
 * ========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <common.h>

struct capturebuffer {
	unsigned char *start;
	size_t offset;
	unsigned int length;
};

extern char g_video_device[30];
extern char *g_jpeg_frame;
extern int g_capture_width;
extern int g_capture_height;
extern int g_osdflag;
extern int g_writeflag;
extern int g_take_snapshot;
extern int g_image_save;
extern unsigned int g_framesize;
extern char *g_framebuff[NUM_BUFFER];
extern int g_enable_osdthread;
extern int g_enable_filerecordthread;
extern int g_enable_jpegsavethread;
extern int current_task;

void *captureThread(void)
{
	int fd, i;
	unsigned int frame_cnt;
	enum v4l2_buf_type type;
	struct v4l2_capability cap;
	struct v4l2_streamparm parm;
//	struct v4l2_cropcap cropcap;
//	struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	struct capturebuffer buffers[NUM_BUFFER];

	if((fd = open(g_video_device, O_RDWR, 0)) < 0) {
		perror("video device open");
		return NULL;
	}

	if(ioctl(fd, VIDIOC_QUERYCAP, &cap) == FAIL) {
		perror("VIDIOC_QUERYCAP");
		return NULL;
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		printf("NO CAPTURE SUPPORT\n");
		return NULL;
	}
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		printf("NO STREAMING SUPPORT\n");
		return NULL;
	}
 
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = 25;
	parm.parm.capture.capturemode = 0;
	if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
		perror("VIDIOC_S_PARM\n");
		return NULL;
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = g_capture_width;
	fmt.fmt.pix.height = g_capture_height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) == FAIL) {
		perror("VIDIOC_S_FMT");
		return NULL;
	}

	memset(&req, 0, sizeof(req));
	req.count = NUM_BUFFER;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &req) == FAIL) {
		perror("VIDIOC_REQBUFS");
		return NULL;
	}

	for(i = 0; i < NUM_BUFFER; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == FAIL) {
			perror("VIDIOC_QUERYBUF");
			return NULL;
		}

		buffers[i].length = buf.length;
		buffers[i].offset = (size_t) buf.m.offset;
		buffers[i].start = mmap(NULL, buffers[i].length,
									PROT_READ | PROT_WRITE,
									MAP_SHARED, fd, buffers[i].offset);
		memset(buffers[i].start, 0xFF, buffers[i].length);
	}

	for (i = 0; i < NUM_BUFFER; i++) {
		memset(&buf, 0, sizeof (buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.m.offset = buffers[i].offset;
		if (ioctl (fd, VIDIOC_QBUF, &buf) == FAIL) {
			perror("VIDIOC_QBUF");
			return NULL;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl (fd, VIDIOC_STREAMON, &type) == FAIL) {
		perror("VIDIOC_STREAMON");
		return NULL;
	}
	sleep(1);
	i = 0;
	frame_cnt = 0;
	while(!KillCaptureThread) {
		memset(&buf, 0, sizeof buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if(ioctl(fd, VIDIOC_DQBUF, &buf) == FAIL) {
			perror("VIDIOC_DQBUF");
			return NULL;
		}
		frame_cnt++;
		memcpy(g_framebuff[i],buffers[buf.index].start,g_framesize);
		if(g_enable_jpegsavethread) {
			if(g_take_snapshot) {
				g_take_snapshot = FALSE;
				g_jpeg_frame = calloc(g_framesize, 1);
				memcpy(g_jpeg_frame,g_framebuff[i],g_framesize);
				g_image_save = TRUE;
			}
		}
		if(g_enable_osdthread) {
			g_osdflag = 1;
		//	current_task = TASK_FOR_OSD;
		} else if(g_enable_filerecordthread) {
			g_writeflag = 1;
		//	current_task = TASK_FOR_FILERECORD;
		}
		i++;
		if(i > 9) i = 0;
		if(ioctl(fd, VIDIOC_QBUF, &buf) == FAIL) {
			perror("VIDIOC_QBUF");
			return NULL;
		}
	}
	return 0;
}