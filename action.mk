${PRJ}-test: ${PRJ}
	-pkill Xnest
	Xnest :1 -ac &
	sleep 1; DISPLAY=":1" xterm &
	sleep 1; DISPLAY=":1" urxvt &
	sleep 1; DISPLAY=":1" valgrind ${T_OBJ}/${PRJ}

