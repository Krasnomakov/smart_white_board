Install rpi-rgb-led-matrix library first! 

cd rpi-rgb-led-matrix

cd examples-api-use

rm -rf *

copy all files from main_application into examples-api-use

sudo apt update 
sudo apt install g++

---
Compiling files with 9++ (considering that installation of rpi-rgb-led-matrix library was in Documents) 

g++ -o master_script_with_buffer master_script_with_buffer.cpp -lgpiod

g++ buffer_process.cpp -o buffer_process

g++ db_manager.cpp -o db_manager

g++ 2_player_pong.cpp -o 2_player_pong -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ 3d_labyrinth_control.cpp -o 3d_labyrinth_control -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ brick_breaker.cpp -o brick_breaker -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ -o falling_sand falling_sand.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -o falling_sand_rotary_color falling_sand_rotary_color.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -o interactive_weather_rotary interactive_weather_rotary.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -o main_mode main_mode.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -o sensor_driven_visuals_7 sensor_driven_visuals_7.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -o snake_in_labyrinth snake_in_labyrinth.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ updated_pong_with_rotary.cpp -o updated_pong_with_rotary -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ wave_gen.cpp -o wave_gen -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ -o 2_mice_1_cat 2_mice_1_cat.cpp -I/Lhome/pi/Documents/rpi->

g++ -o cat_n_mouse cat_n_mouse.cpp -I/Lhome/pi/Documents/rpi->
