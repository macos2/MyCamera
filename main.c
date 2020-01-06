/*
 * main.c
 *
 *  Created on: 2019年12月29日
 *      Author: tom
 */

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
#include <gtk/gtk.h>
#include <cairo.h>
#include "video_unit.h"
#include "mycamera.h"
#include "gresource.h"

typedef struct {
	void *argb;
	void *camera_data;
	guint w, h;
	size_t image_size, buf_size;
	time_t time;
} image_data;

typedef struct {
	GMutex lock;
	GAsyncQueue *draw_queue;
	gboolean stop, restart_camera, normal_size, frame_size_refresh,
			frame_fps_refresh;
	MyCamera *cam;
	GThread *get_image_thread;
	gchar *dev_path;
	gdouble roate_degree;
} thread_data;

typedef struct {
	GtkWindow *ui;
	GtkToggleButton *start, *normal_size;
	GtkButton *save_image;
	GtkBuilder *builder;
	GtkDrawingArea *display;
	GtkListStore *frame_size;
	GtkComboBox *frame_size_combobox;
} Ui;

Ui *ui;
image_data *current_image;
gint fps, pop_fps, lost_fps;
gchar *device_path;

void image_convert_thread(image_data *i_data, thread_data *t_data);

gboolean get_image_thread_loop(gpointer p[]) {
	gint i;
	MyCamera *cam = p[0];
	GThreadPool *convert_pool = p[1];
	GMainLoop *loop = p[2];
	thread_data *t_data = p[3];
	image_data *image = NULL;
	if (t_data->restart_camera == FALSE && t_data->stop == FALSE) {
		image = calloc(1, sizeof(image_data));
		image->camera_data = my_camera_get_image(cam);
		image->w = cam->frame[cam->current_frm].frmsize.discrete.width;
		image->h = cam->frame[cam->current_frm].frmsize.discrete.height;
		image->time = time(NULL);
		if (image->camera_data != NULL)
			g_thread_pool_push(convert_pool, image, NULL);
		else
			image_data_free(image);
	}

	g_mutex_lock(&t_data->lock);
	if (t_data->stop) {
		g_mutex_unlock(&t_data->lock);
		g_main_loop_quit(loop);
		return G_SOURCE_REMOVE;
	}
	g_mutex_unlock(&t_data->lock);
	return G_SOURCE_CONTINUE;
}

gpointer get_image_thread(thread_data *t_data) {
	MyCamera *cam = NULL;
	g_mutex_lock(&t_data->lock);
	if (t_data->cam == NULL)
		cam = my_camera_new(t_data->dev_path);
	else
		cam = t_data->cam;
	g_mutex_unlock(&t_data->lock);
	if (cam == NULL)
		return -1;
	my_camera_start_capture(cam);
	g_mutex_lock(&t_data->lock);
	t_data->cam = cam;
	g_mutex_unlock(&t_data->lock);
	GThreadPool *convert_pool = g_thread_pool_new(image_convert_thread, t_data,
			8, FALSE,
			NULL);
	GMainContext *context = g_main_context_new();
	GSource *timeout = g_timeout_source_new(20);
	GMainLoop *loop = g_main_loop_new(context, FALSE);
	gpointer p[] = { cam, convert_pool, loop, t_data };
	g_source_set_callback(timeout, get_image_thread_loop, p, NULL);
	g_source_attach(timeout, context);
	g_main_loop_run(loop);

	g_main_loop_unref(loop);
	g_main_context_unref(context);
	g_source_unref(timeout);
	g_thread_pool_free(convert_pool, TRUE, FALSE);
	my_camera_stop_capture(cam);
	g_mutex_lock(&t_data->lock);
	g_thread_unref(t_data->get_image_thread);
	t_data->get_image_thread = NULL;
	g_mutex_unlock(&t_data->lock);
	return 0;
}
;

void image_convert_thread(image_data *i_data, thread_data *t_data) {
	if (i_data == NULL)
		return;
	if (i_data->camera_data == NULL)
		image_data_free(i_data);
	int i;
	g_mutex_lock(&t_data->lock);
	i = t_data->cam->fmtdesc[t_data->cam->current_fmtdesc].pixelformat;
	g_mutex_unlock(&t_data->lock);
	switch (i) {
	case V4L2_PIX_FMT_YUYV:
		i_data->argb = yuyv_to_argb(i_data->camera_data, i_data->w, i_data->h,
				255);
		break;
	case V4L2_PIX_FMT_YUV422P:
		i_data->argb = yuv422p_to_argb(i_data->camera_data, i_data->w,
				i_data->h, 255);
		break;
	case V4L2_PIX_FMT_YUV420:
		i_data->argb = yu12_to_argb(i_data->camera_data, i_data->w, i_data->h,
				255);
		break;
	default:
		perror("Unknow Pix Format,Try To use v4l convert");
		image_data_free(i_data);
		i_data = NULL;
	}
	g_mutex_lock(&t_data->lock);
	if (t_data->stop) {
		image_data_free(i_data);
	} else {
		g_async_queue_push(t_data->draw_queue, i_data);
	}
	g_mutex_unlock(&t_data->lock);
	return;
}
;

gboolean image_refresh(thread_data *t_data) {
	g_mutex_lock(&t_data->lock);
	if (g_async_queue_length(t_data->draw_queue) > 0)
		gtk_widget_queue_draw(ui->display);
	g_mutex_unlock(&t_data->lock);
	return G_SOURCE_CONTINUE;
}
;
void image_data_free(image_data *i_data) {
	if (i_data == NULL)
		return;
	g_free(i_data->argb);
	g_free(i_data->camera_data);
	g_free(i_data);
}

gboolean image_draw(GtkWidget *widget, cairo_t *cr, thread_data *t_data) {
	image_data *i_data = NULL, *lastest_i_data = NULL;
	GtkAllocation alloc;
	gint len;
	time_t time = 0;
	g_mutex_lock(&t_data->lock);
	len = g_async_queue_length(t_data->draw_queue);
	if (len > 0) {
		do {
			i_data = g_async_queue_try_pop(t_data->draw_queue);
			if (i_data == NULL)
				break;
			pop_fps++;
			if (i_data->time > time) {
				image_data_free(lastest_i_data);
				lastest_i_data = i_data;
				time = i_data->time;
			} else {
				image_data_free(i_data);
				lost_fps++;
			}
		} while (TRUE);
	}
	g_mutex_unlock(&t_data->lock);
	gtk_widget_get_allocation(widget, &alloc);

	if (lastest_i_data == NULL) {
		if (t_data->stop) {
			cairo_save(cr);
			//gint min=alloc.height>alloc.width?alloc.width:alloc.height;
			cairo_set_font_size(cr, 20);
			cairo_text_extents_t ext;
			cairo_text_extents(cr, "Press 'Start' to Capture Video", &ext);
			cairo_translate(cr, (alloc.width - ext.width) / 2,
					(alloc.height - ext.height) / 2);
			cairo_show_text(cr, "Press 'Start' to Capture Video");
			cairo_set_source_rgba(cr, 1., 0.3, 0.2, 0.8);
			cairo_stroke(cr);
			cairo_restore(cr);
			return TRUE;
		} else if (current_image != NULL)
			lastest_i_data = current_image;
		else
			return FALSE;
	}
	//printf("%d\t%d\t%d\t%d",alloc.x,alloc.y,alloc.width,alloc.height);
	cairo_save(cr);
	cairo_surface_t *surf = cairo_image_surface_create_for_data(
			lastest_i_data->argb, CAIRO_FORMAT_ARGB32, lastest_i_data->w,
			lastest_i_data->h,
			cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32,
					lastest_i_data->w));
	cairo_surface_set_device_offset(surf, lastest_i_data->w / 2.,
			lastest_i_data->h / 2.);
	gdouble scale = 1.;
	if (t_data->normal_size) {
		if (alloc.width < lastest_i_data->w)
			alloc.width = lastest_i_data->w;
		if (alloc.height < lastest_i_data->h)
			alloc.height = lastest_i_data->h;
	} else {
		if (t_data->roate_degree != G_PI && t_data->roate_degree != 0 && t_data->roate_degree !=-1.* G_PI) {
			if (alloc.width > alloc.height) {
				//+-90 +-270 degree
				scale = (gdouble) alloc.height / lastest_i_data->w;
			} else {
				scale = (gdouble) alloc.width / lastest_i_data->h;
			};
		} else {
			//0 +-180 degree
			if (alloc.width > alloc.height) {
				scale = (gdouble) alloc.height / lastest_i_data->h;
			} else {
				scale = (gdouble) alloc.width / lastest_i_data->w;
			};
		}
	}
	cairo_translate(cr, (alloc.width) / 2., (alloc.height) / 2.);
	cairo_rotate(cr, t_data->roate_degree);
	cairo_scale(cr, scale, scale);

	cairo_set_source_surface(cr, surf, 0, 0);
	cairo_paint(cr);
	cairo_surface_destroy(surf);
	cairo_restore(cr);
	fps++;
	if (lastest_i_data != current_image) {
		image_data_free(current_image);
		current_image = lastest_i_data;
	}
	return TRUE;
}
;

gboolean fps_refresh(thread_data *t_data) {
	g_print("\e[s\e[1;32mFps:%4d\tPop Fps:%4d\tLost Fps:%4d\e[0m\e[u", fps, pop_fps,
			lost_fps);
	fps = 0;
	lost_fps = 0;
	pop_fps = 0;
	return G_SOURCE_CONTINUE;
}

void combobox_frame_size_refresh(thread_data *t_data) {
	gint i = 0, j = 0;
	gtk_list_store_clear(ui->frame_size);
	GtkTreeIter iter;
	gchar *temp;
	gtk_tree_model_get_iter_first(ui->frame_size, &iter);
	g_mutex_lock(&t_data->lock);
	while (t_data->cam->frame[i].frmsize.discrete.height
			* t_data->cam->frame[i].frmsize.discrete.width > 0) {
		temp = g_strdup_printf(":%4d x %4d",
				t_data->cam->frame[i].frmsize.discrete.width,
				t_data->cam->frame[i].frmsize.discrete.height);
		gtk_list_store_append(ui->frame_size, &iter);
		gtk_list_store_set(ui->frame_size, &iter, 0, i + 1, 1, temp, -1);
		g_free(temp);
		i++;
	}
	t_data->frame_size_refresh = TRUE;
	g_mutex_unlock(&t_data->lock);
}

//ui callback function
void start_stop_capture_cb(GtkToggleButton *togglebutton, thread_data *t_data) {
	g_print("%s\n", __func__);
	g_mutex_lock(&t_data->lock);
	t_data->stop = !gtk_toggle_button_get_active(togglebutton);
	g_mutex_unlock(&t_data->lock);
	if (t_data->stop == FALSE && t_data->get_image_thread == NULL) {
		t_data->get_image_thread = g_thread_new("get-image", get_image_thread,
				t_data);
	}
	gtk_widget_queue_draw(ui->display);
}

void normal_size_toggled_cb(GtkToggleButton *togglebutton, thread_data *t_data) {
	g_print("%s\n", __func__);
	g_mutex_lock(&t_data->lock);
	t_data->normal_size = gtk_toggle_button_get_active(togglebutton);
	g_mutex_unlock(&t_data->lock);
}

void save_photo_cb(GtkButton *button, thread_data *t_data) {
	g_print("%s\n", __func__);
	gchar *filename = NULL, *temp, *argb;
	gint w, h;
	size_t size;
	w = current_image->w;
	h = current_image->h;
	size = w * h * 4;
	argb = malloc(size);
	memcpy(argb, current_image->argb, size);
	cairo_surface_t *save_image = cairo_image_surface_create_for_data(argb,
			CAIRO_FORMAT_ARGB32, w, h,
			cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w));
	GtkFileChooserDialog *dialog = gtk_file_chooser_dialog_new("Save Photo",
			gtk_widget_get_parent_window(button), GTK_FILE_CHOOSER_ACTION_SAVE,
			"Save", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);
	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filter, "*.png");
	gtk_file_filter_set_name(filter, "PNG Files");
	gtk_file_chooser_add_filter(dialog, filter);
	gtk_file_chooser_set_current_name(dialog, "image.png");
	switch (gtk_dialog_run(dialog)) {
	case GTK_RESPONSE_OK:
		filename = gtk_file_chooser_get_filename(dialog);
		if (!g_str_has_suffix(filename, ".png")) {
			temp = g_strdup_printf("%s.png", filename);
			g_free(filename);
			filename = temp;
		}
		;
		gtk_widget_destroy(dialog);
		break;
	case GTK_RESPONSE_CANCEL:
	default:
		gtk_widget_destroy(dialog);
		break;
	}
	if (filename != NULL) {
		cairo_surface_write_to_png(save_image, filename);
		g_free(filename);
	}
	cairo_surface_destroy(save_image);
	g_free(argb);
}

void frame_size_changed_cb(GtkComboBox *widget, thread_data *t_data) {
	g_print("%s\n", __func__);
	gchar id;
	g_mutex_lock(&t_data->lock);
	if (t_data->cam != NULL) {
		id = gtk_combo_box_get_active(widget);
		if (id < 0) {
			id = 0;
			gtk_combo_box_set_active(widget, id);
		}
		t_data->cam->current_frm = id;
		my_camera_stop_capture(t_data->cam);
		if(gtk_toggle_button_get_active(ui->start))
			my_camera_start_capture(t_data->cam);
	}
	t_data->restart_camera = FALSE;
	g_mutex_unlock(&t_data->lock);
}
void rotate_left_cb(GtkButton *button, thread_data *t_data) {
	g_print("%s\n", __func__);
	t_data->roate_degree -= G_PI / 2.;
	if(t_data->roate_degree==2.*G_PI||t_data->roate_degree==-2.*G_PI)t_data->roate_degree=0;
}
void rotate_right_cb(GtkButton *button, thread_data *t_data) {
	g_print("%s\n", __func__);
	t_data->roate_degree += G_PI / 2.;
	if(t_data->roate_degree==2.*G_PI||t_data->roate_degree==-2.*G_PI)t_data->roate_degree=0;
}


void light_toggled_cb(GtkToggleButton *togglebutton, thread_data *t_data) {
	g_print("%s\n", __func__);
	struct v4l2_control control;
	int fd=fileno(t_data->cam->dev);
	CLEAR(control);
	control.id=0x009c0901;//led_mode,see cmd "v4l2-ctl -L" in bash shell;
	if(gtk_toggle_button_get_active(togglebutton)){
	control.value=2;//ON,see cmd "v4l2-ctl -L" in bash shell;
	}else{
	control.value=0;//OFF,see cmd "v4l2-ctl -L" in bash shell;
	}
	if(xioctl(fd,VIDIOC_S_CTRL,&control)!=0)
		perror("On/Off light fail.");
		printf("\e[1;32mvCMD'4l2-ctl -L' for detial\n\e[0m");
}

void ui_init(thread_data *t_data) {
	ui = malloc(sizeof(Ui));
	//ui->builder = gtk_builder_new_from_file("ui.glade");
	ui->builder=gtk_builder_new_from_resource("/my/ui.glade");
	ui->normal_size = gtk_builder_get_object(ui->builder, "normal_size");
	ui->save_image = gtk_builder_get_object(ui->builder, "save_image");
	ui->start = gtk_builder_get_object(ui->builder, "start");
	ui->ui = gtk_builder_get_object(ui->builder, "ui");
	ui->display = gtk_builder_get_object(ui->builder, "display");
	ui->frame_size = gtk_builder_get_object(ui->builder, "frame_size");
	ui->frame_size_combobox = gtk_builder_get_object(ui->builder,
			"frame_size_combobox");

	gtk_builder_add_callback_symbol(ui->builder, "start_stop_capture_cb",
			start_stop_capture_cb);
	gtk_builder_add_callback_symbol(ui->builder, "normal_size_toggled_cb",
			normal_size_toggled_cb);
	gtk_builder_add_callback_symbol(ui->builder, "save_photo_cb",
			save_photo_cb);
	gtk_builder_add_callback_symbol(ui->builder, "frame_size_changed_cb",
			frame_size_changed_cb);

	gtk_builder_add_callback_symbol(ui->builder, "rotate_left_cb",
			rotate_left_cb);
	gtk_builder_add_callback_symbol(ui->builder, "rotate_right_cb",
			rotate_right_cb);
	gtk_builder_add_callback_symbol(ui->builder, "light_toggled_cb",
			light_toggled_cb);

	gtk_builder_connect_signals(ui->builder, t_data);
	g_signal_connect(ui->display, "draw", image_draw, t_data);
	combobox_frame_size_refresh(t_data);
	gtk_combo_box_set_active(ui->frame_size_combobox, t_data->cam->current_frm);
	gtk_widget_show_all(ui->ui);
}

GOptionEntry option_entry[]={
		{"device",'d',G_OPTION_FLAG_FILENAME,G_OPTION_ARG_STRING,&device_path,"Special Device Path,default:'/dev/video0'","[Device Path]"},
};


int main(int argc, char *argv[]) {
	gint i, j;
	gtk_init(&argc, &argv);
	GError *err;
	GOptionContext *context=g_option_context_new(NULL);
	g_option_context_add_main_entries(context, option_entry,NULL);
	if(!g_option_context_parse(context,&argc,&argv,&err)){
		perror(err->message);
		return EXIT_FAILURE;
	};
	g_option_context_free(context);

	current_image = NULL;
	GAsyncQueue *convert_queue, *draw_queue;

	thread_data *t_data = calloc(1, sizeof(thread_data));
	t_data->normal_size = TRUE;
	if(device_path==NULL)device_path=g_strdup("/dev/video0");
	g_print("\e[1;32mOpen %s...\e[0m\n",device_path);
	t_data->dev_path = g_strdup(device_path);
	t_data->cam = my_camera_new(t_data->dev_path);
	if(t_data->cam==NULL){
		perror("Open Device Fail");
		return EXIT_FAILURE;
	}
	t_data->roate_degree = 0;

	j = 1;
	//find the compatable format to capture
	for (i = 0; i < 10; i++) {
		switch (t_data->cam->fmtdesc[i].pixelformat) {
		case V4L2_PIX_FMT_YUYV:
		//case V4L2_PIX_FMT_YUV422P://for OrangePi PC2 gc2035 csi camera should common this line for work fine
		case V4L2_PIX_FMT_YUV420:
			t_data->cam->current_fmtdesc = i;
			j = 0;
			break;
		default:
			break;
		}
		if (j == 0)
			break;
	}
	g_mutex_init(&t_data->lock);
	//the image in queue for convert in sub-thread
	convert_queue = g_async_queue_new();

	//the image in queue for display
	draw_queue = g_async_queue_new();
	t_data->draw_queue = draw_queue;

	ui_init(t_data);
	//sub-thread to take photo from camera.
	t_data->get_image_thread = g_thread_new("get-image", get_image_thread,
			t_data);

	//check whether new image in the draw_queue and notify program to refresh display.
	g_timeout_add(20, image_refresh, t_data);
	//for benchmark how many images could be token or display in one second.
	g_timeout_add_seconds(1, fps_refresh, NULL);
	gtk_main();
	return EXIT_SUCCESS;
}
