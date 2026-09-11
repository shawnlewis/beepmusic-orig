COLLECTD_CFLAGS = -DHAVE_CONFIG_H \
	-I. -DPREFIX=\"/usr\" \
	-DCONFIGFILE=\"/etc/collectd.conf\" \
	-DLOCALSTATEDIR=\"/var\" \
	-DPKGLOCALSTATEDIR=\"/var/lib/collectd\" \
	-DPIDFILE=\"/var/run/collectd.pid\" \
	-DPLUGINDIR=\"/usr/lib/collectd\" \
	-DPKGDATADIR=\"/usr/share/collectd\"

SMAPS_OBJ = smaps.o
UBUSMON_OBJ = ubusmon.o

OBJS = $(SMAPS_OBJ) $(UBUSMON_OBJ)

all: compile

%.o: %.c
	$(CC) $(COLLECTD_CFLAGS) $(CPPFLAGS) $(CFLAGS) -I$(STAGING_DIR)/usr/include/collectd -fpic -DPIC -c $< -o $@

compile: $(OBJS)
	$(CC) -shared -fPIC -DPIC $(SMAPS_OBJ) $(LDFLAGS) $(CFLAGS) -Wl,-soname -Wl,smaps.so -o smaps.so
	$(CC) -shared -fPIC -DPIC $(UBUSMON_OBJ) $(LDFLAGS) $(CFLAGS) -Wl,-soname -Wl,ubusmon.so -o ubusmon.so -lubus -lubox
