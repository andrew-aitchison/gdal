

include ../../../GDALmake.opt

OBJ	=	ogrwalkdriver.o ogrwalkdatasource.o ogrwalklayer.o ogrwalktablelayer.o ogrwalkselectlayer.o ogrwalktool.o

CPPFLAGS	:=	-iquote .. -iquote ../.. -iquote ../generic -iquote ../pgeo  $(CPPFLAGS)

default:	$(O_OBJ:.o=.$(OBJ_EXT))

clean:
	rm -f *.o $(O_OBJ)

$(O_OBJ):  ogrwalk.h ../generic/ogrwarpedlayer.h ../generic/ogrunionlayer.h
