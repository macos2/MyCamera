mycamera:gresource.c gresource.h $(shell find *.c *.h *.glade)
		@echo "\e[1;32mBuild...\e[0m"
		@rm -f gresource.h gresource.c
		@glib-compile-resources --generate-header gresource.xml
		@glib-compile-resources --generate-source gresource.xml
		@gcc -o $@ $(shell find *.c) -lv4l2 `pkg-config --libs --cflags gtk+-3.0` -w
gresource.c:gresource.xml
		@glib-compile-resources --generate-source gresource.xml
gresource.h:gresource.xml
		@glib-compile-resources --generate-header gresource.xml
all:mycamera
