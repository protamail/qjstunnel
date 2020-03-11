OBJDIR=.obj
CLSDIR=$(OBJDIR)/classes

HOST_CC=gcc
CC=gcc
CFLAGS=-g -O2 -flto -Wall -MMD -MF $(OBJDIR)/$(@F).d -I/usr/lib/jvm/java/include -I/usr/lib/jvm/java/include/linux -I/opt/app/workload/addon/java/jdk1.8.0_144/include -I/opt/app/workload/addon/java/jdk1.8.0_144/include/linux
CFLAGS += -Wno-array-bounds -Wno-format-truncation
AR=gcc-ar
STRIP=strip
LDFLAGS=-g
SHLIB=libquickjsc.so
JAR=libquickjsc.jar

PROGS=$(JAR) $(SHLIB)
JSRC=*.java

all: $(PROGS)

LIB_OBJS=$(OBJDIR)/QJSConnector.o

LIBS=-lm -ldl libquickjs.lto.a

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(CLSDIR):
	mkdir -p $(CLSDIR)

$(SHLIB): $(OBJDIR) $(LIB_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $(LIB_OBJS) $(LIBS)
	$(STRIP) $@

$(JAR): $(CLSDIR) $(JSRC)
	javac -d $(CLSDIR) -g -h . $(JSRC)
	jar -cf $(JAR) -C $(CLSDIR) .

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -fPIC -DJS_SHARED_LIBRARY -c -o $@ $<

clean:
	rm -rf $(OBJDIR)/ $(PROGS)

test: all
	java -cp $(JAR) -Djava.library.path=. org.scriptable.QJSConnector
