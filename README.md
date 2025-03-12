 Ambient AIoT LED Black Board Codebase 

Tested on Raspberry Pi 4, Bookworm OS for main application 
Raspberry Pi Pico w, Circuitpython for pico code 

--

To deploy download the repo
Install rpi-rgb-matrix-library (in Documents)

Get to examples

Delete all files inside 
Copy main application there 

Currently need to manually compile each file with terminal commands provided in readme

It is possinle to change number of available modes in main script and do not compile 
Code should work even if some modes are not responding - simply switch to the next mode 


It is possible that some libraries you might need to install are not listed
If facing trouble - copy errors output from terminal to duck.ai and debug (delete your username) 

Always keep pico connected before booting the pi 
It ensures that you get pico on port 0 

It still can go to port 1 sometimes 
Current code implementation requires it to be on 0 

--

Repo structure  

README.md

1. rpi_pico_code 
	- code.py
	- README.md

2. rpi-rgb-matrix-library 
	- README.md

3. main_application (master, buffer, 18 modes + sublevel Python codes, clone,) 
	- master_script.cpp
	- buffer_process.cpp
	- db_manager.cpp
	- clone_db.sh
	- setup_ssh_keys.sh
	- Makefile
	- README.md

	- updated_pong_with_rotary.cpp
	- brick_breaker.cpp
	- wave_gen.cpp
	- sensor_driven_visuals_7.cpp
	- falling_sand.cpp
	- snake_in_labyrinth.cpp
	- updated_ollama.cpp
	- ollama_no_input.py
	- stars.cpp
	- cat_n_mouse.cpp
	- 2_mice_1_cat.cpp
	- weather_info_panel.cc
	- buienRadarToCsv.py
	- nasa_image.cc
	- nasa_image.py
	- falling_sand_rotary_color.cpp
	- dynamic_fireworks_rotary.cpp
	- interactive_weather_rotary.cpp
	- coral_garden_rotary.cpp
	- 2_player_pong.cpp
	- main_mode_rotary.cpp
	- 3d_labyrinth_control.cpp

	- 5x8.bdf
	- 6x9.bdf

