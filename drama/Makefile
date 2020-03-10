SDIR=src
IDIR=$(SDIR)/include
LDIR=lib
BUILD=obj
ODIR=src/.obj

CFLAGS=-I$(IDIR) #-ggdb
# CXX=g++
LDFLAGS=

OUT=tester

LDEPS=

GB_PAGE=/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
HUGEPAGE=/mnt/huge

all: $(OUT)
.PHONY: clean


SOURCES := $(wildcard $(SDIR)/*.c)
OBJECTS := $(patsubst $(SDIR)/%.c, $(ODIR)/%.o, $(SOURCES))


$(ODIR)/%.o: $(SDIR)/%.c
	mkdir -p $(ODIR)
	$(CXX) -o $@ -c $< $(CFLAGS) $(LDFLAGS) $(LDEPS)


$(OUT): $(OBJECTS) 
	mkdir -p $(BUILD)
	$(CXX) -o $(BUILD)/$@ $^ $(CFLAGS) $(LDFLAGS) $(LDEPS)
	chmod +x $(BUILD)/$@

clean:
	rm -rf $(BUILD)
	rm -rf $(ODIR)
 
setup: 
	echo "Mounting hugetlbfs"
	echo 1 | sudo tee -a $(GB_PAGE) 
	@if ! [ -d $(HUGEPAGE) ]; then\
		sudo mkdir $(HUGEPAGE);\
		sudo mount -t hugetlbfs  -o pagesize=1G none $(HUGEPAGE);\
		sudo chown pit:pit $(HUGEPAGE);\
	fi
	

teardown:
	@if [ -d $(HUGEPAGE) ]; then\
		sudo umount -f $(HUGEPAGE);\
		sudo rm -r $(HUGEPAGE);\
	fi
	echo 0 | sudo tee -a $(GB_PAGE) 

run:
	sudo $(BUILD)/$(OUT)
