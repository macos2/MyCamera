mycamera:$(shell find *.c *.h *.glade *.xml)
		@echo "\e[1;32mBuild...\e[0m"
		@rm -f gresource.h gresource.c
		@glib-compile-resources --generate-header gresource.xml
		@glib-compile-resources --generate-source gresource.xml
		@gcc -o $@ $(shell find *.c) -lv4l2 `pkg-config --libs --cflags gtk+-3.0` -w
all:mycamera
