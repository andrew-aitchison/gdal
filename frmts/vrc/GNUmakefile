# Inspired by ../grib/GNUmakefile

# EXTRAFLAGS = -iquote vrc
EXTRAFLAGS =

include ../../GDALmake.opt

OBJ	=	png_crc.o VRC.o VRCutils.o VRCthirtysix.o VRHV.o


#CPPFLAGS        :=       -iquote .. $(CPPFLAGS) -DFRMT_vrhv
CPPFLAGS        :=       -iquote .. $(CPPFLAGS)

LO_O_OBJ       =       $(addsuffix .lo,$(basename $(O_OBJ)))

#LNK_FLAGS		+=		-shared
LD_SHARED		= $(LD) -shared

ifeq ($(PNG_SETTING),internal)
EXTRAFLAGS      :=      $(EXTRAFLAGS) -I../png/libpng -DUSE_PNG
ifeq ($(LIBZ_SETTING),internal)
EXTRAFLAGS      :=      $(EXTRAFLAGS) -I../zlib
endif
endif

ifeq ($(PNG_SETTING),external)
EXTRAFLAGS      :=      $(EXTRAFLAGS) -DUSE_PNG
endif

#CPPFLAGS	:=	 $(EXTRAFLAGS) $(CPPFLAGS) -DFRMT_vrhv
CPPFLAGS	:=	 $(EXTRAFLAGS) $(CPPFLAGS)

default:	$(OBJ:.o=.$(OBJ_EXT))

../o/%.$(OBJ_EXT):	vrc/%.c
	$(CC) -c $(GDAL_INCLUDE) $(CPPFLAGS) $(CFLAGS) $< -o $@

../o/%.$(OBJ_EXT):	vrc/%.cpp
	$(CXX) -c $(GDAL_INCLUDE) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

%.$(OBJ_EXT):	vrc/%.c
	$(CC) -c $(GDAL_INCLUDE) $(CPPFLAGS) $(CFLAGS) $< -o $@

%.$(OBJ_EXT):	vrc/%.cpp
	$(CXX) -c $(GDAL_INCLUDE) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(OBJ) $(O_OBJ) *.so *.lo
	rm -f $(LO_O_OBJ) .libs/*.o $(PLUGIN_SO)

$(PLUGIN_SO): $(OBJ)
	$(LD_SHARED) $(LNK_FLAGS) $(OBJ) $(CONFIG_LIBS_INS) $(LIBS) -o $(PLUGIN_SO)

install-obj:	$(O_OBJ:.o=.$(OBJ_EXT))
