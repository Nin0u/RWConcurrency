CC=gcc -Wall -g
INCL=-Iinclude
CCO=$(CC) $(INCL) -c $< -o $@

# Make an obj/ directrory to store all .o files
OBJ_DIR=@mkdir obj -p

INC=include/owner.h include/rl_lock.h include/rl_open_file.h include/rl_descriptor.h include/rl_all_file.h

# Final Objects
OBJECTS=obj/main.o obj/rl_library_lock.o

# Output files' names.
TARGET=main

all: $(TARGET)

#==== Clean rule =====#
clean:
	rm -rf obj/ $(TARGET)

#==== Main ====#
main: $(OBJECTS)
	$(CC) -o main $(OBJECTS)

obj/main.o: src/main.c include/rl_library_lock.h
	$(OBJ_DIR)
	$(CCO)

obj/rl_library_lock.o: src/rl_library_lock.c include/rl_library_lock.h $(INC)
	$(OBJ_DIR)
	$(CCO)

