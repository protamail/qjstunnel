OBJDIR=.obj
CLSDIR=$(OBJDIR)/classes

JC=javac
JC_FLAGS=-d $(CLSDIR) -g -h .

HOST_CC=gcc
CC=gcc
CFLAGS=-g -O2 -flto -Wall -MMD -MF $(OBJDIR)/$(@F).d -I/usr/lib/jvm/java/include -I/usr/lib/jvm/java/include/linux
CFLAGS += -Wno-array-bounds -Wno-format-truncation
AR=gcc-ar
STRIP=strip
LDFLAGS=-g
SHLIB=libtest.so
JAR=QJSTunnel.jar

PROGS=$(JAR) $(SHLIB)
JSRC=*.java

all: $(PROGS)

LIB_OBJS=$(OBJDIR)/test.o

LIBS=-lm -ldl libqjs.so

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(CLSDIR):
	mkdir -p $(CLSDIR)

$(SHLIB): $(OBJDIR) $(LIB_OBJS)
	$(CC) $(LDFLAGS) -fPIC -Wl,-rpath=. -shared -o $@ $(LIB_OBJS) $(LIBS)
	$(STRIP) $@

$(JAR): $(CLSDIR) $(JSRC)
	$(JC) $(JC_FLAGS) $(JSRC)
	jar -cf $(JAR) -C $(CLSDIR) .

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -fPIC -DJS_SHARED_LIBRARY -c -o $@ $<

clean:
	rm -rf $(OBJDIR)/ $(PROGS)

test: all
	java -cp $(JAR) -Djava.library.path=. QJSTunnel
