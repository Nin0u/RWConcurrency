CC=gcc -Wall -g
INCL=-Iinclude
CCO=$(CC) $(INCL) -c $< -o $@

# Make an obj/ directrory to store all .o files
OBJ_DIR=@mkdir obj -p

INC=include/owner.h include/rl_lock.h include/rl_open_file.h include/rl_descriptor.h include/rl_all_file.h

# Final Objects
OBJECTS=obj/main.o obj/rl_library_lock.o
TOBJECTS=obj/test1.o obj/test2.o

# Output files' names.
TARGET=main
TTARGET=test

all: $(TARGET) $(TTARGET)

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
#==== Test ====#
test: $(TOBJECTS)
	$(CC) -o test1 obj/test1.o obj/rl_library_lock.o
	$(CC) -o test2 obj/test2.o obj/rl_library_lock.o

obj/test1.o: src/test1.c include/rl_library_lock.h
	$(OBJ_DIR)
	$(CCO)

obj/test2.o: src/test2.c include/rl_library_lock.h
	$(OBJ_DIR)
	$(CCO)

