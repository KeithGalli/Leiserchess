CC = gcc
TARGET := leiserchess
SRC := util.c tt.c fen.c move_gen.c search.c eval.c
OBJ := $(SRC:.c=.o)
UNAME := $(shell uname)

ifeq ($(PARALLEL),1)
	OS_TYPE := Parallel Linux
	PFLAG := -DPARALLEL -D_BSD_SOURCE -D_XOPEN_SOURCE # -g needed for test framework assertions
	CFLAGS := -std=gnu99 -Wall -fcilkplus -g
	LDFLAGS= -Wall -lrt -lm -lcilkrts -ldl -lpthread
else
ifeq ($(UNAME),Darwin)
	OS_TYPE := Mac
	PFLAG := -DMACPORT -D__MACH__ -D_XOPEN_SOURCE
	CFLAGS := -std=gnu99 -lrt -Wall -DNDEBUG -g3
	LDFLAGS= -lrt -Wall
else
	OS_TYPE := Linux
	#PFLAG := -D_XOPEN_SOURCE
	CFLAGS := -std=gnu99 -lrt -Wall -g
	LDFLAGS= -Wall -lm -lrt -ldl -lpthread
endif
endif

#CFLAGS = -std=c99 -Wall -fcilkplus
ifeq ($(DEBUG),1)
	CFLAGS += -O0 -DDEBUG $(PFLAG)
else
	CFLAGS += -O3 -fcilkplus -DNDEBUG $(PFLAG)
endif

ifeq ($(REFERENCE),1)
	CFLAGS += -DRUN_REFERENCE_CODE=1
endif

CFLAGS += $(OTHER_CFLAGS)

LDFLAGS= -Wall -lrt -lm -lcilkrts -ldl -lpthread

.PHONY : default clean

default : $(TARGET)

# Each C source file will have a corresponding file of prerequisites.
# Include the prerequisites for each of our C source files.
-include $(SRC:.c=.d)

# This rule generates a file of dependencies (i.e., a makefile) %.d
# from the C source file %.c.
%.d : %.c
	@set -e; rm -f $@; \
	$(CC) -MM -MT $*.o -MP -MF $@.$$$$ $(CFLAGS) $<; \
	sed -e 's|\($*\)\.o[ :]*|\1.o $@ : |g' < $@.$$$$ > $@; \
	rm -f $@.$$$$*

# We could use the -MMD and -MP flags here to have this rule generate
# the dependencies file.
%.o : %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -c $< -o $@ -lrt

search.c: search_scout.c


leiserchess : leiserchess.o $(OBJ)

ifndef NAME
	$(CC) $^ $(LDFLAGS) -o $@ -lrt
else
	$(CC) $^ $(LDFLAGS) -o $(NAME) -lrt
endif

clean :
	rm -f *.o *.d* *~ $(TARGET)

ifeq ($(PROF),1)
  CFLAGS += -DPROFILE_BUILD -pg
  LDFLAGS += -pg
endif
