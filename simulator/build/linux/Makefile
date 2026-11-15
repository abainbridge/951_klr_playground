src_dir=../..
obj_dir=obj
inc_dirs=-I ../.. -I ../../deadfrog

COMPILE_AND_LINK_FLAGS=-lm #-flto #-fsanitize=address


cflags=-MMD -O0 -g -march=native \
	-fdata-sections -ffunction-sections \
	-fno-strict-aliasing $(COMPILE_AND_LINK_FLAGS) \
	-ffast-math \
	-Wall -Wno-parentheses -Wno-sign-compare -Wno-switch -Wno-unused-result -Wshadow
#	-fstack-protector-all -fstack-clash-protection -fstack-protector-strong \
#	-ftrapv \

cxxflags=$(cflags) -Wno-reorder -Wno-class-memaccess -fno-threadsafe-statics -fno-rtti -fno-exceptions

cpp_files=\
	../../deadfrog/df_bitmap.cpp \
	../../deadfrog/df_colour.cpp \
	../../deadfrog/df_common_linux.cpp \
	../../deadfrog/df_font.cpp \
	../../deadfrog/df_message_dialog.cpp \
	../../deadfrog/df_time.cpp \
	../../deadfrog/df_window.cpp	\
	../../deadfrog/fonts/df_mono.cpp \
	../../deadfrog/fonts/df_prop.cpp


c_files=\
	../../cpu.c \
	../../graph.c \
	../../main.c \
	../../virtual_car.c

o_files=$(patsubst $(src_dir)/%.cpp,$(obj_dir)/%.o,$(cpp_files)) $(patsubst $(src_dir)/%.c,$(obj_dir)/%.o,$(c_files))
d_files=$(patsubst %.o,%.d,$(o_files))

../../sim: $(o_files)
	LC_ALL=en_US g++ -o $@ $(o_files) -Wl,--gc-sections $(COMPILE_AND_LINK_FLAGS) -static

# This magic line makes Make pay attention to the dependency files
-include $(d_files)

$(obj_dir)/%.o: $(src_dir)/%.cpp Makefile
	@mkdir -p $(dir $@)
	LC_ALL=en_US g++ $(cxxflags) $(inc_dirs) $< -c -o $@

$(obj_dir)/%.o: $(src_dir)/%.c Makefile
	@mkdir -p $(dir $@)
	LC_ALL=en_US gcc $(cflags) $(inc_dirs) $< -c -o $@

clean:
	rm -rf obj deadfrog ../../sim
