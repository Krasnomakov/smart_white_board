CFLAGS=-Wall -O3 -g -Wextra -Wno-unused-parameter
CXXFLAGS=$(CFLAGS)

# Use only rules in this file so builds always include RGB include/lib flags.
.SUFFIXES:

# Modes launched by master_script.cpp
MODE_CPP_BINARIES=updated_pong_with_rotary brick_breaker wave_gen sensor_driven_visuals_7 \
	falling_sand snake_in_labyrinth stars cat_n_mouse 2_mice_1_cat \
	falling_sand_rotary_color dynamic_fireworks_rotary interactive_weather_rotary \
	coral_garden_rotary 2_player_pong main_mode_rotary 3d_labyrinth_control thermal_display
MODE_CC_BINARIES=weather_info_panel nasa_image updated_ollama

BINARIES=$(MODE_CPP_BINARIES) $(MODE_CC_BINARIES)
OBJECTS=$(MODE_CPP_BINARIES:%=%.o) $(MODE_CC_BINARIES:%=%.o)
EXTRA_BINARIES=master_script buffer_process db_manager

# Where our library resides. You mostly only need to change the
# RGB_LIB_DISTRIBUTION, this is where the library is checked out.
RGB_LIB_DISTRIBUTION=/home/beamboard/rpi-rgb-led-matrix
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

# WiringPi configuration for main_mode_rotary.
WIRINGPI_INCDIR?=/usr/include
WIRINGPI_CXXFLAGS?=-I$(WIRINGPI_INCDIR)
WIRINGPI_LDFLAGS?=-lwiringPi

# To compile image-example, image_and_text_input, and weather_info_panel
MAGICK_CXXFLAGS?=$(shell GraphicsMagick++-config --cppflags --cxxflags)
MAGICK_LDFLAGS?=$(shell GraphicsMagick++-config --ldflags --libs)

# Add libcurl flags for downloading images
CURL_LDFLAGS=-lcurl

all : $(BINARIES)

extras: $(EXTRA_BINARIES)

build-all: all extras

$(RGB_LIBRARY): FORCE
	$(MAKE) -C $(RGB_LIBDIR)

demo : demo-main.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS)

minimal-example : minimal-example.o
input-example : input-example.o
text-example: text-example.o
scrolling-text-example : scrolling-text-example.o
clock : clock.o
ledcat : ledcat.o
pixel-mover : pixel-mover.o
ollama-to-led : ollama-to-led.o
updated_ollama : updated_ollama.o

# All the binaries that have the same name as the object file.
% : %.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS)

# main_mode_rotary requires WiringPi.
main_mode_rotary.o : main_mode_rotary.cpp
	$(CXX) -I$(RGB_INCDIR) $(WIRINGPI_CXXFLAGS) $(CXXFLAGS) -c -o $@ $<

main_mode_rotary : main_mode_rotary.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS) $(WIRINGPI_LDFLAGS)

# Rule to compile the image_and_text_input example.
image_and_text_input.o : image_and_text_input.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) $(MAGICK_CXXFLAGS) -c -o $@ $<

image_and_text_input : image_and_text_input.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS) $(MAGICK_LDFLAGS)

# Rule to compile the weather_info_panel example.
weather_info_panel.o : weather_info_panel.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) $(MAGICK_CXXFLAGS) -c -o $@ $<

weather_info_panel : weather_info_panel.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS) $(MAGICK_LDFLAGS) $(CURL_LDFLAGS)

# Rule to compile the nasa_image example.
nasa_image.o : nasa_image.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) $(MAGICK_CXXFLAGS) -c -o $@ $<

nasa_image : nasa_image.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS) $(MAGICK_LDFLAGS) $(CURL_LDFLAGS)

# Rule to compile the thermal_display example.
thermal_display.o : thermal_display.cpp
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) $(MAGICK_CXXFLAGS) -c -o $@ $<

thermal_display : thermal_display.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS) $(MAGICK_LDFLAGS)

# Rule to compile the updated_ollama example.
updated_ollama.o : updated_ollama.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) $(MAGICK_CXXFLAGS) -c -o $@ $<

updated_ollama : updated_ollama.o $(RGB_LIBRARY)
	$(CXX) $< -o $@ $(LDFLAGS) $(MAGICK_LDFLAGS) $(CURL_LDFLAGS)

# Since the C example uses the C++ library underneath, which depends on C++
# runtime stuff, you still have to also link -lstdc++
c-example : c-example.o $(RGB_LIBRARY)
	$(CC) $< -o $@ $(LDFLAGS) -lstdc++

master_script: master_script.cpp
	g++ -o $@ $< -lgpiod -lpthread

buffer_process: buffer_process.cpp
	g++ -o $@ $<

db_manager: db_manager.cpp
	g++ -std=c++17 -o $@ $< -lsqlite3

%.o : %.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -c -o $@ $<

%.o : %.cpp
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -c -o $@ $<

%.o : %.c
	$(CC) -I$(RGB_INCDIR) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(BINARIES)

clean-all: clean
	rm -f $(EXTRA_BINARIES)

FORCE:
.PHONY: all extras build-all clean clean-all FORCE
