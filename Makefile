wav2prg: wav2prg_api.o \
         main.o \
         turbotape.o \
         kernal.o \
         loaders.o \
         novaload.o \
         dependency_tree.o \
         audiogenic.o \
         pavlodapenetrator.o \
         connection.o \
         pavlodaold.o
	$(CC) -o $@ $^

