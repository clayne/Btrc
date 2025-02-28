﻿// comes from https://stackoverflow.com/questions/11813271/embed-resources-eg-shader-code-images-into-executable-library-with-cmake

#include <stdlib.h>
#include <stdio.h>

FILE* open_or_exit(const char* fname, const char* mode)
{
  FILE* f = fopen(fname, mode);
  if (f == NULL) {
    perror(fname);
    exit(EXIT_FAILURE);
  }
  return f;
}

int main(int argc, char** argv)
{
  if (argc < 3) {
    printf("USAGE: %s {file} {sym} {rsrc}\n\n"
        "  Creates file from the contents of {rsrc}\n",
        argv[0]);
    return EXIT_FAILURE;
  }

  const char *outfile = argv[1];
  const char* sym = argv[2];
  FILE* in = open_or_exit(argv[3], "r");
  
  FILE* out = open_or_exit(outfile,"w");
  fprintf(out, "inline const char %s[] = {\n", sym);

  unsigned char buf[256];
  size_t nread = 0;
  size_t linecount = 0;
  do {
    nread = fread(buf, 1, sizeof(buf), in);
    size_t i;
    for (i=0; i < nread; i++) {
      if(linecount == 0) fprintf(out, "    ");
      fprintf(out, "0x%02x, ", buf[i]);
      if (++linecount == 10) { fprintf(out, "\n"); linecount = 0; }
    }
  } while (nread > 0);
  if (linecount > 0) fprintf(out, "\n");
  fprintf(out, "};\n");

  fclose(in);
  fclose(out);

  return EXIT_SUCCESS;
}
